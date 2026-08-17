// Copyright Epic Games, Inc. All Rights Reserved.

#include "EnvActorLibrary.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"

namespace
{
	/** Park3D 가 소유한 액터는 제외한다 — 그쪽은 car.* / preset.* / cam.* 이 담당한다. */
	bool IsPark3DOwned(const AActor* Actor)
	{
		const FString Class = Actor->GetClass()->GetName();
		return Class.StartsWith(TEXT("CarActor")) || Class.StartsWith(TEXT("PTZCamera"))
			|| Class.StartsWith(TEXT("Parking")) || Class.StartsWith(TEXT("MapFloor"))
			|| Class.StartsWith(TEXT("CarPlacement")) || Class.StartsWith(TEXT("CameraControl"))
			|| Class.StartsWith(TEXT("LightControl"));
	}
}

bool Park3DEnv::IsEnvActor(const AActor* Actor)
{
	if (!Actor || !IsValid(Actor) || IsPark3DOwned(Actor))
	{
		return false;
	}
	TArray<UPrimitiveComponent*> Prims;
	const_cast<AActor*>(Actor)->GetComponents<UPrimitiveComponent>(Prims);
	for (const UPrimitiveComponent* Prim : Prims)
	{
		// 이미 숨긴 것도 대상으로 남긴다 — 다시 켜려면 찾을 수 있어야 한다.
		if (Prim && Prim->IsRegistered() && Prim->Bounds.SphereRadius > 1.f)
		{
			return true;
		}
	}
	return false;
}

TArray<AActor*> Park3DEnv::SetHiddenByNames(UWorld* World, const TSet<FString>& Names, bool bHidden)
{
	TArray<AActor*> Changed;
	if (!World || Names.Num() == 0)
	{
		return Changed;
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || !Names.Contains(Actor->GetName()))
		{
			continue;
		}
		Actor->SetActorHiddenInGame(bHidden);
		Actor->SetActorEnableCollision(!bHidden);
		Changed.Add(Actor);
	}
	return Changed;
}
