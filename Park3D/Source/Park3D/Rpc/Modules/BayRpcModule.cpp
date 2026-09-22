// Copyright Epic Games, Inc. All Rights Reserved.

#include "BayRpcModule.h"
#include "../RpcDispatcher.h"
#include "../RpcParamUtil.h"
#include "../../ParkingPresetManager.h"
#include "../../PresetMakerWidget.h"
#include "../../Park3DDataPaths.h"
#include "../../Env/BayPropActor.h"
#include "../../Env/LevelSlotLibrary.h"
#include "../../Config/Park3DAppConfig.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Algo/Reverse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// 유니티 빌드에서 다른 .cpp 의 익명 네임스페이스와 이름이 겹치지 않도록 이 파일의 헬퍼는 전부 Bay 접두를 단다.
namespace
{
	/** 레벨 ISM_Slot 판 스케일에 포함된 선 두께(사방, m) — 파이썬 envimport.LINE_THICKNESS_M. 프롭 면의 판도 같은 여유를 준다. */
	constexpr float BayLevelLineM = 0.07f;
	/** 프리셋 검산 허용 오차(m) — envimport._fit_preset 의 0.02. */
	constexpr float BayFitToleranceM = 0.02f;
	/** 숨긴 레벨 면의 인스턴스 스케일. 0 은 행렬이 특이해지므로 눈에 안 보이는 작은 값으로 접는다. */
	constexpr float BayHiddenScale = 0.0001f;
	const TCHAR* BayAssetPrefix = TEXT("bay_");

	/**
	 * 주차면 종류 표 — scripts/make_bay_assets.py BAYS(내측 폭·길이·선 폭) + bay.py BAY_INNER_M(프리셋 변환용 폭·길이).
	 * PresetWidthM 이 0 이면 프리셋 변환 때 판 외측 × 스케일(파이썬의 prop.size)을 쓴다.
	 */
	struct FBayTypeDef
	{
		const TCHAR* Type;
		float WidthM;
		float LengthM;
		float LineWM;
		float PresetWidthM;
		float PresetLengthM;
	};
	const FBayTypeDef GBayTypes[] = {
		{ TEXT("normal"),       2.5f,  5.0f,  0.12f, 2.5f, 5.0f },
		{ TEXT("disabled"),     2.5f,  5.0f,  0.12f, 3.3f, 5.0f },
		{ TEXT("normal_6m"),    2.5f,  6.0f,  0.12f, 0.f,  0.f  },
		{ TEXT("normal_585cm"), 2.25f, 5.85f, 0.15f, 0.f,  0.f  },
		{ TEXT("normal_645cm"), 2.25f, 6.45f, 0.15f, 0.f,  0.f  },
	};

	const FBayTypeDef* BayFindType(const FString& Type)
	{
		for (const FBayTypeDef& T : GBayTypes)
		{
			if (Type == T.Type) return &T;
		}
		return nullptr;
	}

	TArray<FString> BaySortedTypes()
	{
		TArray<FString> Out;
		for (const FBayTypeDef& T : GBayTypes) { Out.Add(T.Type); }
		Out.Sort();
		return Out;
	}

	/** 내측 (폭, 길이) 에 가장 가까운 종류 — 레벨 면의 type 표시용(파이썬 slot_face 가 2.5 × {5,6} 으로 맞추는 것과 같은 뜻). */
	const FBayTypeDef& BayClosestType(float InnerW, float InnerL)
	{
		const FBayTypeDef* Best = &GBayTypes[0];
		float BestD = TNumericLimits<float>::Max();
		for (const FBayTypeDef& T : GBayTypes)
		{
			if (FCString::Strcmp(T.Type, TEXT("disabled")) == 0) continue; // 크기로는 일반면과 구분되지 않는다
			const float D = FMath::Abs(T.WidthM - InnerW) + FMath::Abs(T.LengthM - InnerL);
			if (D < BestD) { BestD = D; Best = &T; }
		}
		return *Best;
	}

	/** 판 외측 × |scale| (bay_dto 의 size = 에셋 바운딩박스 × |scale|). */
	FVector2D BayOuterSizeM(const FBayTypeDef& Ty, const FVector& Scale)
	{
		return FVector2D((Ty.WidthM + Ty.LineWM) * FMath::Abs(Scale.X), (Ty.LengthM + Ty.LineWM) * FMath::Abs(Scale.Y));
	}

	/** 씬의 주차면 하나(레벨 면 또는 프롭 면)를 같은 모양으로 다루는 레코드. bay.exportPresets 의 인라인 bays[] 도 이것이다. */
	struct FBayRec
	{
		FString Name;
		FString Label;
		FString Group;
		FString Type;
		FString Asset;
		FVector PosM = FVector::ZeroVector;
		float Yaw = 0.f;
		FVector Scale = FVector::OneVector;
		FVector2D SizeM = FVector2D::ZeroVector;   // 판 외측 × |scale|
		bool bHidden = false;
		bool bLevel = false;
		ABayPropActor* Actor = nullptr;            // 프롭 면
		FString LevelActor;                        // 레벨 면
		int32 LevelInstance = -1;

		/** 프리셋 변환에 쓰는 (폭, 길이) — bay.py: BAY_INNER_M.get(type) or prop.size. */
		FVector2D PresetSize() const
		{
			const FBayTypeDef* T = BayFindType(Type);
			return (T && T->PresetWidthM > 0.f) ? FVector2D(T->PresetWidthM, T->PresetLengthM) : SizeM;
		}
	};

	float BayMod360(double V)
	{
		double R = FMath::Fmod(V, 360.0);
		if (R < 0.0) R += 360.0;
		return static_cast<float>(R);
	}

	double BayRound(double V, int32 Digits)
	{
		const double M = FMath::Pow(10.0, static_cast<double>(Digits));
		return FMath::RoundToDouble(V * M) / M;
	}

	FString BayLevelKey(const FString& ActorName, int32 Instance)
	{
		return FString::Printf(TEXT("level:%s#%d"), *ActorName, Instance);
	}

	/** "BP_ParkingSlot_C_12" 의 끝 숫자(ParkingPresetManager 의 레벨 면 순서와 같은 규칙). */
	int32 BayTrailingNumber(const FString& Name)
	{
		int32 End = Name.Len();
		while (End > 0 && FChar::IsDigit(Name[End - 1])) --End;
		return End < Name.Len() ? FCString::Atoi(*Name.Mid(End)) : 0;
	}

	/** 이름 자연 정렬(숫자 구간은 수로 비교) — 파이썬 slotnumbers._natural_key. */
	bool BayNaturalLess(const FString& A, const FString& B)
	{
		int32 i = 0, j = 0;
		while (i < A.Len() && j < B.Len())
		{
			if (FChar::IsDigit(A[i]) && FChar::IsDigit(B[j]))
			{
				int64 NA = 0, NB = 0;
				while (i < A.Len() && FChar::IsDigit(A[i])) { NA = NA * 10 + (A[i] - TEXT('0')); ++i; }
				while (j < B.Len() && FChar::IsDigit(B[j])) { NB = NB * 10 + (B[j] - TEXT('0')); ++j; }
				if (NA != NB) return NA < NB;
			}
			else
			{
				if (A[i] != B[j]) return A[i] < B[j];
				++i; ++j;
			}
		}
		return (A.Len() - i) < (B.Len() - j);
	}

	float BayMetersToUU(UWorld* World)
	{
		if (AParkingPresetManager* Mgr = Cast<AParkingPresetManager>(UGameplayStatics::GetActorOfClass(World, AParkingPresetManager::StaticClass())))
		{
			return Mgr->MetersToUU > 0.f ? Mgr->MetersToUU : 100.f;
		}
		return 100.f;
	}

	bool BayIsSlotComponent(const UInstancedStaticMeshComponent* Comp)
	{
		// ISM_Slot 만 주차면이다(ISM_Stopper=스토퍼, ISM_Space=안전공간) — ParkingPresetManager·CarPlacementManager 와 같은 규약.
		return Comp && Comp->GetStaticMesh() && Comp->GetName().Contains(TEXT("Slot"));
	}

	/** 레벨 BP_ParkingSlot 액터를 이름 번호순으로(preset.numbers 의 레벨 면 순서와 같다). */
	void BayCollectSlotActors(UWorld* World, TArray<AActor*>& Out)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (IsValid(*It) && It->GetClass()->GetName().StartsWith(TEXT("BP_ParkingSlot")))
			{
				Out.Add(*It);
			}
		}
		Out.Sort([](const AActor& A, const AActor& B)
		{
			const int32 NA = BayTrailingNumber(A.GetName());
			const int32 NB = BayTrailingNumber(B.GetName());
			return NA != NB ? NA < NB : A.GetName() < B.GetName();
		});
	}

	/** 레벨에 ISM_Slot 이 있으면 그 재질 — 프롭 면이 레벨 면과 같은 구획선으로 보이게. 없으면 nullptr(엔진 기본 재질). */
	UMaterialInterface* BayFindLevelSlotMaterial(UWorld* World)
	{
		TArray<AActor*> SlotActors;
		BayCollectSlotActors(World, SlotActors);
		for (AActor* Actor : SlotActors)
		{
			TArray<UInstancedStaticMeshComponent*> Comps;
			Actor->GetComponents(Comps);
			for (UInstancedStaticMeshComponent* Comp : Comps)
			{
				if (BayIsSlotComponent(Comp))
				{
					if (UMaterialInterface* Mat = Comp->GetMaterial(0)) return Mat;
				}
			}
		}
		return nullptr;
	}

	FBayRec BayRecFromActor(ABayPropActor* A)
	{
		FBayRec R;
		R.Name = A->BayName;
		R.Label = A->Label;
		R.Group = A->Group;
		R.Type = A->BayType;
		R.Asset = BayAssetPrefix + A->BayType;
		R.PosM = A->PosM;
		R.Yaw = A->Yaw;
		R.Scale = A->Scale;
		const FBayTypeDef* Ty = BayFindType(A->BayType);
		R.SizeM = BayOuterSizeM(Ty ? *Ty : GBayTypes[0], A->Scale);
		R.bHidden = A->IsBayHidden();
		R.Actor = A;
		return R;
	}

	/** 레벨 ISM_Slot 인스턴스 → 레코드. T 는 월드 변환(숨긴 면은 접기 전 원래 변환). */
	FBayRec BayRecFromLevelSlot(AActor* Actor, UInstancedStaticMeshComponent* Comp, int32 Instance, const FTransform& T, bool bHidden, float UU)
	{
		const FVector Extent = Comp->GetStaticMesh()->GetBounds().BoxExtent;
		const FVector S = T.GetScale3D();
		const float HalfX = Extent.X * FMath::Abs(S.X);
		const float HalfY = Extent.Y * FMath::Abs(S.Y);
		const bool bXLong = HalfX >= HalfY;
		// bay 규약은 로컬 X = 폭. 판의 로컬 X 가 길면(길가 평행주차면) 로컬 Y 가 폭이다 — 파이썬 slot_face 의 +90° 정규화와 같다.
		const FVector WidthAxis = (bXLong ? T.GetUnitAxis(EAxis::Y) : T.GetUnitAxis(EAxis::X)).GetSafeNormal2D();
		const float WidthM = 2.f * FMath::Min(HalfX, HalfY) / UU;
		const float LengthM = 2.f * FMath::Max(HalfX, HalfY) / UU;
		const FBayTypeDef& Ty = BayClosestType(WidthM - 2.f * BayLevelLineM, LengthM - 2.f * BayLevelLineM);

		FBayRec R;
		R.Name = BayLevelKey(Actor->GetName(), Instance);
		R.Label = Actor->GetActorNameOrLabel();
		R.Group = Actor->GetName();
		R.Type = Ty.Type;
		R.Asset = BayAssetPrefix + FString(Ty.Type);
		R.PosM = T.GetLocation() / UU;
		R.Yaw = WidthAxis.IsNearlyZero() ? 0.f : BayMod360(FMath::RadiansToDegrees(FMath::Atan2(WidthAxis.Y, WidthAxis.X)));
		R.Scale = FVector::OneVector;
		R.SizeM = FVector2D(WidthM, LengthM);
		R.bHidden = bHidden;
		R.bLevel = true;
		R.LevelActor = Actor->GetName();
		R.LevelInstance = Instance;
		return R;
	}

	/** 씬의 주차면 전부: 프롭 면 → 레벨 면 순. 숨긴 레벨 면은 보관한 원래 변환으로 기하를 낸다(접힌 인스턴스는 크기가 0 이다). */
	void BayCollect(UWorld* World, TMap<FString, FTransform>& HiddenLevel, float UU, TArray<FBayRec>& Out)
	{
		for (TActorIterator<ABayPropActor> It(World); It; ++It)
		{
			if (IsValid(*It)) Out.Add(BayRecFromActor(*It));
		}
		TArray<AActor*> SlotActors;
		BayCollectSlotActors(World, SlotActors);
		for (AActor* Actor : SlotActors)
		{
			TArray<UInstancedStaticMeshComponent*> Comps;
			Actor->GetComponents(Comps);
			for (UInstancedStaticMeshComponent* Comp : Comps)
			{
				if (!BayIsSlotComponent(Comp)) continue;
				for (int32 i = 0; i < Comp->GetInstanceCount(); ++i)
				{
					FTransform T;
					if (!Comp->GetInstanceTransform(i, T, /*bWorldSpace=*/true)) continue;
					const FString Key = BayLevelKey(Actor->GetName(), i);
					const FTransform* Saved = HiddenLevel.Find(Key);
					if (Saved && T.GetScale3D().GetAbsMax() > BayHiddenScale * 10.f)
					{
						// 접혀 있지 않은데 보관 항목이 있다 — 레벨이 바뀌어 같은 이름이 다시 생긴 것. 옛 변환을 버린다.
						HiddenLevel.Remove(Key);
						Saved = nullptr;
					}
					Out.Add(BayRecFromLevelSlot(Actor, Comp, i, Saved ? *Saved : T, Saved != nullptr, UU));
				}
			}
		}
	}

	/** 레벨 면 숨김/복원 — 인스턴스 스케일을 접고 원래 변환을 보관한다. 대상 인스턴스를 못 찾으면 false. */
	bool BaySetLevelSlotHidden(UWorld* World, const FBayRec& R, bool bHidden, TMap<FString, FTransform>& HiddenLevel)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (!IsValid(*It) || It->GetName() != R.LevelActor) continue;
			TArray<UInstancedStaticMeshComponent*> Comps;
			It->GetComponents(Comps);
			for (UInstancedStaticMeshComponent* Comp : Comps)
			{
				if (!BayIsSlotComponent(Comp)) continue;
				if (R.LevelInstance < 0 || R.LevelInstance >= Comp->GetInstanceCount()) continue;
				if (bHidden)
				{
					if (HiddenLevel.Contains(R.Name)) return true; // 이미 접혀 있다
					FTransform T;
					if (!Comp->GetInstanceTransform(R.LevelInstance, T, /*bWorldSpace=*/true)) return false;
					FTransform Collapsed = T;
					Collapsed.SetScale3D(FVector(BayHiddenScale));
					if (!Comp->UpdateInstanceTransform(R.LevelInstance, Collapsed, /*bWorldSpace=*/true, /*bMarkRenderStateDirty=*/true, /*bTeleport=*/true)) return false;
					HiddenLevel.Add(R.Name, T);
				}
				else
				{
					const FTransform* Saved = HiddenLevel.Find(R.Name);
					if (!Saved) return true; // 이미 보인다
					if (!Comp->UpdateInstanceTransform(R.LevelInstance, *Saved, /*bWorldSpace=*/true, /*bMarkRenderStateDirty=*/true, /*bTeleport=*/true)) return false;
					HiddenLevel.Remove(R.Name);
				}
				return true;
			}
		}
		return false;
	}

	/** 면 하나 숨김/복원(프롭·레벨 공용). */
	bool BaySetHidden(UWorld* World, const FBayRec& R, bool bHidden, TMap<FString, FTransform>& HiddenLevel)
	{
		if (R.bLevel) return BaySetLevelSlotHidden(World, R, bHidden, HiddenLevel);
		if (!R.Actor) return false;
		R.Actor->SetBayHidden(bHidden);
		return true;
	}

	// ===== 레벨 면 편집(bay.update/delete/create 의 level: 항목) =====

	/** 레벨 면 R 이 속한 액터와 ISM_Slot. 인스턴스 번호가 범위 밖이면 nullptr. */
	UInstancedStaticMeshComponent* BayLevelComp(UWorld* World, const FString& ActorName, int32 Instance, AActor** OutActor = nullptr)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (!IsValid(*It) || It->GetName() != ActorName) continue;
			UInstancedStaticMeshComponent* Comp = Park3DLevelSlots::FindSlotComponent(*It);
			if (!Comp || (Instance >= 0 && Instance >= Comp->GetInstanceCount())) return nullptr;
			if (OutActor) *OutActor = *It;
			return Comp;
		}
		return nullptr;
	}

	/**
	 * bay 규약(중심 m, 폭 방향 yaw, 크기 m 폭×길이) → 인스턴스 월드 변환.
	 * bXLong: 판의 로컬 X 를 길이로 쓸 것인가 — 그 액터가 이미 쓰는 축 규약을 따른다(LV_Park_03 은 X=길이, LV_Park_01 은 X=폭).
	 * 판이 Plane(100cm) 이 아닐 수도 있으므로 스케일은 메시 바운즈로 환산한다.
	 */
	FTransform BayLevelTransform(const FVector& PosM, float WidthYaw, const FVector2D& SizeM, bool bXLong, float UU, const FVector& MeshExtent)
	{
		const float LenX = bXLong ? SizeM.Y : SizeM.X;
		const float LenY = bXLong ? SizeM.X : SizeM.Y;
		const FVector Scale(
			MeshExtent.X > 0.f ? LenX * UU / (2.f * MeshExtent.X) : 1.f,
			MeshExtent.Y > 0.f ? LenY * UU / (2.f * MeshExtent.Y) : 1.f,
			1.f);
		return FTransform(FRotator(0.f, bXLong ? WidthYaw - 90.f : WidthYaw, 0.f), PosM * UU, Scale);
	}

	bool BayInstanceXLong(const UInstancedStaticMeshComponent* Comp, const FTransform& T)
	{
		const FVector Extent = Comp->GetStaticMesh()->GetBounds().BoxExtent;
		const FVector S = T.GetScale3D();
		return Extent.X * FMath::Abs(S.X) >= Extent.Y * FMath::Abs(S.Y);
	}

	/** 바닥 번호 다시 그리기 — 레벨 면이 바뀌면 번호 목록(CollectSlotNumbers)이 달라진다. 매니저가 없으면(테스트) 건너뛴다. */
	void BayRefreshSlotNumbers(UWorld* World)
	{
		if (AParkingPresetManager* Mgr = Cast<AParkingPresetManager>(UGameplayStatics::GetActorOfClass(World, AParkingPresetManager::StaticClass())))
		{
			Mgr->RebuildSlotNumbers(Mgr->ResolvePresets());
		}
	}

	/** 인스턴스 하나를 지운 뒤 같은 액터의 보관 키(level:<액터>#<n>)를 당겨 쓴다 — ISM 은 지우면 뒤 번호가 한 칸씩 앞으로 온다. */
	void BayShiftHiddenKeys(TMap<FString, FTransform>& HiddenLevel, const FString& ActorName, int32 Removed)
	{
		TMap<FString, FTransform> Next;
		for (const TPair<FString, FTransform>& Kv : HiddenLevel)
		{
			FString Actor; FString Idx;
			if (!Kv.Key.StartsWith(TEXT("level:")) || !Kv.Key.Mid(6).Split(TEXT("#"), &Actor, &Idx) || Actor != ActorName)
			{
				Next.Add(Kv.Key, Kv.Value);
				continue;
			}
			const int32 I = FCString::Atoi(*Idx);
			if (I == Removed) continue;
			Next.Add(I > Removed ? BayLevelKey(ActorName, I - 1) : Kv.Key, Kv.Value);
		}
		HiddenLevel = MoveTemp(Next);
	}

	const FBayRec* BayFindByName(const TArray<FBayRec>& Bays, const FString& Name)
	{
		return Bays.FindByPredicate([&Name](const FBayRec& R) { return R.Name == Name; });
	}

	/** 이름이 비었거나 겹치면 bay_<n> 으로 새로 짓는다(파이썬 env.add_prop 과 같은 규칙). */
	FString BayUniqueName(const FString& Wanted, TSet<FString>& Taken, int32& Serial)
	{
		FString Name = Wanted;
		if (Name.IsEmpty() || Taken.Contains(Name))
		{
			do
			{
				++Serial;
				Name = FString::Printf(TEXT("bay_%d"), Serial);
			}
			while (Taken.Contains(Name));
		}
		Taken.Add(Name);
		return Name;
	}

	bool BayHasSelector(const TSharedPtr<FJsonObject>& P)
	{
		return RpcParam::Has(P, TEXT("name")) || RpcParam::Has(P, TEXT("names")) || RpcParam::Has(P, TEXT("group")) || RpcParam::Has(P, TEXT("all"));
	}

	/** name | names[] | group (| all) 로 고른 이름 집합. 다른 이름도 그대로 담는다(notFound 로 돌려주기 위해). 비면 E 설정 후 false. */
	bool BaySelectNames(const TSharedPtr<FJsonObject>& P, bool bAllowAll, const TArray<FBayRec>& Bays, TSet<FString>& Out, FRpcError& E)
	{
		const FString Single = RpcParam::GetString(P, TEXT("name"));
		if (!Single.IsEmpty()) Out.Add(Single);
		const TArray<TSharedPtr<FJsonValue>>* Names = nullptr;
		if (P.IsValid() && P->TryGetArrayField(TEXT("names"), Names) && Names)
		{
			for (const TSharedPtr<FJsonValue>& V : *Names)
			{
				FString S;
				if (V.IsValid() && V->TryGetString(S) && !S.IsEmpty()) Out.Add(S);
			}
		}
		const FString Group = RpcParam::GetString(P, TEXT("group"));
		if (!Group.IsEmpty())
		{
			for (const FBayRec& R : Bays) { if (R.Group == Group) Out.Add(R.Name); }
		}
		if (bAllowAll && RpcParam::GetBool(P, TEXT("all"), false))
		{
			for (const FBayRec& R : Bays) { Out.Add(R.Name); }
		}
		if (Out.Num() == 0)
		{
			E.FailDomain(bAllowAll ? TEXT("name, names, group 또는 all 이 필요합니다") : TEXT("name, names 또는 group 이 필요합니다"));
			return false;
		}
		return true;
	}

	/** name | names[] | group | all 로 고른 씬 면(아무것도 안 주면 전부), 이름 자연 정렬. 비면 E. */
	bool BayChosen(const TSharedPtr<FJsonObject>& P, const TArray<FBayRec>& Bays, TArray<const FBayRec*>& Out, FRpcError& E)
	{
		TSet<FString> Wanted;
		if (BayHasSelector(P))
		{
			if (!BaySelectNames(P, /*bAllowAll=*/true, Bays, Wanted, E)) return false;
		}
		else
		{
			for (const FBayRec& R : Bays) { Wanted.Add(R.Name); }
		}
		for (const FBayRec& R : Bays)
		{
			if (Wanted.Contains(R.Name)) Out.Add(&R);
		}
		Out.Sort([](const FBayRec& A, const FBayRec& B) { return BayNaturalLess(A.Name, B.Name); });
		if (Out.Num() == 0)
		{
			E.FailDomain(TEXT("변환할 주차면이 없습니다 (bay.list 로 확인)"));
			return false;
		}
		return true;
	}

	/** bay.create 와 같은 키(pos{x,z,y?} yaw? type? name? label? group? scale?) → 레코드(씬에 넣지 않는다). */
	bool BayParseSpec(const TSharedPtr<FJsonObject>& D, const FString& DefaultName, FBayRec& Out, FRpcError& E)
	{
		const FString Type = RpcParam::GetString(D, TEXT("type"), TEXT("normal"));
		const FBayTypeDef* Ty = BayFindType(Type);
		if (!Ty)
		{
			E.FailDomain(FString::Printf(TEXT("허용되지 않은 type: %s (%s)"), *Type, *FString::Join(BaySortedTypes(), TEXT(" | "))));
			return false;
		}
		FVector Pos;
		if (!RpcParam::RequirePosXZ(D, TEXT("pos"), Pos, E)) return false;
		Out.Name = RpcParam::GetString(D, TEXT("name"), DefaultName);
		Out.Type = Type;
		Out.Asset = BayAssetPrefix + Type;
		Out.PosM = Pos;
		Out.Yaw = static_cast<float>(RpcParam::GetFloat(D, TEXT("yaw"), 0.0));
		Out.Scale = RpcParam::GetVec3(D, TEXT("scale"), FVector::OneVector);
		Out.Label = RpcParam::GetString(D, TEXT("label"));
		Out.Group = RpcParam::GetString(D, TEXT("group"));
		Out.SizeM = BayOuterSizeM(*Ty, Out.Scale);
		return true;
	}

	/** 레코드대로 ABayPropActor 스폰. 종류를 모르면(bay.load 의 없는 에셋) normal 크기로 놓는다. */
	ABayPropActor* BaySpawn(UWorld* World, const FBayRec& Spec, UMaterialInterface* Material, float UU)
	{
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ABayPropActor* A = World->SpawnActor<ABayPropActor>(SP);
		if (!A) return nullptr;
		const FBayTypeDef* Ty = BayFindType(Spec.Type);
		const FBayTypeDef& Def = Ty ? *Ty : GBayTypes[0];
		A->BayName = Spec.Name;
		A->BayType = Spec.Type;
		A->Label = Spec.Label;
		A->Group = Spec.Group;
		A->PosM = Spec.PosM;
		A->Yaw = Spec.Yaw;
		A->Scale = Spec.Scale;
		A->PlaneWidthM = Def.WidthM + 2.f * BayLevelLineM;
		A->PlaneLengthM = Def.LengthM + 2.f * BayLevelLineM;
		A->ApplyTransform(UU);
		if (Material && A->Mesh)
		{
			A->Mesh->SetMaterial(0, Material);
		}
		return A;
	}

	TSharedPtr<FJsonObject> BayToDto(const FBayRec& R)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), R.Name);
		O->SetStringField(TEXT("label"), R.Label);
		O->SetStringField(TEXT("group"), R.Group);
		O->SetStringField(TEXT("type"), R.Type);
		O->SetStringField(TEXT("asset"), R.Asset);
		O->SetObjectField(TEXT("pos"), RpcDto::Vec3(R.PosM.X, R.PosM.Y, R.PosM.Z));
		O->SetNumberField(TEXT("yaw"), R.Yaw);
		O->SetObjectField(TEXT("scale"), RpcDto::Vec3(R.Scale.X, R.Scale.Y, R.Scale.Z));
		O->SetObjectField(TEXT("size"), RpcDto::Vec3(R.SizeM.X, R.SizeM.Y, 0.0));
		O->SetBoolField(TEXT("hidden"), R.bHidden);
		// 이 포트만의 가산 키 — 레벨 면(ISM 인스턴스)과 프롭 면(액터)을 구분한다.
		O->SetStringField(TEXT("source"), R.bLevel ? TEXT("level") : TEXT("prop"));
		return O;
	}

	TArray<TSharedPtr<FJsonValue>> BayStringArray(const TArray<FString>& In)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FString& S : In) { Out.Add(MakeShared<FJsonValueString>(S)); }
		return Out;
	}

	TArray<FString> BaySortedArray(const TSet<FString>& In)
	{
		TArray<FString> Out = In.Array();
		Out.Sort();
		return Out;
	}

	// ===== 면 → 프리셋 (envimport._fit_preset · bay.fit_chain · bay.bays_to_presets 포팅) =====

	struct FBayFitFace
	{
		FVector Center;
		float Yaw;
	};

	struct FBayMadePreset
	{
		FParkingPreset Preset;
		FString SlotType;   // normal | disabled — 파이썬 preset_to_dto 의 slotType(응답 전용, FParkingPreset 에는 없다)
	};

	/** 프리셋 P 의 FaceIndex 면 중심(m). ComputeSlotCorners 를 MetersToUU=1 로 불러 미터 그대로 계산한다. */
	FVector BaySlotCenterM(const FParkingPreset& P, int32 FaceIndex)
	{
		FVector C[4];
		AParkingPresetManager::ComputeSlotCorners(P, FaceIndex, /*MetersToUU=*/1.f, /*FaceHeightZ=*/0.f, C);
		return (C[0] + C[1] + C[2] + C[3]) * 0.25f;
	}

	/**
	 * 판 중심 목록 → 프리셋. 면이 폭축(로컬 X, 나란히)으로 늘어서면 useBaseWidth, 길이축(앞뒤)이면 아니다.
	 * faceRot 두 후보(θ±90)와 정렬 방향 두 가지를 ComputeSlotCorners 로 검산해 같은 중심열을 내는 조합을 고른다 —
	 * faceRot 가 180 을 넘으면 걸음이 뒤집히는 언리얼 규약 때문에 해석으로 못 정한다.
	 */
	bool BayFitPreset(const TArray<FBayFitFace>& Faces, float W, float L, int32 Idx, const FString& Name, const FString& SlotType, int32 CamIdx, FBayMadePreset& Out)
	{
		if (Faces.Num() == 0) return false;
		const float Yaw = Faces[0].Yaw;
		const float Rad = FMath::DegreesToRadians(Yaw);
		const FVector2D Axes[2] = {
			FVector2D(FMath::Cos(Rad), FMath::Sin(Rad)),    // 폭축(useBaseWidth)
			FVector2D(-FMath::Sin(Rad), FMath::Cos(Rad)),   // 길이축
		};
		for (int32 a = 0; a < 2; ++a)
		{
			const bool bBaseWidth = (a == 0);
			const FVector2D Ax = Axes[a];
			TArray<FBayFitFace> Ordered = Faces;
			Ordered.StableSort([Ax](const FBayFitFace& A, const FBayFitFace& B)
			{
				return (A.Center.X * Ax.X + A.Center.Y * Ax.Y) < (B.Center.X * Ax.X + B.Center.Y * Ax.Y);
			});
			const float FaceRots[2] = { Yaw - 90.f, Yaw + 90.f };
			for (int32 r = 0; r < 2; ++r)
			{
				for (int32 s = 0; s < 2; ++s)
				{
					TArray<FBayFitFace> Seq = Ordered;
					if (s == 1) Algo::Reverse(Seq);

					FParkingPreset Pr;
					Pr.PresetIdx = Idx;
					Pr.PresetName = Name;
					Pr.FaceCount = Seq.Num();
					Pr.Offset = FVector(BayRound(Seq[0].Center.X, 4), BayRound(Seq[0].Center.Y, 4), BayRound(Seq[0].Center.Z, 4));
					Pr.FaceRotate = static_cast<float>(BayRound(BayMod360(FaceRots[r]), 3));
					Pr.GroupFaceRotate = 0.f;
					Pr.BoxSizeX = W;
					Pr.BoxSizeZ = L;
					Pr.DirType = EFaceDirType::Dir;
					Pr.bIsBaseWidth = bBaseWidth;
					Pr.CameraIdx = CamIdx;

					bool bOk = true;
					for (int32 k = 0; k < Seq.Num() && bOk; ++k)
					{
						bOk = FVector::Dist2D(BaySlotCenterM(Pr, k), Seq[k].Center) < BayFitToleranceM;
					}
					if (bOk)
					{
						Out.Preset = Pr;
						Out.SlotType = SlotType;
						return true;
					}
				}
			}
		}
		return false;
	}

	/**
	 * 같은 group·종류·yaw 의 면 묶음 → 프리셋 하나. 걸음(피치)은 첫 면과 가장 가까운 면의 간격이고, 그 간격이 폭축·길이축
	 * 중 어느 쪽에 놓였는지로 useBaseWidth 를 정한다. 검산에 걸리면 false.
	 */
	bool BayFitChain(const TArray<const FBayRec*>& Members, const FVector2D& SizeWL, int32 Idx, const FString& Name, int32 CamIdx, FBayMadePreset& Out)
	{
		float W = SizeWL.X;
		float L = SizeWL.Y;
		if (Members.Num() > 1)
		{
			const FBayRec* P0 = Members[0];
			const FBayRec* Near = nullptr;
			double Best = TNumericLimits<double>::Max();
			for (int32 i = 1; i < Members.Num(); ++i)
			{
				const double D = FVector::Dist2D(Members[i]->PosM, P0->PosM);
				if (D < Best) { Best = D; Near = Members[i]; }
			}
			const double DX = Near->PosM.X - P0->PosM.X;
			const double DY = Near->PosM.Y - P0->PosM.Y;
			const double YawRad = FMath::DegreesToRadians(static_cast<double>(P0->Yaw));
			const double AlongW = FMath::Abs(DX * FMath::Cos(YawRad) + DY * FMath::Sin(YawRad));
			const double Hyp = FMath::Sqrt(DX * DX + DY * DY);
			const float Pitch = static_cast<float>(BayRound(Hyp, 3)); // mm — 좌표는 4자리라 5.9999 같은 값이 나온다
			if (AlongW >= Hyp * 0.5) W = Pitch; else L = Pitch;
		}
		TArray<FBayFitFace> Faces;
		for (const FBayRec* M : Members) { Faces.Add({ M->PosM, M->Yaw }); }
		const FString SlotType = Members[0]->Type == TEXT("disabled") ? TEXT("disabled") : TEXT("normal");
		return BayFitPreset(Faces, W, L, Idx, Name, SlotType, CamIdx, Out);
	}

	/**
	 * 면 목록 → (프리셋 목록, 등간격이 아니라 면마다 쪼갠 묶음 키). 같은 group·종류·yaw 가 한 묶음, idx 는 StartIdx 부터 순번.
	 * bay.toPresets(메모리에 넣는다) 와 bay.exportPresets(파일만 쓴다) 가 같이 쓴다.
	 */
	void BaysToPresets(const TArray<const FBayRec*>& Chosen, int32 CamIdx, int32 StartIdx, TArray<FBayMadePreset>& Made, TArray<FString>& Split)
	{
		struct FChain { FString Key; FString GroupKey; TArray<const FBayRec*> Members; };
		TArray<FChain> Chains;
		for (const FBayRec* R : Chosen)
		{
			const FString GroupKey = R->Group.IsEmpty() ? R->Name : R->Group;
			const FString Key = FString::Printf(TEXT("%s|%s|%.1f"), *GroupKey, *R->Type, BayMod360(R->Yaw));
			FChain* C = Chains.FindByPredicate([&Key](const FChain& X) { return X.Key == Key; });
			if (!C)
			{
				FChain NewChain;
				NewChain.Key = Key;
				NewChain.GroupKey = GroupKey;
				Chains.Add(NewChain);
				C = &Chains.Last();
			}
			C->Members.Add(R);
		}
		int32 Idx = StartIdx;
		for (const FChain& C : Chains)
		{
			const FVector2D SizeWL = C.Members[0]->PresetSize();
			const FString Name = C.Members[0]->Label.IsEmpty() ? C.GroupKey : C.Members[0]->Label;
			FBayMadePreset Pr;
			if (BayFitChain(C.Members, SizeWL, Idx, Name, CamIdx, Pr))
			{
				Made.Add(Pr);
				++Idx;
			}
			else
			{
				// 등간격이 아니다 — 면마다 하나(한 면은 항상 맞는다).
				Split.Add(C.GroupKey);
				for (const FBayRec* M : C.Members)
				{
					TArray<const FBayRec*> Single;
					Single.Add(M);
					FBayMadePreset One;
					if (BayFitChain(Single, SizeWL, Idx, Name, CamIdx, One)) Made.Add(One);
					++Idx;
				}
			}
		}
	}

	TSharedPtr<FJsonValue> BayMadeToDto(const FBayMadePreset& M)
	{
		TSharedPtr<FJsonObject> O = RpcDto::PresetToDto(M.Preset);
		O->SetStringField(TEXT("slotType"), M.SlotType);
		return MakeShared<FJsonValueObject>(O);
	}

	// ===== 파일 =====

	/** bay.save/load: fullPath > Save/3D/Bay/<fileName 기본 Bays>.json. */
	FString BayResolveFilePath(const TSharedPtr<FJsonObject>& P)
	{
		const FString Full = RpcParam::GetString(P, TEXT("fullPath"));
		if (!Full.IsEmpty()) return Full;
		FString FileName = RpcParam::GetString(P, TEXT("fileName"), TEXT("Bays"));
		if (!FileName.EndsWith(TEXT(".json"))) FileName += TEXT(".json");
		return Park3DDataPaths::GetDataFilePath(TEXT("Bay"), *FileName);
	}

	/** bay.saveLevel/loadLevel: fullPath > Save/3D/Bay/<fileName 기본 LevelSlots>.json (config slot_file 과 같은 폴더). */
	FString BayResolveLevelFilePath(const TSharedPtr<FJsonObject>& P)
	{
		const FString Full = RpcParam::GetString(P, TEXT("fullPath"));
		if (!Full.IsEmpty()) return Full;
		FString FileName = RpcParam::GetString(P, TEXT("fileName"), TEXT("LevelSlots"));
		if (!FileName.EndsWith(TEXT(".json"))) FileName += TEXT(".json");
		return Park3DDataPaths::GetDataFilePath(TEXT("Bay"), *FileName);
	}

	/** bay.exportPresets: fullPath > Save/3D/Preset/<fileName>.json — 하나는 필수(기본 이름으로 조용히 덮어쓰지 않는다). */
	bool BayResolvePresetWritePath(const TSharedPtr<FJsonObject>& P, FString& Out, FRpcError& E)
	{
		const FString Full = RpcParam::GetString(P, TEXT("fullPath"));
		if (!Full.IsEmpty()) { Out = Full; return true; }
		FString Name;
		if (!RpcParam::RequireString(P, TEXT("fileName"), Name, E)) return false;
		Name.TrimStartAndEndInline();
		if (Name.IsEmpty() || Name.Contains(TEXT("/")) || Name.Contains(TEXT("\\")) || Name.StartsWith(TEXT(".")))
		{
			E.FailDomain(FString::Printf(TEXT("fileName 이 잘못됐다 — 파일 이름만(경로·'.' 시작 불가): '%s'"), *Name));
			return false;
		}
		if (!Name.EndsWith(TEXT(".json"))) Name += TEXT(".json");
		Out = Park3DDataPaths::GetDataFilePath(TEXT("Preset"), *Name);
		return true;
	}

	bool BayWriteJson(const FString& Path, const TSharedRef<FJsonObject>& Root)
	{
		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Out);
		if (!FJsonSerializer::Serialize(Root, Writer)) return false;
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), /*Tree=*/true);
		return FFileHelper::SaveStringToFile(Out, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	TSharedPtr<FJsonObject> BayReadJson(const FString& Path)
	{
		FString Json;
		if (!FFileHelper::LoadFileToString(Json, *Path)) return nullptr;
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return nullptr;
		return Root;
	}

	/** 프롭 면 → 파일 항목(OmiPark3D EnvProp.to_json 과 같은 키). hidden 은 런타임 상태라 쓰지 않는다. */
	TSharedPtr<FJsonValue> BayToFileEntry(const FBayRec& R)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), R.Name);
		O->SetStringField(TEXT("asset"), R.Asset);
		O->SetStringField(TEXT("label"), R.Label);
		O->SetStringField(TEXT("group"), R.Group);
		O->SetObjectField(TEXT("pos"), RpcDto::Vec3(R.PosM.X, R.PosM.Y, R.PosM.Z));
		TSharedPtr<FJsonObject> Rot = MakeShared<FJsonObject>();
		Rot->SetNumberField(TEXT("roll"), 0.0);
		Rot->SetNumberField(TEXT("pitch"), 0.0);
		Rot->SetNumberField(TEXT("yaw"), R.Yaw);
		O->SetObjectField(TEXT("rot"), Rot);
		O->SetObjectField(TEXT("scale"), RpcDto::Vec3(R.Scale.X, R.Scale.Y, R.Scale.Z));
		return MakeShared<FJsonValueObject>(O);
	}
}

void FBayRpcModule::Register(URpcDispatcher& Dispatcher)
{
	auto NeedWorld = [this](FRpcError& E) -> UWorld*
	{
		UWorld* W = GetWorldPtr();
		if (!W) E.FailDomain(TEXT("월드 없음(맵 미로드)"));
		return W;
	};
	auto Collect = [this](UWorld* W, TArray<FBayRec>& Out)
	{
		BayCollect(W, HiddenLevelSlots, BayMetersToUU(W), Out);
	};

	// bay.list {nameLike? type?} → {bays[], count, hiddenCount, types[]}
	Dispatcher.Register(TEXT("bay.list"), [NeedWorld, Collect](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* W = NeedWorld(E); if (!W) return nullptr;
		TArray<FBayRec> Bays; Collect(W, Bays);
		const FString Like = RpcParam::GetString(P, TEXT("nameLike"));
		const FString Type = RpcParam::GetString(P, TEXT("type"));

		TArray<TSharedPtr<FJsonValue>> Arr;
		int32 HiddenCount = 0;
		for (const FBayRec& R : Bays)
		{
			// 파이썬(`like in name`)과 같이 대소문자를 구분한다.
			if (!Like.IsEmpty() && !(R.Name.Contains(Like, ESearchCase::CaseSensitive) || R.Label.Contains(Like, ESearchCase::CaseSensitive) || R.Group.Contains(Like, ESearchCase::CaseSensitive))) continue;
			if (!Type.IsEmpty() && R.Type != Type) continue;
			Arr.Add(MakeShared<FJsonValueObject>(BayToDto(R)));
			if (R.bHidden) ++HiddenCount;
		}
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetArrayField(TEXT("bays"), Arr);
		Root->SetNumberField(TEXT("count"), Arr.Num());
		Root->SetNumberField(TEXT("hiddenCount"), HiddenCount);
		Root->SetArrayField(TEXT("types"), BayStringArray(BaySortedTypes()));
		return RpcDto::MakeObject(Root);
	});

	// bay.create {pos{x,z,y?} yaw?=0 type?=normal name? label? group? scale?} → bay DTO
	// group 이 레벨 BP_ParkingSlot 액터 이름이면 프롭이 아니라 그 액터의 ISM_Slot 에 인스턴스를 넣는다(레벨 면 — 번호·스냅 대상).
	Dispatcher.Register(TEXT("bay.create"), [this, NeedWorld, Collect](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* W = NeedWorld(E); if (!W) return nullptr;
		FBayRec Spec;
		if (!BayParseSpec(P, FString(), Spec, E)) return nullptr;
		if (AActor* LevelActor = nullptr; !Spec.Group.IsEmpty() && BayLevelComp(W, Spec.Group, -1, &LevelActor))
		{
			UInstancedStaticMeshComponent* Comp = Park3DLevelSlots::FindSlotComponent(LevelActor);
			const float UU = BayMetersToUU(W);
			// 축 규약은 그 액터의 첫 인스턴스를 따른다(없으면 bay 규약대로 X=폭).
			FTransform First;
			const bool bXLong = Comp->GetInstanceCount() > 0 && Comp->GetInstanceTransform(0, First, true) && BayInstanceXLong(Comp, First);
			const FTransform T = BayLevelTransform(Spec.PosM, Spec.Yaw, Spec.SizeM, bXLong, UU, Comp->GetStaticMesh()->GetBounds().BoxExtent);
			const int32 Idx = Comp->AddInstance(T, /*bWorldSpace=*/true);
			Comp->MarkRenderStateDirty();
			BayRefreshSlotNumbers(W);
			return RpcDto::MakeObject(BayToDto(BayRecFromLevelSlot(LevelActor, Comp, Idx, T, false, UU)));
		}
		TArray<FBayRec> Bays; Collect(W, Bays);
		TSet<FString> Taken;
		for (const FBayRec& R : Bays) { Taken.Add(R.Name); }
		Spec.Name = BayUniqueName(Spec.Name, Taken, NextSerial);

		ABayPropActor* A = BaySpawn(W, Spec, BayFindLevelSlotMaterial(W), BayMetersToUU(W));
		if (!A) { E.FailDomain(TEXT("주차면 액터를 생성할 수 없습니다")); return nullptr; }
		return RpcDto::MakeObject(BayToDto(BayRecFromActor(A)));
	});

	// bay.update {name pos? | delta? yaw? type? scale? label? size?} → bay DTO.
	// 레벨 면(level:…)은 pos/delta/yaw/type/size{x=폭,y=길이 m} 로 인스턴스 변환을 바꾼다(scale·label 은 프롭 면 전용).
	Dispatcher.Register(TEXT("bay.update"), [this, NeedWorld, Collect](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* W = NeedWorld(E); if (!W) return nullptr;
		FString Name;
		if (!RpcParam::RequireString(P, TEXT("name"), Name, E)) return nullptr;
		TArray<FBayRec> Bays; Collect(W, Bays);
		const FBayRec* R = BayFindByName(Bays, Name);
		if (!R) { E.FailDomain(FString::Printf(TEXT("주차면 없음: %s (bay.list 로 확인)"), *Name)); return nullptr; }
		if (R->bLevel)
		{
			if (HiddenLevelSlots.Contains(R->Name))
			{
				E.FailDomain(FString::Printf(TEXT("숨긴 레벨 주차면은 수정할 수 없습니다: %s (bay.hide hidden:false 뒤에)"), *Name));
				return nullptr;
			}
			AActor* LevelActor = nullptr;
			UInstancedStaticMeshComponent* Comp = BayLevelComp(W, R->LevelActor, R->LevelInstance, &LevelActor);
			if (!Comp) { E.FailDomain(FString::Printf(TEXT("레벨 주차면 인스턴스를 찾지 못했습니다: %s"), *Name)); return nullptr; }
			FTransform Cur;
			Comp->GetInstanceTransform(R->LevelInstance, Cur, /*bWorldSpace=*/true);

			FVector PosM = R->PosM;
			if (RpcParam::Has(P, TEXT("pos")))   PosM = RpcParam::GetVec3(P, TEXT("pos"), PosM);
			if (RpcParam::Has(P, TEXT("delta"))) PosM += RpcParam::GetVec3(P, TEXT("delta"));
			const float Yaw = static_cast<float>(RpcParam::GetFloat(P, TEXT("yaw"), R->Yaw));
			FVector2D SizeM = R->SizeM;
			if (RpcParam::Has(P, TEXT("type")))
			{
				const FString Type = RpcParam::GetString(P, TEXT("type"));
				const FBayTypeDef* Ty = BayFindType(Type);
				if (!Ty)
				{
					E.FailDomain(FString::Printf(TEXT("허용되지 않은 type: %s (%s)"), *Type, *FString::Join(BaySortedTypes(), TEXT(" | "))));
					return nullptr;
				}
				SizeM = BayOuterSizeM(*Ty, FVector::OneVector);
			}
			if (RpcParam::Has(P, TEXT("size")))
			{
				const FVector S = RpcParam::GetVec3(P, TEXT("size"), FVector(SizeM.X, SizeM.Y, 0.f));
				SizeM = FVector2D(S.X, S.Y);
			}
			if (SizeM.X <= 0.f || SizeM.Y <= 0.f) { E.FailDomain(TEXT("size 는 양수여야 합니다")); return nullptr; }

			const float UU = BayMetersToUU(W);
			const FTransform T = BayLevelTransform(PosM, Yaw, SizeM, BayInstanceXLong(Comp, Cur), UU, Comp->GetStaticMesh()->GetBounds().BoxExtent);
			if (!Comp->UpdateInstanceTransform(R->LevelInstance, T, /*bWorldSpace=*/true, /*bMarkRenderStateDirty=*/true, /*bTeleport=*/true))
			{
				E.FailDomain(FString::Printf(TEXT("레벨 주차면 변환 실패: %s"), *Name));
				return nullptr;
			}
			BayRefreshSlotNumbers(W);
			return RpcDto::MakeObject(BayToDto(BayRecFromLevelSlot(LevelActor, Comp, R->LevelInstance, T, false, UU)));
		}
		if (!R->Actor)
		{
			E.FailDomain(FString::Printf(TEXT("주차면 액터 없음: %s"), *Name));
			return nullptr;
		}
		ABayPropActor* A = R->Actor;
		if (RpcParam::Has(P, TEXT("pos")))   A->PosM = RpcParam::GetVec3(P, TEXT("pos"), A->PosM);
		if (RpcParam::Has(P, TEXT("delta"))) A->PosM += RpcParam::GetVec3(P, TEXT("delta"));
		if (RpcParam::Has(P, TEXT("yaw")))   A->Yaw = static_cast<float>(RpcParam::GetFloat(P, TEXT("yaw"), A->Yaw));
		if (RpcParam::Has(P, TEXT("type")))
		{
			const FString Type = RpcParam::GetString(P, TEXT("type"));
			const FBayTypeDef* Ty = BayFindType(Type);
			if (!Ty)
			{
				E.FailDomain(FString::Printf(TEXT("허용되지 않은 type: %s (%s)"), *Type, *FString::Join(BaySortedTypes(), TEXT(" | "))));
				return nullptr;
			}
			A->BayType = Type;
			A->PlaneWidthM = Ty->WidthM + 2.f * BayLevelLineM;
			A->PlaneLengthM = Ty->LengthM + 2.f * BayLevelLineM;
		}
		if (RpcParam::Has(P, TEXT("scale"))) A->Scale = RpcParam::GetVec3(P, TEXT("scale"), A->Scale);
		if (RpcParam::Has(P, TEXT("label"))) A->Label = RpcParam::GetString(P, TEXT("label"), A->Label);
		A->ApplyTransform(BayMetersToUU(W));
		return RpcDto::MakeObject(BayToDto(BayRecFromActor(A)));
	});

	// bay.delete {name | names[] | group} → {deleted[], deletedCount, notFound[], levelSkipped[]}
	// 레벨 면도 지운다(ISM 인스턴스 제거). ⚠ 같은 액터의 뒤 인스턴스 이름이 한 칸씩 앞으로 온다(level:<액터>#<n-1>) — bay.list 로 다시 볼 것.
	// levelSkipped 는 옛 응답 호환용으로 남겨 두며 항상 비어 있다.
	Dispatcher.Register(TEXT("bay.delete"), [this, NeedWorld, Collect](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* W = NeedWorld(E); if (!W) return nullptr;
		TArray<FBayRec> Bays; Collect(W, Bays);
		TSet<FString> Wanted;
		if (!BaySelectNames(P, /*bAllowAll=*/false, Bays, Wanted, E)) return nullptr;

		TSet<FString> Deleted, LevelSkipped;
		TMap<FString, TArray<int32>> LevelRemove; // 액터 → 인스턴스 번호(내림차순으로 지워야 앞 번호가 안 밀린다)
		for (const FBayRec& R : Bays)
		{
			if (!Wanted.Contains(R.Name)) continue;
			if (R.bLevel) { LevelRemove.FindOrAdd(R.LevelActor).Add(R.LevelInstance); continue; }
			if (R.Actor) { R.Actor->Destroy(); Deleted.Add(R.Name); }
		}
		for (TPair<FString, TArray<int32>>& Kv : LevelRemove)
		{
			Kv.Value.Sort([](int32 A, int32 B) { return A > B; });
			for (const int32 Idx : Kv.Value)
			{
				UInstancedStaticMeshComponent* Comp = BayLevelComp(W, Kv.Key, Idx);
				if (Comp && Comp->RemoveInstance(Idx))
				{
					Deleted.Add(BayLevelKey(Kv.Key, Idx));
					BayShiftHiddenKeys(HiddenLevelSlots, Kv.Key, Idx);
				}
			}
		}
		if (LevelRemove.Num() > 0) BayRefreshSlotNumbers(W);
		TArray<FString> NotFound;
		for (const FString& N : BaySortedArray(Wanted))
		{
			if (!Deleted.Contains(N) && !LevelSkipped.Contains(N)) NotFound.Add(N);
		}
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetArrayField(TEXT("deleted"), BayStringArray(BaySortedArray(Deleted)));
		Root->SetNumberField(TEXT("deletedCount"), Deleted.Num());
		Root->SetArrayField(TEXT("notFound"), BayStringArray(NotFound));
		Root->SetArrayField(TEXT("levelSkipped"), BayStringArray(BaySortedArray(LevelSkipped)));
		return RpcDto::MakeObject(Root);
	});

	// bay.clear → {deletedCount}. 프롭 면 전부 삭제(레벨 면은 남는다).
	Dispatcher.Register(TEXT("bay.clear"), [NeedWorld, Collect](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* W = NeedWorld(E); if (!W) return nullptr;
		TArray<FBayRec> Bays; Collect(W, Bays);
		int32 Removed = 0;
		for (const FBayRec& R : Bays)
		{
			if (!R.bLevel && R.Actor) { R.Actor->Destroy(); ++Removed; }
		}
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetNumberField(TEXT("deletedCount"), Removed);
		return RpcDto::MakeObject(Root);
	});

	// bay.hide {name | names[] | group | all, hidden?=true} → {hidden, changed[], notFound[]}. 런타임 상태 — 파일에 남지 않는다.
	Dispatcher.Register(TEXT("bay.hide"), [this, NeedWorld, Collect](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* W = NeedWorld(E); if (!W) return nullptr;
		TArray<FBayRec> Bays; Collect(W, Bays);
		TSet<FString> Wanted;
		if (!BaySelectNames(P, /*bAllowAll=*/true, Bays, Wanted, E)) return nullptr;
		const bool bHidden = RpcParam::GetBool(P, TEXT("hidden"), true);

		TSet<FString> Changed;
		for (const FBayRec& R : Bays)
		{
			if (Wanted.Contains(R.Name) && BaySetHidden(W, R, bHidden, HiddenLevelSlots)) Changed.Add(R.Name);
		}
		TArray<FString> NotFound;
		for (const FString& N : BaySortedArray(Wanted))
		{
			if (!BayFindByName(Bays, N)) NotFound.Add(N);
		}
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("hidden"), bHidden);
		Root->SetArrayField(TEXT("changed"), BayStringArray(BaySortedArray(Changed)));
		Root->SetArrayField(TEXT("notFound"), BayStringArray(NotFound));
		return RpcDto::MakeObject(Root);
	});

	// bay.hideAll {hidden?=true} / bay.showAll {} → {ok, hidden, changedCount, bayCount, hiddenCount}. 프리셋 리스트는 그대로.
	auto HideAll = [this, NeedWorld, Collect](bool bHidden, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* W = NeedWorld(E); if (!W) return nullptr;
		TArray<FBayRec> Bays; Collect(W, Bays);
		int32 ChangedCount = 0;
		for (const FBayRec& R : Bays)
		{
			if (R.bHidden == bHidden) continue; // 이미 그 상태인 면은 changedCount 에서 뺀다
			if (BaySetHidden(W, R, bHidden, HiddenLevelSlots)) ++ChangedCount;
		}
		TArray<FBayRec> After; Collect(W, After);
		int32 HiddenCount = 0;
		for (const FBayRec& R : After) { if (R.bHidden) ++HiddenCount; }

		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("ok"), true);
		Root->SetBoolField(TEXT("hidden"), bHidden);
		Root->SetNumberField(TEXT("changedCount"), ChangedCount);
		Root->SetNumberField(TEXT("bayCount"), After.Num());
		Root->SetNumberField(TEXT("hiddenCount"), HiddenCount);
		return RpcDto::MakeObject(Root);
	};
	Dispatcher.Register(TEXT("bay.hideAll"), [HideAll](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		return HideAll(RpcParam::GetBool(P, TEXT("hidden"), true), E);
	});
	Dispatcher.Register(TEXT("bay.showAll"), [HideAll](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		return HideAll(false, E);
	});

	// bay.save {fullPath? | fileName?=Bays} → {ok, path, fileName, count}. 프롭 면만 envProps 형식으로 쓴다.
	Dispatcher.Register(TEXT("bay.save"), [NeedWorld, Collect](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* W = NeedWorld(E); if (!W) return nullptr;
		TArray<FBayRec> Bays; Collect(W, Bays);
		const FString Path = BayResolveFilePath(P);

		TArray<TSharedPtr<FJsonValue>> Props;
		for (const FBayRec& R : Bays)
		{
			if (!R.bLevel) Props.Add(BayToFileEntry(R));
		}
		// 루트에 datas 를 두지 않는다 — 차량·프리셋 로더가 이 파일을 자기 것으로 오인하지 않게(파이썬 save_env_props 와 같다).
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("isUnreal"), true);
		Root->SetStringField(TEXT("kind"), TEXT("envProps"));
		Root->SetBoolField(TEXT("bays"), true);
		Root->SetArrayField(TEXT("props"), Props);
		if (!BayWriteJson(Path, Root))
		{
			E.FailDomain(FString::Printf(TEXT("주차면 저장 실패: %s"), *Path));
			return nullptr;
		}
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetStringField(TEXT("path"), Path);
		O->SetStringField(TEXT("fileName"), FPaths::GetCleanFilename(Path));
		O->SetNumberField(TEXT("count"), Props.Num());
		return RpcDto::MakeObject(O);
	});

	// bay.load {fullPath? | fileName?=Bays} → {ok, count, replaced, skipped, missingAssets[], fileName}.
	// 파일의 주차면(bay_ 에셋 항목)만 골라 현재 프롭 면과 바꿔 끼운다. 레벨 면·다른 프롭은 그대로.
	Dispatcher.Register(TEXT("bay.load"), [this, NeedWorld, Collect](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* W = NeedWorld(E); if (!W) return nullptr;
		const FString Path = BayResolveFilePath(P);
		const TSharedPtr<FJsonObject> Doc = BayReadJson(Path);
		const TArray<TSharedPtr<FJsonValue>>* Props = nullptr;
		if (!Doc.IsValid() || RpcParam::GetString(Doc, TEXT("kind")) != TEXT("envProps") || !Doc->TryGetArrayField(TEXT("props"), Props) || !Props)
		{
			E.FailDomain(FString::Printf(TEXT("주차면 로드 실패: %s"), *Path));
			return nullptr;
		}

		int32 Loaded = 0;
		TArray<FBayRec> Incoming;
		TSet<FString> Missing;
		for (const TSharedPtr<FJsonValue>& V : *Props)
		{
			if (!V.IsValid() || V->Type != EJson::Object) continue;
			const TSharedPtr<FJsonObject> D = V->AsObject();
			const FString Name = RpcParam::GetString(D, TEXT("name"));
			const FString Asset = RpcParam::GetString(D, TEXT("asset"));
			if (Name.IsEmpty() || Asset.IsEmpty()) continue; // 파이썬 EnvProp.from_json 이 버리는 항목
			++Loaded;
			if (!Asset.StartsWith(BayAssetPrefix)) continue; // 주차면이 아닌 프롭 → skipped

			FBayRec R;
			R.Name = Name;
			R.Asset = Asset;
			R.Type = Asset.Mid(FCString::Strlen(BayAssetPrefix));
			R.PosM = RpcParam::GetVec3(D, TEXT("pos"));
			const TSharedPtr<FJsonObject>* Rot = nullptr;
			if (D->TryGetObjectField(TEXT("rot"), Rot) && Rot && Rot->IsValid())
			{
				R.Yaw = static_cast<float>(RpcParam::GetFloat(*Rot, TEXT("yaw"), 0.0));
			}
			R.Scale = RpcParam::GetVec3(D, TEXT("scale"), FVector::OneVector);
			R.Label = RpcParam::GetString(D, TEXT("label"));
			R.Group = RpcParam::GetString(D, TEXT("group"));
			if (!BayFindType(R.Type)) Missing.Add(Asset); // 모르는 에셋 — 파이썬처럼 항목은 남기되(normal 크기) 이름을 보고한다
			Incoming.Add(R);
		}

		TArray<FBayRec> Bays; Collect(W, Bays);
		int32 Replaced = 0;
		TSet<FString> Taken;
		for (const FBayRec& R : Bays)
		{
			if (R.bLevel) { Taken.Add(R.Name); continue; }
			if (R.Actor) { R.Actor->Destroy(); ++Replaced; }
		}
		UMaterialInterface* Mat = BayFindLevelSlotMaterial(W);
		const float UU = BayMetersToUU(W);
		int32 Count = 0;
		for (FBayRec& R : Incoming)
		{
			R.Name = BayUniqueName(R.Name, Taken, NextSerial);
			if (BaySpawn(W, R, Mat, UU)) ++Count;
		}

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("count"), Count);
		O->SetNumberField(TEXT("replaced"), Replaced);
		O->SetNumberField(TEXT("skipped"), Loaded - Incoming.Num());
		O->SetArrayField(TEXT("missingAssets"), BayStringArray(BaySortedArray(Missing)));
		O->SetStringField(TEXT("fileName"), FPaths::GetCleanFilename(Path));
		return RpcDto::MakeObject(O);
	});

	// bay.saveLevel {fullPath? | fileName?=LevelSlots} → {ok, path, fileName, count, level}.
	// 레벨 면(BP_ParkingSlot ISM_Slot) 전부를 스냅샷으로 쓴다 — config `slot_file` 에 걸면 기동 때 그대로 복원된다.
	// 숨긴 면은 접기 전 원래 변환으로 쓴다(숨김은 런타임 상태). 프롭 면은 bay.save 의 몫.
	Dispatcher.Register(TEXT("bay.saveLevel"), [this, NeedWorld](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* W = NeedWorld(E); if (!W) return nullptr;
		const FString Path = BayResolveLevelFilePath(P);
		const FString Level = UPark3DAppConfigLibrary::GetCurrentLevelPath(W);
		TArray<Park3DLevelSlots::FSlotInstance> Slots;
		Park3DLevelSlots::Snapshot(W, HiddenLevelSlots, Slots);
		if (!Park3DLevelSlots::SaveFile(Path, Level, Slots))
		{
			E.FailDomain(FString::Printf(TEXT("레벨 주차면 저장 실패: %s"), *Path));
			return nullptr;
		}
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetStringField(TEXT("path"), Path);
		O->SetStringField(TEXT("fileName"), FPaths::GetCleanFilename(Path));
		O->SetNumberField(TEXT("count"), Slots.Num());
		O->SetStringField(TEXT("level"), Level);
		return RpcDto::MakeObject(O);
	});

	// bay.loadLevel {fullPath? | fileName?=LevelSlots} → {ok, count, missingActors[], fileName, level}.
	// 파일에 나온 액터의 인스턴스를 전부 파일대로 바꿔 끼운다(파일에 없는 액터는 그대로). 그 액터의 숨김 보관은 버린다.
	Dispatcher.Register(TEXT("bay.loadLevel"), [this, NeedWorld](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* W = NeedWorld(E); if (!W) return nullptr;
		const FString Path = BayResolveLevelFilePath(P);
		TArray<Park3DLevelSlots::FSlotInstance> Slots;
		FString FileLevel;
		if (!Park3DLevelSlots::LoadFile(Path, Slots, FileLevel))
		{
			E.FailDomain(FString::Printf(TEXT("레벨 주차면 로드 실패: %s (kind=levelSlots 파일이어야 한다)"), *Path));
			return nullptr;
		}
		TSet<FString> Touched;
		for (const Park3DLevelSlots::FSlotInstance& S : Slots) { Touched.Add(S.ActorName); }
		for (auto It = HiddenLevelSlots.CreateIterator(); It; ++It)
		{
			FString Actor, Idx;
			if (It.Key().Mid(6).Split(TEXT("#"), &Actor, &Idx) && Touched.Contains(Actor)) It.RemoveCurrent();
		}
		TArray<FString> Missing;
		const int32 Count = Park3DLevelSlots::Apply(W, Slots, Missing);
		BayRefreshSlotNumbers(W);

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("count"), Count);
		O->SetArrayField(TEXT("missingActors"), BayStringArray(Missing));
		O->SetStringField(TEXT("fileName"), FPaths::GetCleanFilename(Path));
		O->SetStringField(TEXT("level"), FileLevel);
		return RpcDto::MakeObject(O);
	});

	// bay.fromPresets {clearPresets?=true} → {count, bays[], clearedPresets}.
	// 메모리의 프리셋(preset.load 로 읽은 파일 등) → 면마다 프롭 하나. 프리셋 크기가 에셋 내측과 다르면 scale 로 맞춘다.
	Dispatcher.Register(TEXT("bay.fromPresets"), [this, NeedWorld, Collect](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* W = NeedWorld(E); if (!W) return nullptr;
		AParkingPresetManager* Mgr = GetPresetManager(E); if (!Mgr) return nullptr;
		// ResolvePresets 는 StoredPresets 가 비면 config 의 preset_file 을 읽는다(preset.numbers 와 같은 규약). 뒤에서 비우므로 복사해 둔다.
		const TArray<FParkingPreset> Source = Mgr->ResolvePresets();

		TArray<FBayRec> Bays; Collect(W, Bays);
		TSet<FString> Taken;
		for (const FBayRec& R : Bays) { Taken.Add(R.Name); }
		UMaterialInterface* Mat = BayFindLevelSlotMaterial(W);
		const float UU = BayMetersToUU(W);
		// FParkingPreset 에는 slotType 이 없다 — 전부 normal(파이썬은 pr.slot_type 으로 disabled 를 고른다).
		const FBayTypeDef& Ty = GBayTypes[0];

		TArray<FString> Made;
		for (const FParkingPreset& Pr : Source)
		{
			for (int32 k = 0; k < Pr.FaceCount; ++k)
			{
				FVector C[4];
				AParkingPresetManager::ComputeSlotCorners(Pr, k, /*MetersToUU=*/1.f, /*FaceHeightZ=*/0.f, C); // [0]→[3] 이 폭(xSize) 변
				const double CX = (C[0].X + C[1].X + C[2].X + C[3].X) * 0.25;
				const double CY = (C[0].Y + C[1].Y + C[2].Y + C[3].Y) * 0.25;
				const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(C[3].Y - C[0].Y, C[3].X - C[0].X));

				FBayRec S;
				S.Name = FString::Printf(TEXT("p%d_f%d"), Pr.PresetIdx, k + 1);
				S.Type = Ty.Type;
				S.Asset = BayAssetPrefix + FString(Ty.Type);
				S.PosM = FVector(BayRound(CX, 4), BayRound(CY, 4), Pr.Offset.Z);
				S.Yaw = static_cast<float>(BayRound(Yaw, 3));
				S.Scale = FVector(BayRound(Pr.BoxSizeX / Ty.PresetWidthM, 4), BayRound(Pr.BoxSizeZ / Ty.PresetLengthM, 4), 1.0);
				S.Label = Pr.PresetName;
				S.Group = FString::Printf(TEXT("preset_%d"), Pr.PresetIdx);
				S.Name = BayUniqueName(S.Name, Taken, NextSerial);
				if (BaySpawn(W, S, Mat, UU)) Made.Add(S.Name);
			}
		}
		int32 Cleared = 0;
		if (RpcParam::GetBool(P, TEXT("clearPresets"), true))
		{
			Cleared = Mgr->StoredPresets.Num();
			Mgr->ClearPresets(); // RefreshView 포함 — 선이 두 겹으로 그려지지 않게
		}
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("count"), Made.Num());
		O->SetArrayField(TEXT("bays"), BayStringArray(Made));
		O->SetNumberField(TEXT("clearedPresets"), Cleared);
		return RpcDto::MakeObject(O);
	});

	// bay.toPresets {name? | names[]? | group? | all?(기본 전부) camIdx?=1 clearBays?=true}
	//   → {count, presets[], bayCount, bays[], split[], clearedBays}. bay.fromPresets 의 역.
	// 같은 group·종류·yaw 의 등간격 면이 프리셋 하나(아니면 면마다 하나, split[] 에 보고). 레벨 면은 지울 수 없어 clearBays 면 숨긴다.
	Dispatcher.Register(TEXT("bay.toPresets"), [this, NeedWorld, Collect](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* W = NeedWorld(E); if (!W) return nullptr;
		AParkingPresetManager* Mgr = GetPresetManager(E); if (!Mgr) return nullptr;
		TArray<FBayRec> Bays; Collect(W, Bays);
		TArray<const FBayRec*> Chosen;
		if (!BayChosen(P, Bays, Chosen, E)) return nullptr;

		TArray<FBayMadePreset> Made;
		TArray<FString> Split;
		BaysToPresets(Chosen, RpcParam::GetInt(P, TEXT("camIdx"), 1), Mgr->NextPresetIdx(), Made, Split);
		for (const FBayMadePreset& M : Made) { Mgr->AddPreset(M.Preset); }

		int32 Cleared = 0;
		if (RpcParam::GetBool(P, TEXT("clearBays"), true))
		{
			for (const FBayRec* R : Chosen)
			{
				if (R->bLevel) { if (BaySetLevelSlotHidden(W, *R, true, HiddenLevelSlots)) ++Cleared; }
				else if (R->Actor) { R->Actor->Destroy(); ++Cleared; }
			}
		}
		Mgr->RefreshView();

		TArray<TSharedPtr<FJsonValue>> PresetArr;
		for (const FBayMadePreset& M : Made) { PresetArr.Add(BayMadeToDto(M)); }
		TArray<FString> Names;
		for (const FBayRec* R : Chosen) { Names.Add(R->Name); }
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("count"), Made.Num());
		O->SetArrayField(TEXT("presets"), PresetArr);
		O->SetNumberField(TEXT("bayCount"), Chosen.Num());
		O->SetArrayField(TEXT("bays"), BayStringArray(Names));
		O->SetArrayField(TEXT("split"), BayStringArray(Split));
		O->SetNumberField(TEXT("clearedBays"), Cleared);
		return RpcDto::MakeObject(O);
	});

	// bay.exportPresets {fileName | fullPath(필수) bays[]? | name? names[]? group? camIdx?=1 overwrite?=false}
	//   → {ok, overwritten, path, fileName, count, presets[], bayCount, bays[], split[]}
	// 면 → 언리얼 프리셋 **파일**. 씬은 건드리지 않는다. bays[] 를 주면 그것만(씬에 넣지 않는다), 없으면 씬 면.
	Dispatcher.Register(TEXT("bay.exportPresets"), [NeedWorld, Collect](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		FString Path;
		if (!BayResolvePresetWritePath(P, Path, E)) return nullptr;
		const bool bExisted = IFileManager::Get().FileExists(*Path);
		if (bExisted && !RpcParam::GetBool(P, TEXT("overwrite"), false))
		{
			E.FailDomain(FString::Printf(TEXT("이미 있는 파일: %s — overwrite:true 로 덮어쓴다"), *FPaths::GetCleanFilename(Path)));
			return nullptr;
		}

		TArray<FBayRec> Inline;
		TArray<FBayRec> Bays;
		TArray<const FBayRec*> Chosen;
		const TArray<TSharedPtr<FJsonValue>>* Raw = nullptr;
		if (P.IsValid() && P->TryGetArrayField(TEXT("bays"), Raw) && Raw)
		{
			if (Raw->Num() == 0) { E.FailDomain(TEXT("bays[] 가 비어 있습니다")); return nullptr; }
			for (int32 k = 0; k < Raw->Num(); ++k)
			{
				const TSharedPtr<FJsonValue>& V = (*Raw)[k];
				if (!V.IsValid() || V->Type != EJson::Object)
				{
					E.FailDomain(FString::Printf(TEXT("bays[%d] 은 객체여야 합니다"), k));
					return nullptr;
				}
				FBayRec R;
				if (!BayParseSpec(V->AsObject(), FString::Printf(TEXT("b%d"), k + 1), R, E)) return nullptr;
				Inline.Add(R);
			}
			for (const FBayRec& R : Inline) { Chosen.Add(&R); }
		}
		else
		{
			UWorld* W = NeedWorld(E); if (!W) return nullptr;
			Collect(W, Bays);
			if (!BayChosen(P, Bays, Chosen, E)) return nullptr;
		}

		TArray<FBayMadePreset> Made;
		TArray<FString> Split;
		BaysToPresets(Chosen, RpcParam::GetInt(P, TEXT("camIdx"), 1), 1, Made, Split);
		TArray<FParkingPreset> Presets;
		for (const FBayMadePreset& M : Made) { Presets.Add(M.Preset); }
		// preset.save 와 같은 경로(isUnreal=true 언리얼 미터 스키마). SavePresetsToJson 은 폴더를 만들지 않으므로 먼저 만든다.
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), /*Tree=*/true);
		if (!UPresetMakerWidget::SavePresetsToJson(Path, Presets))
		{
			E.FailDomain(FString::Printf(TEXT("프리셋 저장 실패: %s"), *Path));
			return nullptr;
		}

		TArray<TSharedPtr<FJsonValue>> PresetArr;
		for (const FBayMadePreset& M : Made) { PresetArr.Add(BayMadeToDto(M)); }
		TArray<FString> Names;
		for (const FBayRec* R : Chosen) { Names.Add(R->Name); }
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetBoolField(TEXT("overwritten"), bExisted);
		O->SetStringField(TEXT("path"), Path);
		O->SetStringField(TEXT("fileName"), FPaths::GetCleanFilename(Path));
		O->SetNumberField(TEXT("count"), Made.Num());
		O->SetArrayField(TEXT("presets"), PresetArr);
		O->SetNumberField(TEXT("bayCount"), Chosen.Num());
		O->SetArrayField(TEXT("bays"), BayStringArray(Names));
		O->SetArrayField(TEXT("split"), BayStringArray(Split));
		return RpcDto::MakeObject(O);
	});
}
