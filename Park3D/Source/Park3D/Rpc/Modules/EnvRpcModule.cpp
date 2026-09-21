// Copyright Epic Games, Inc. All Rights Reserved.

#include "EnvRpcModule.h"
#include "../RpcDispatcher.h"
#include "../RpcParamUtil.h"
#include "../../Park3DDataPaths.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetData.h"
#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonReader.h"
#include "../../Env/EnvActorLibrary.h"

namespace
{
	/** 배치 에셋을 찾는 콘텐츠 루트. 바로 아래 폴더(Building · Props …)가 category 가 된다. */
	const TCHAR* EnvAssetRoot = TEXT("/Game/Environment");

	/** env.create 가 스폰한 액터의 표식. update/delete/clear/save 는 이 태그가 있는 것만 다룬다. */
	const FName EnvPropTag(TEXT("EnvProp"));

	/** 계약의 미터 → 월드 cm. car.create(UnrealMetersToWorld)와 같은 값. */
	constexpr float EnvMetersToUU = 100.f;

	TSharedPtr<FJsonObject> ActorToDto(AActor* Actor, const FVector& From, bool bHasFrom)
	{
		const FVector Origin = Actor->GetActorLocation();
		FVector BoundsOrigin, BoundsExtent;
		Actor->GetActorBounds(/*bOnlyCollidingComponents=*/false, BoundsOrigin, BoundsExtent);

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), Actor->GetName());
		O->SetStringField(TEXT("label"), Actor->GetActorNameOrLabel());
		O->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
		O->SetObjectField(TEXT("pos"), RpcDto::Vec3(Origin.X, Origin.Y, Origin.Z));
		O->SetObjectField(TEXT("size"), RpcDto::Vec3(BoundsExtent.X * 2.0, BoundsExtent.Y * 2.0, BoundsExtent.Z * 2.0));
		O->SetBoolField(TEXT("hidden"), Actor->IsHidden());
		if (bHasFrom)
		{
			O->SetNumberField(TEXT("distance"), FVector::Dist(From, Origin));
		}
		return O;
	}

	/** {roll?,pitch?,yaw?} 서브객체 → FRotator. 없는 성분은 Default 성분, 키 자체가 없으면 Default. */
	FRotator ReadRot(const TSharedPtr<FJsonObject>& P, const FString& Key, const FRotator& Default)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!P.IsValid() || !P->TryGetObjectField(Key, Obj) || !Obj || !Obj->IsValid())
		{
			return Default;
		}
		FRotator R = Default;
		double V = 0.0;
		if ((*Obj)->TryGetNumberField(TEXT("roll"), V))  { R.Roll = V; }
		if ((*Obj)->TryGetNumberField(TEXT("pitch"), V)) { R.Pitch = V; }
		if ((*Obj)->TryGetNumberField(TEXT("yaw"), V))   { R.Yaw = V; }
		return R;
	}

	TSharedPtr<FJsonObject> RotToJson(const FRotator& R)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("roll"), R.Roll);
		O->SetNumberField(TEXT("pitch"), R.Pitch);
		O->SetNumberField(TEXT("yaw"), R.Yaw);
		return O;
	}

	/**
	 * 프롭 DTO — OmiPark3D actor_dto(EnvProp) 와 같은 키. pos·size 는 계약 미터(입력 pos 와 같은 단위라 그대로 되돌려 넣을 수 있다).
	 * env.list 의 ActorToDto 가 cm 를 내는 것과 다르다 — 그쪽은 기존 계약이라 손대지 않는다.
	 */
	TSharedPtr<FJsonObject> PropToDto(const FEnvRpcModule::FEnvPropRecord& R)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), R.Name);
		O->SetStringField(TEXT("label"), R.Label);
		O->SetStringField(TEXT("class"), TEXT("Prop"));
		O->SetStringField(TEXT("asset"), R.Asset);
		O->SetStringField(TEXT("group"), R.Group);
		AActor* Actor = R.Actor.Get();
		if (Actor)
		{
			const FVector Pos = Actor->GetActorLocation() / EnvMetersToUU;
			FVector BoundsOrigin, BoundsExtent;
			Actor->GetActorBounds(/*bOnlyCollidingComponents=*/false, BoundsOrigin, BoundsExtent);
			const FVector Size = BoundsExtent * 2.0 / EnvMetersToUU;
			const FVector Scale = Actor->GetActorScale3D();
			O->SetObjectField(TEXT("pos"), RpcDto::Vec3(Pos.X, Pos.Y, Pos.Z));
			O->SetObjectField(TEXT("size"), RpcDto::Vec3(Size.X, Size.Y, Size.Z));
			O->SetBoolField(TEXT("hidden"), Actor->IsHidden());
			O->SetObjectField(TEXT("rot"), RotToJson(Actor->GetActorRotation()));
			O->SetObjectField(TEXT("scale"), RpcDto::Vec3(Scale.X, Scale.Y, Scale.Z));
		}
		return O;
	}

	/** env.save/load 경로: fullPath > Save/3D/Env + fileName(기본 EnvProps). car.save 와 같은 규약. */
	FString ResolveEnvPath(const TSharedPtr<FJsonObject>& P)
	{
		const FString FullPath = RpcParam::GetString(P, TEXT("fullPath"));
		if (!FullPath.IsEmpty())
		{
			return FullPath;
		}
		FString FileName = RpcParam::GetString(P, TEXT("fileName"), TEXT("EnvProps"));
		if (!FileName.EndsWith(TEXT(".json")))
		{
			FileName += TEXT(".json");
		}
		return Park3DDataPaths::GetDataFilePath(TEXT("Env"), *FileName);
	}

	/** name | names[] (+ 선택 group 은 호출부가 더한다) → 집합. env.hide 와 같은 읽기. */
	void ReadNameSet(const TSharedPtr<FJsonObject>& P, TSet<FString>& Out)
	{
		const FString Single = RpcParam::GetString(P, TEXT("name"));
		if (!Single.IsEmpty())
		{
			Out.Add(Single);
		}
		const TArray<TSharedPtr<FJsonValue>>* NamesArr = nullptr;
		if (P.IsValid() && P->TryGetArrayField(TEXT("names"), NamesArr) && NamesArr)
		{
			for (const TSharedPtr<FJsonValue>& V : *NamesArr)
			{
				FString S;
				if (V.IsValid() && V->TryGetString(S) && !S.IsEmpty())
				{
					Out.Add(S);
				}
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> SortedStrings(const TArray<FString>& In)
	{
		TArray<FString> Sorted = In;
		Sorted.Sort();
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FString& S : Sorted)
		{
			Arr.Add(MakeShared<FJsonValueString>(S));
		}
		return Arr;
	}

	/** 프롭 슬러그가 주차면 판(bay_*)인가 — OmiPark3D is_bay_asset 과 같은 접두 규약. */
	bool IsBaySlug(const FString& Asset)
	{
		return Asset.StartsWith(TEXT("bay_"), ESearchCase::IgnoreCase);
	}
}

// ===== 에셋 목록 =====

void FEnvRpcModule::EnsureAssetCache(bool bForceRescan)
{
	if (bAssetsScanned && !bForceRescan)
	{
		return;
	}
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	// 에디터는 기동 직후 비동기 수집 중일 수 있어 그 폴더만 동기 스캔해 목록을 확정한다.
	// 쿡 빌드는 AssetRegistry.bin 이 이미 전부 담고 있으므로 강제 재스캔(env.reloadAssets)이 아니면 건너뛴다.
	if (bForceRescan || Registry.IsLoadingAssets())
	{
		TArray<FString> Paths;
		Paths.Add(FString(EnvAssetRoot));
		Registry.ScanPathsSynchronous(Paths, bForceRescan);
	}

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(EnvAssetRoot));
	Filter.bRecursivePaths = true;
	Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	TArray<FAssetData> Found;
	Registry.GetAssets(Filter, Found);

	// 경로순으로 돌려야 같은 이름이 두 폴더에 있을 때 어느 쪽이 원래 slug 를 갖는지가 실행마다 같다.
	Found.Sort([](const FAssetData& A, const FAssetData& B) { return A.GetObjectPathString() < B.GetObjectPathString(); });

	AssetsBySlug.Reset();
	const FString RootPrefix = FString(EnvAssetRoot) + TEXT("/");
	for (const FAssetData& A : Found)
	{
		FEnvAssetEntry E;
		E.Name = A.AssetName.ToString();
		E.ObjectPath = A.GetObjectPathString();
		E.Package = A.PackageName.ToString();

		FString Rel = A.PackagePath.ToString();   // /Game/Environment/Props/TrashProps
		if (Rel.RemoveFromStart(RootPrefix))
		{
			int32 Slash = INDEX_NONE;
			if (Rel.FindChar(TEXT('/'), Slash))
			{
				Rel = Rel.Left(Slash);
			}
		}
		else
		{
			Rel.Reset(); // 루트 바로 아래 파일 — 폴더 없음
		}
		E.Category = Rel.ToLower();

		// 같은 이름이 다른 폴더에 있으면 두 번째부터 `<category>_<이름>`(OmiPark3D #504 와 같은 규약). 그래도 겹치면 버린다.
		E.Slug = E.Name.ToLower();
		if (AssetsBySlug.Contains(E.Slug))
		{
			E.Slug = E.Category + TEXT("_") + E.Slug;
			if (AssetsBySlug.Contains(E.Slug))
			{
				continue;
			}
		}
		AssetsBySlug.Add(E.Slug, E);
	}
	bAssetsScanned = true;
	UE_LOG(LogTemp, Log, TEXT("[Env] 배치 에셋 목록: %d개 (%s%s)"), AssetsBySlug.Num(), EnvAssetRoot,
		bForceRescan ? TEXT(", 강제 재스캔") : TEXT(""));
}

UStaticMesh* FEnvRpcModule::ResolveMesh(const FString& SlugOrPath, FString& OutAsset, FRpcError& OutError)
{
	const FString Key = SlugOrPath.TrimStartAndEnd();
	if (Key.IsEmpty())
	{
		OutError.FailDomain(TEXT("필수 파라미터 누락: asset"));
		return nullptr;
	}

	FString ObjectPath;
	if (Key.StartsWith(TEXT("/")))
	{
		// 오브젝트 경로 — 루트 밖 에셋(엔진 큐브 등)도 허용한다. `.이름` 접미가 없으면 붙인다.
		ObjectPath = Key;
		const FString Short = FPaths::GetCleanFilename(Key);
		int32 Dot = INDEX_NONE;
		if (!Short.FindChar(TEXT('.'), Dot))
		{
			ObjectPath += TEXT(".") + Short;
		}
		OutAsset = ObjectPath;
		// 목록 안의 에셋이면 slug 로 기록한다 — 파일에 slug 가 남아야 다른 기계에서도 같은 에셋을 찾는다.
		EnsureAssetCache(false);
		for (const TPair<FString, FEnvAssetEntry>& It : AssetsBySlug)
		{
			if (It.Value.ObjectPath.Equals(ObjectPath, ESearchCase::IgnoreCase))
			{
				OutAsset = It.Key;
				break;
			}
		}
	}
	else
	{
		EnsureAssetCache(false);
		const FEnvAssetEntry* Entry = AssetsBySlug.Find(Key.ToLower());
		if (!Entry)
		{
			OutError.FailDomain(FString::Printf(TEXT("환경 에셋 없음: %s (env.assets 로 확인)"), *Key));
			return nullptr;
		}
		ObjectPath = Entry->ObjectPath;
		OutAsset = Entry->Slug;
	}

	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *ObjectPath);
	if (!Mesh)
	{
		OutError.FailDomain(FString::Printf(TEXT("환경 에셋 로드 실패: %s"), *ObjectPath));
		return nullptr;
	}
	return Mesh;
}

// ===== 프롭 =====

void FEnvRpcModule::PruneProps(UWorld* World)
{
	for (auto It = Props.CreateIterator(); It; ++It)
	{
		AActor* Actor = It.Value().Actor.Get();
		if (!Actor || !IsValid(Actor) || Actor->GetWorld() != World)
		{
			It.RemoveCurrent();
		}
	}
}

FString FEnvRpcModule::NewPropName(const FString& Asset)
{
	// 경로로 만든 프롭은 짧은 이름만 쓴다("/Engine/BasicShapes/Cube.Cube" → "cube").
	FString Base = Asset;
	if (Base.StartsWith(TEXT("/")))
	{
		Base = FPaths::GetBaseFilename(Base).ToLower();
	}
	for (;;)
	{
		++PropSerial;
		const FString Name = FString::Printf(TEXT("%s_%d"), *Base, PropSerial);
		if (!Props.Contains(Name))
		{
			return Name;
		}
	}
}

FEnvRpcModule::FEnvPropRecord* FEnvRpcModule::SpawnProp(UWorld* World, UStaticMesh* Mesh, const FString& Asset, const FString& Name,
	const FString& Label, const FString& Group, const FVector& PosMeters, const FRotator& Rot, const FVector& Scale)
{
	const FString Wanted = (Name.IsEmpty() || Props.Contains(Name)) ? NewPropName(Asset) : Name;

	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SP.Name = FName(*Wanted);
	// 레벨에 같은 이름의 액터가 이미 있으면(파괴 뒤 GC 전 포함) 엔진이 새 이름을 짓는다 — 기록은 실제 이름을 따른다.
	SP.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;

	AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), PosMeters * EnvMetersToUU, Rot, SP);
	if (!Actor)
	{
		return nullptr;
	}
	// 기본 모빌리티(Static)로는 플레이 중 SetStaticMesh·이동이 거부된다. 메시를 넣기 전에 바꿔야 한다.
	if (UStaticMeshComponent* Comp = Actor->GetStaticMeshComponent())
	{
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetStaticMesh(Mesh);
	}
	Actor->SetActorScale3D(Scale);
	Actor->Tags.Add(EnvPropTag);

	FEnvPropRecord R;
	R.Name = Actor->GetName();
	R.Asset = Asset;
	R.Label = Label;
	R.Group = Group;
	R.Actor = Actor;
	return &Props.Add(R.Name, R);
}

FEnvRpcModule::FEnvPropRecord* FEnvRpcModule::FindProp(const FString& Name, FRpcError& OutError)
{
	FEnvPropRecord* R = Props.Find(Name);
	if (!R || !R->Actor.IsValid())
	{
		OutError.FailDomain(FString::Printf(TEXT("프롭 없음: %s (env.list nameLike 로 확인)"), *Name));
		return nullptr;
	}
	return R;
}

bool FEnvRpcModule::RemoveProp(const FString& Name)
{
	FEnvPropRecord* R = Props.Find(Name);
	if (!R)
	{
		return false;
	}
	if (AActor* Actor = R->Actor.Get())
	{
		Actor->Destroy();
		// 파괴된 액터는 GC 전까지 이름을 쥐고 있다(UWorld::SpawnActor 의 StaticFindObjectFast 에 걸린다). 그대로 두면
		// 같은 이름을 바로 다시 놓을 때(env.clear → env.load, 같은 name 재생성) 엔진이 `<이름>_0` 을 지어 파일의 name 과
		// 월드의 name 이 갈라진다 → 이름을 비워 준다(에디터의 액터 교체가 쓰는 수법). 실측: SaveLoad 테스트의 'A 복원' 실패.
		Actor->Rename(nullptr, nullptr, REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
	}
	Props.Remove(Name);
	return true;
}

// ===== 맵 오브젝트 숨김 =====

void FEnvRpcModule::FillMapState(UWorld* World, const TSharedPtr<FJsonObject>& Root)
{
	int32 MapCount = 0, HiddenCount = 0, KeptCount = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Park3DEnv::IsEnvActor(Actor))
		{
			continue;
		}
		const FEnvPropRecord* R = Props.Find(Actor->GetName());
		if (Park3DEnv::IsMapKeepActor(Actor) || (R && IsBaySlug(R->Asset)))
		{
			++KeptCount;
			continue;
		}
		++MapCount;
		if (Actor->IsHidden())
		{
			++HiddenCount;
		}
	}
	Root->SetNumberField(TEXT("mapCount"), MapCount);
	Root->SetNumberField(TEXT("hiddenCount"), HiddenCount);
	Root->SetNumberField(TEXT("keptCount"), KeptCount);
	Root->SetBoolField(TEXT("hidden"), MapCount > 0 && HiddenCount == MapCount);
}

int32 FEnvRpcModule::SetMapHidden(UWorld* World, bool bHidden)
{
	int32 Changed = 0;
	if (bHidden)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Park3DEnv::IsEnvActor(Actor) || Actor->IsHidden())
			{
				continue; // 이미 숨겨진 것은 changedCount 에서 빼고, 되돌릴 집합에도 넣지 않는다(env.hide 의 몫).
			}
			const FEnvPropRecord* R = Props.Find(Actor->GetName());
			if (Park3DEnv::IsMapKeepActor(Actor) || (R && IsBaySlug(R->Asset)))
			{
				continue;
			}
			Park3DEnv::SetActorHidden(Actor, true);
			MapHidden.Add(Actor);
			++Changed;
		}
	}
	else
	{
		for (const TWeakObjectPtr<AActor>& Weak : MapHidden)
		{
			AActor* Actor = Weak.Get();
			if (Actor && IsValid(Actor) && Actor->GetWorld() == World && Actor->IsHidden())
			{
				Park3DEnv::SetActorHidden(Actor, false);
				++Changed;
			}
		}
		MapHidden.Reset();
	}
	return Changed;
}

// ===== 등록 =====

void FEnvRpcModule::Register(URpcDispatcher& Dispatcher)
{
	// env.list — 레벨 액터를 찾는다. 거리·이름으로 좁히고 가까운 순으로 돌려준다.
	//   near {x,y,z} + radius(cm) : 그 점 주변만. radius 만 주면 near 는 (0,0,0).
	//   nameLike : 이름/라벨/클래스에 이 문자열이 들어간 것만(대소문자 무시).
	//   limit : 기본 30. 레벨에는 액터가 수천 개라 통째로 돌려주면 읽을 수 없다.
	Dispatcher.Register(TEXT("env.list"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* World = GetWorldPtr();
		if (!World) { E.FailDomain(TEXT("월드 없음(맵 미로드)")); return nullptr; }

		const bool bHasNear = RpcParam::Has(P, TEXT("near"));
		const FVector Near = RpcParam::GetVec3(P, TEXT("near"));
		const double Radius = RpcParam::GetFloat(P, TEXT("radius"), 0.0);
		const FString NameLike = RpcParam::GetString(P, TEXT("nameLike"));
		const int32 Limit = FMath::Clamp(RpcParam::GetInt(P, TEXT("limit"), 30), 1, 300);

		struct FHit { AActor* Actor; double Dist; };
		TArray<FHit> Hits;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Park3DEnv::IsEnvActor(Actor))
			{
				continue;
			}
			const double Dist = FVector::Dist(Near, Actor->GetActorLocation());
			if (Radius > 0.0 && Dist > Radius)
			{
				continue;
			}
			if (!NameLike.IsEmpty())
			{
				const bool bMatch = Actor->GetName().Contains(NameLike)
					|| Actor->GetActorNameOrLabel().Contains(NameLike)
					|| Actor->GetClass()->GetName().Contains(NameLike);
				if (!bMatch)
				{
					continue;
				}
			}
			Hits.Add({ Actor, Dist });
		}
		Hits.Sort([](const FHit& A, const FHit& B) { return A.Dist < B.Dist; });

		TArray<TSharedPtr<FJsonValue>> Arr;
		for (int32 i = 0; i < Hits.Num() && i < Limit; ++i)
		{
			Arr.Add(MakeShared<FJsonValueObject>(ActorToDto(Hits[i].Actor, Near, bHasNear || Radius > 0.0)));
		}

		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetArrayField(TEXT("actors"), Arr);
		Root->SetNumberField(TEXT("matched"), Hits.Num());   // 잘라 낸 개수를 알 수 있게 총계도 준다.
		Root->SetNumberField(TEXT("returned"), Arr.Num());
		return RpcDto::MakeObject(Root);
	});

	// env.hide — 이름으로 찾아 숨기거나 되돌린다. name 하나 또는 names 배열.
	// 숨길 때 충돌도 함께 끈다: 안 그러면 보이지 않는 벽이 남아 클릭 피킹(view.pick)과
	// 바닥 트레이스가 그 자리에서 막힌다. 되돌릴 때 함께 켠다.
	Dispatcher.Register(TEXT("env.hide"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* World = GetWorldPtr();
		if (!World) { E.FailDomain(TEXT("월드 없음(맵 미로드)")); return nullptr; }

		TSet<FString> Wanted;
		const FString Single = RpcParam::GetString(P, TEXT("name"));
		if (!Single.IsEmpty())
		{
			Wanted.Add(Single);
		}
		const TArray<TSharedPtr<FJsonValue>>* NamesArr = nullptr;
		if (P.IsValid() && P->TryGetArrayField(TEXT("names"), NamesArr) && NamesArr)
		{
			for (const TSharedPtr<FJsonValue>& V : *NamesArr)
			{
				FString S;
				if (V.IsValid() && V->TryGetString(S) && !S.IsEmpty())
				{
					Wanted.Add(S);
				}
			}
		}
		if (Wanted.Num() == 0)
		{
			E.FailDomain(TEXT("name 또는 names 가 필요합니다(env.list 의 name 값을 씁니다)"));
			return nullptr;
		}
		const bool bHidden = RpcParam::GetBool(P, TEXT("hidden"), true);

		// 실제 숨김은 공용 로직이 한다 — 시작 시 config 의 hide_actors 적용도 같은 함수를 쓴다.
		TArray<TSharedPtr<FJsonValue>> Changed;
		TSet<FString> Found;
		for (AActor* Actor : Park3DEnv::SetHiddenByNames(World, Wanted, bHidden))
		{
			Found.Add(Actor->GetName());
			Changed.Add(MakeShared<FJsonValueObject>(ActorToDto(Actor, FVector::ZeroVector, false)));
		}

		// 못 찾은 이름은 조용히 넘기지 않는다 — 오타 하나로 "숨겼는데 그대로"가 되기 때문이다.
		TArray<TSharedPtr<FJsonValue>> Missing;
		for (const FString& Name : Wanted)
		{
			if (!Found.Contains(Name))
			{
				Missing.Add(MakeShared<FJsonValueString>(Name));
			}
		}

		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("hidden"), bHidden);
		Root->SetArrayField(TEXT("changed"), Changed);
		Root->SetArrayField(TEXT("notFound"), Missing);
		return RpcDto::MakeObject(Root);
	});

	// ---- 확장 (OmiPark3D env.py 되가져오기) ----

	// env.assets — /Game/Environment 아래 StaticMesh 목록 {assets:[{slug, category, name, path, ueAsset, size?}], count}.
	//   nameLike : slug 에 이 문자열이 들어간 것만. category : 그 폴더만(소문자).
	//   withSize : 기본 false. true 면 메시를 실제로 로드해 바운딩박스(m)를 넣는다 — 수백 개면 첫 호출이 수 초 걸린다.
	Dispatcher.Register(TEXT("env.assets"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		EnsureAssetCache(false);
		const FString Like = RpcParam::GetString(P, TEXT("nameLike")).ToLower();
		const FString Cat = RpcParam::GetString(P, TEXT("category")).ToLower();
		const bool bWithSize = RpcParam::GetBool(P, TEXT("withSize"), false);

		TArray<FString> Keys;
		AssetsBySlug.GetKeys(Keys);
		Keys.Sort();

		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FString& Key : Keys)
		{
			const FEnvAssetEntry& A = AssetsBySlug[Key];
			if (!Like.IsEmpty() && !A.Slug.Contains(Like)) continue;
			if (!Cat.IsEmpty() && A.Category != Cat) continue;

			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("slug"), A.Slug);
			Row->SetStringField(TEXT("category"), A.Category);
			Row->SetStringField(TEXT("name"), A.Name);
			Row->SetStringField(TEXT("path"), A.ObjectPath);
			Row->SetStringField(TEXT("ueAsset"), A.Package);
			if (bWithSize)
			{
				if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *A.ObjectPath))
				{
					const FVector Size = Mesh->GetBoundingBox().GetSize() / EnvMetersToUU;
					Row->SetObjectField(TEXT("size"), RpcDto::Vec3(Size.X, Size.Y, Size.Z));
				}
				else
				{
					Row->SetBoolField(TEXT("meshLoaded"), false);
				}
			}
			Arr.Add(MakeShared<FJsonValueObject>(Row));
		}
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetArrayField(TEXT("assets"), Arr);
		Root->SetNumberField(TEXT("count"), Arr.Num());
		return RpcDto::MakeObject(Root);
	});

	// env.create — 에셋을 놓는다. asset(slug 또는 오브젝트 경로) pos{x,z,y?} rot{roll,pitch,yaw}? scale{x,y,z}? name? label?
	Dispatcher.Register(TEXT("env.create"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* World = GetWorldPtr();
		if (!World) { E.FailDomain(TEXT("월드 없음(맵 미로드)")); return nullptr; }
		PruneProps(World);

		FString AssetKey;
		if (!RpcParam::RequireString(P, TEXT("asset"), AssetKey, E)) return nullptr;
		FVector Pos;
		if (!RpcParam::RequirePosXZ(P, TEXT("pos"), Pos, E)) return nullptr;

		FString Asset;
		UStaticMesh* Mesh = ResolveMesh(AssetKey, Asset, E);
		if (!Mesh) return nullptr;

		const FRotator Rot = ReadRot(P, TEXT("rot"), FRotator::ZeroRotator);
		const FVector Scale = RpcParam::GetVec3(P, TEXT("scale"), FVector(1.0, 1.0, 1.0));
		FEnvPropRecord* R = SpawnProp(World, Mesh, Asset, RpcParam::GetString(P, TEXT("name")),
			RpcParam::GetString(P, TEXT("label")), FString(), Pos, Rot, Scale);
		if (!R) { E.FailDomain(TEXT("프롭 생성 실패(스폰 거부)")); return nullptr; }
		return RpcDto::MakeObject(PropToDto(*R));
	});

	// env.update — 프롭을 옮기고 돌리고 늘인다. name 필수. pos{x,y,z}? | delta{x,y,z}? rot? scale? label? asset?
	// 전달한 키만 바꾼다(부분 성분도 현재 값 위에 덮는다).
	Dispatcher.Register(TEXT("env.update"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* World = GetWorldPtr();
		if (!World) { E.FailDomain(TEXT("월드 없음(맵 미로드)")); return nullptr; }
		PruneProps(World);

		FString Name;
		if (!RpcParam::RequireString(P, TEXT("name"), Name, E)) return nullptr;
		FEnvPropRecord* R = FindProp(Name, E);
		if (!R) return nullptr;
		AActor* Actor = R->Actor.Get();

		if (RpcParam::Has(P, TEXT("asset")))
		{
			FString Asset;
			UStaticMesh* Mesh = ResolveMesh(RpcParam::GetString(P, TEXT("asset")), Asset, E);
			if (!Mesh) return nullptr;
			if (AStaticMeshActor* SM = Cast<AStaticMeshActor>(Actor))
			{
				if (UStaticMeshComponent* Comp = SM->GetStaticMeshComponent()) { Comp->SetStaticMesh(Mesh); }
			}
			R->Asset = Asset;
		}

		FVector PosM = Actor->GetActorLocation() / EnvMetersToUU;
		if (RpcParam::Has(P, TEXT("pos")))   { PosM = RpcParam::GetVec3(P, TEXT("pos"), PosM); }
		if (RpcParam::Has(P, TEXT("delta"))) { PosM += RpcParam::GetVec3(P, TEXT("delta")); }
		FRotator Rot = Actor->GetActorRotation();
		if (RpcParam::Has(P, TEXT("rot")))   { Rot = ReadRot(P, TEXT("rot"), Rot); }
		Actor->SetActorLocationAndRotation(PosM * EnvMetersToUU, Rot);
		if (RpcParam::Has(P, TEXT("scale"))) { Actor->SetActorScale3D(RpcParam::GetVec3(P, TEXT("scale"), Actor->GetActorScale3D())); }
		if (RpcParam::Has(P, TEXT("label"))) { R->Label = RpcParam::GetString(P, TEXT("label"), R->Label); }

		return RpcDto::MakeObject(PropToDto(*R));
	});

	// env.delete — 프롭 삭제. name | names[] | group(파일에서 읽은 묶음 이름). 레벨 액터는 이름이 같아도 지우지 않는다.
	Dispatcher.Register(TEXT("env.delete"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* World = GetWorldPtr();
		if (!World) { E.FailDomain(TEXT("월드 없음(맵 미로드)")); return nullptr; }
		PruneProps(World);

		TSet<FString> Wanted;
		ReadNameSet(P, Wanted);
		const FString Group = RpcParam::GetString(P, TEXT("group"));
		if (!Group.IsEmpty())
		{
			for (const TPair<FString, FEnvPropRecord>& It : Props)
			{
				if (It.Value.Group == Group) { Wanted.Add(It.Key); }
			}
		}
		if (Wanted.Num() == 0)
		{
			E.FailDomain(TEXT("name, names 또는 group 이 필요합니다"));
			return nullptr;
		}

		TArray<FString> Removed, Missing;
		for (const FString& Name : Wanted)
		{
			(RemoveProp(Name) ? Removed : Missing).Add(Name);
		}
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetArrayField(TEXT("deleted"), SortedStrings(Removed));
		Root->SetNumberField(TEXT("deletedCount"), Removed.Num());
		Root->SetArrayField(TEXT("notFound"), SortedStrings(Missing));
		return RpcDto::MakeObject(Root);
	});

	// env.clear — 프롭 전부 삭제(레벨 액터는 남는다) → {deletedCount}.
	Dispatcher.Register(TEXT("env.clear"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* World = GetWorldPtr();
		if (!World) { E.FailDomain(TEXT("월드 없음(맵 미로드)")); return nullptr; }
		PruneProps(World);

		TArray<FString> Names;
		Props.GetKeys(Names);
		int32 Deleted = 0;
		for (const FString& Name : Names)
		{
			if (RemoveProp(Name)) { ++Deleted; }
		}
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetNumberField(TEXT("deletedCount"), Deleted);
		return RpcDto::MakeObject(Root);
	});

	// env.save — fullPath? | fileName?=EnvProps → Save/3D/Env/<fileName>.json
	// 파일 꼴은 OmiPark3D save_env_props 와 같다: {isUnreal, kind:"envProps", props:[{name, asset, label, group, pos, rot, scale}]}.
	// 루트에 `datas` 를 두지 않는다 — 차량·프리셋 로더가 이 파일을 자기 것으로 오인하지 않게.
	Dispatcher.Register(TEXT("env.save"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* World = GetWorldPtr();
		if (!World) { E.FailDomain(TEXT("월드 없음(맵 미로드)")); return nullptr; }
		PruneProps(World);
		const FString Path = ResolveEnvPath(P);

		TArray<FString> Names;
		Props.GetKeys(Names);
		Names.Sort();
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FString& Name : Names)
		{
			const FEnvPropRecord& R = Props[Name];
			AActor* Actor = R.Actor.Get();
			if (!Actor) continue;
			const FVector Pos = Actor->GetActorLocation() / EnvMetersToUU;
			const FVector Scale = Actor->GetActorScale3D();
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("name"), R.Name);
			Row->SetStringField(TEXT("asset"), R.Asset);
			Row->SetStringField(TEXT("label"), R.Label);
			Row->SetStringField(TEXT("group"), R.Group);
			Row->SetObjectField(TEXT("pos"), RpcDto::Vec3(Pos.X, Pos.Y, Pos.Z));
			Row->SetObjectField(TEXT("rot"), RotToJson(Actor->GetActorRotation()));
			Row->SetObjectField(TEXT("scale"), RpcDto::Vec3(Scale.X, Scale.Y, Scale.Z));
			Arr.Add(MakeShared<FJsonValueObject>(Row));
		}
		TSharedPtr<FJsonObject> Doc = MakeShared<FJsonObject>();
		Doc->SetBoolField(TEXT("isUnreal"), true);
		Doc->SetStringField(TEXT("kind"), TEXT("envProps"));
		Doc->SetArrayField(TEXT("props"), Arr);

		FString Out;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Doc.ToSharedRef(), Writer);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), /*Tree=*/true);
		if (!FFileHelper::SaveStringToFile(Out, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			E.FailDomain(FString::Printf(TEXT("환경 프롭 저장 실패: %s"), *Path));
			return nullptr;
		}
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("ok"), true);
		Root->SetStringField(TEXT("path"), Path);
		Root->SetStringField(TEXT("fileName"), FPaths::GetCleanFilename(Path));
		Root->SetNumberField(TEXT("count"), Arr.Num());
		return RpcDto::MakeObject(Root);
	});

	// env.load — 파일을 읽어 프롭을 통째로 바꾼다 → {ok, count, missingAssets, fileName}.
	// 에셋을 못 찾은 항목은 놓지 못하고 missingAssets 에만 남긴다(OmiPark3D 는 빈 프롭으로 남기지만 여기서는 메시 없는 액터를 만들지 않는다).
	Dispatcher.Register(TEXT("env.load"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* World = GetWorldPtr();
		if (!World) { E.FailDomain(TEXT("월드 없음(맵 미로드)")); return nullptr; }
		PruneProps(World);
		const FString Path = ResolveEnvPath(P);

		FString Text;
		TSharedPtr<FJsonObject> Doc;
		if (!FFileHelper::LoadFileToString(Text, *Path)
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Doc) || !Doc.IsValid()
			|| RpcParam::GetString(Doc, TEXT("kind")) != TEXT("envProps"))
		{
			E.FailDomain(FString::Printf(TEXT("환경 프롭 로드 실패: %s"), *Path));
			return nullptr;
		}
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		if (!Doc->TryGetArrayField(TEXT("props"), Rows) || !Rows)
		{
			E.FailDomain(FString::Printf(TEXT("환경 프롭 로드 실패: %s"), *Path));
			return nullptr;
		}

		// 통째로 바꾼다 — 먼저 지우고 다시 놓는다.
		TArray<FString> Old;
		Props.GetKeys(Old);
		for (const FString& Name : Old) { RemoveProp(Name); }

		TSet<FString> Missing;
		int32 Count = 0;
		for (const TSharedPtr<FJsonValue>& V : *Rows)
		{
			const TSharedPtr<FJsonObject>* Row = nullptr;
			if (!V.IsValid() || !V->TryGetObject(Row) || !Row || !Row->IsValid()) continue;
			const FString Name = RpcParam::GetString(*Row, TEXT("name"));
			const FString AssetKey = RpcParam::GetString(*Row, TEXT("asset"));
			if (Name.IsEmpty() || AssetKey.IsEmpty()) continue;

			FString Asset;
			FRpcError Ignored;
			UStaticMesh* Mesh = ResolveMesh(AssetKey, Asset, Ignored);
			if (!Mesh) { Missing.Add(AssetKey); continue; }

			if (SpawnProp(World, Mesh, Asset, Name, RpcParam::GetString(*Row, TEXT("label")), RpcParam::GetString(*Row, TEXT("group")),
				RpcParam::GetVec3(*Row, TEXT("pos")), ReadRot(*Row, TEXT("rot"), FRotator::ZeroRotator),
				RpcParam::GetVec3(*Row, TEXT("scale"), FVector(1.0, 1.0, 1.0))))
			{
				++Count;
			}
		}
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("ok"), true);
		Root->SetNumberField(TEXT("count"), Count);
		Root->SetArrayField(TEXT("missingAssets"), SortedStrings(Missing.Array()));
		Root->SetStringField(TEXT("fileName"), FPaths::GetCleanFilename(Path));
		return RpcDto::MakeObject(Root);
	});

	// env.reloadAssets — AssetRegistry 를 다시 훑어 목록을 새로 만든다 → {ok, assetCount, added, props, missingAssets}.
	// 쿡 빌드에서는 새 파일이 생길 수 없으므로 사실상 목록 재구성이다(에디터에서 임포트한 에셋을 재기동 없이 보이게).
	Dispatcher.Register(TEXT("env.reloadAssets"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		const int32 Before = AssetsBySlug.Num();
		EnsureAssetCache(/*bForceRescan=*/true);
		if (UWorld* World = GetWorldPtr()) { PruneProps(World); }

		TSet<FString> Missing;
		for (const TPair<FString, FEnvPropRecord>& It : Props)
		{
			// 경로로 만든 프롭은 목록 밖이어도 정상이다 — slug 로 기록된 것만 목록과 대조한다.
			if (!It.Value.Asset.StartsWith(TEXT("/")) && !AssetsBySlug.Contains(It.Value.Asset))
			{
				Missing.Add(It.Value.Asset);
			}
		}
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("ok"), true);
		Root->SetNumberField(TEXT("assetCount"), AssetsBySlug.Num());
		Root->SetNumberField(TEXT("added"), AssetsBySlug.Num() - Before);
		Root->SetNumberField(TEXT("props"), Props.Num());
		Root->SetArrayField(TEXT("missingAssets"), SortedStrings(Missing.Array()));
		return RpcDto::MakeObject(Root);
	});

	// env.hideMap — 바닥(도로·지면·바닥·랜드스케이프)·주차면·차량만 남기고 맵 오브젝트(건물·소품·표지·보도·나무 …)를
	// 전부 숨긴다(hidden=false 면 되돌린다). 런타임 상태 — 파일에 남지 않고 레벨을 옮기면 다시 보인다.
	// → {ok, changedCount, mapCount, hiddenCount, keptCount, hidden}
	Dispatcher.Register(TEXT("env.hideMap"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* World = GetWorldPtr();
		if (!World) { E.FailDomain(TEXT("월드 없음(맵 미로드)")); return nullptr; }
		PruneProps(World);
		const bool bHidden = RpcParam::GetBool(P, TEXT("hidden"), true);
		const int32 Changed = SetMapHidden(World, bHidden);
		UE_LOG(LogTemp, Log, TEXT("[Env] 맵 오브젝트 %s: %d개 변경"), bHidden ? TEXT("숨김") : TEXT("표시"), Changed);

		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("ok"), true);
		Root->SetNumberField(TEXT("changedCount"), Changed);
		FillMapState(World, Root);
		return RpcDto::MakeObject(Root);
	});

	// env.showMap — hideMap 이 숨긴 것을 전부 되돌린다. env.hideMap {hidden:false} 와 같지만 파라미터 없이 부른다.
	Dispatcher.Register(TEXT("env.showMap"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* World = GetWorldPtr();
		if (!World) { E.FailDomain(TEXT("월드 없음(맵 미로드)")); return nullptr; }
		PruneProps(World);
		const int32 Changed = SetMapHidden(World, false);
		UE_LOG(LogTemp, Log, TEXT("[Env] 맵 오브젝트 표시: %d개 변경"), Changed);

		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("ok"), true);
		Root->SetNumberField(TEXT("changedCount"), Changed);
		FillMapState(World, Root);
		return RpcDto::MakeObject(Root);
	});

	// env.mapState — 맵 오브젝트 숨김 상태 → {mapCount, hiddenCount, keptCount(바닥·주차면), hidden(전부 숨김이면 true)}.
	Dispatcher.Register(TEXT("env.mapState"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* World = GetWorldPtr();
		if (!World) { E.FailDomain(TEXT("월드 없음(맵 미로드)")); return nullptr; }
		PruneProps(World);
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		FillMapState(World, Root);
		return RpcDto::MakeObject(Root);
	});
}
