// Copyright Epic Games, Inc. All Rights Reserved.

#include "EnvRpcModule.h"
#include "../RpcDispatcher.h"
#include "../RpcParamUtil.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"

namespace
{
	/**
	 * 목록에 낼 가치가 있는 액터인가.
	 * 보이는 지오메트리가 있어야 "가리는 물체"로서 의미가 있다 — 컨트롤러·게임모드·볼륨은 걸러낸다.
	 */
	bool IsVisibleGeometry(const AActor* Actor)
	{
		if (!Actor || !IsValid(Actor))
		{
			return false;
		}
		TArray<UPrimitiveComponent*> Prims;
		const_cast<AActor*>(Actor)->GetComponents<UPrimitiveComponent>(Prims);
		for (const UPrimitiveComponent* Prim : Prims)
		{
			// 숨겨 둔 것도 목록에는 남긴다 — 다시 켜려면 이름이 보여야 한다.
			if (Prim && Prim->IsRegistered() && Prim->Bounds.SphereRadius > 1.f)
			{
				return true;
			}
		}
		return false;
	}

	/** Park3D 가 소유한 액터는 제외한다 — 그쪽은 car.* / preset.* / cam.* 이 담당한다. */
	bool IsPark3DOwned(const AActor* Actor)
	{
		const FString Class = Actor->GetClass()->GetName();
		return Class.StartsWith(TEXT("CarActor")) || Class.StartsWith(TEXT("PTZCamera"))
			|| Class.StartsWith(TEXT("Parking")) || Class.StartsWith(TEXT("MapFloor"))
			|| Class.StartsWith(TEXT("CarPlacement")) || Class.StartsWith(TEXT("CameraControl"))
			|| Class.StartsWith(TEXT("LightControl"));
	}

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
}

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
			if (!IsVisibleGeometry(Actor) || IsPark3DOwned(Actor))
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

		TArray<TSharedPtr<FJsonValue>> Changed;
		TSet<FString> Found;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor || !Wanted.Contains(Actor->GetName()))
			{
				continue;
			}
			Found.Add(Actor->GetName());
			Actor->SetActorHiddenInGame(bHidden);
			Actor->SetActorEnableCollision(!bHidden);
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
}
