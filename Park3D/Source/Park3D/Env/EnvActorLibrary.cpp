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
		SetActorHidden(Actor, bHidden);
		Changed.Add(Actor);
	}
	return Changed;
}

void Park3DEnv::SetActorHidden(AActor* Actor, bool bHidden)
{
	if (!Actor)
	{
		return;
	}
	Actor->SetActorHiddenInGame(bHidden);
	Actor->SetActorEnableCollision(!bHidden);
}

bool Park3DEnv::IsMapKeepActor(const AActor* Actor)
{
	if (!Actor)
	{
		return false;
	}
	const FString Class = Actor->GetClass()->GetName();
	if (Class.StartsWith(TEXT("BP_ParkingSlot")))
	{
		return true; // 레벨 주차면 — 바닥 번호·bay.* 가 이것을 센다.
	}
	// 하늘·대기·안개·조명·포스트프로세스는 "맵 오브젝트"가 아니라 무대 자체다 — 숨기면 배경이 검게 되고 조명이 사라진다
	// (실측: 스카이 BP 가 숨겨져 검은 하늘). 클래스 이름으로만 본다 — 이름으로 보면 가로등(street_light)까지 남는다.
	static const TCHAR* KeepClassTokens[] = { TEXT("Sky"), TEXT("Atmosphere"), TEXT("Fog"), TEXT("Light"), TEXT("PostProcess") };
	for (const TCHAR* Token : KeepClassTokens)
	{
		if (Class.Contains(Token, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	static const TCHAR* KeepTokens[] = { TEXT("Road"), TEXT("Ground"), TEXT("Floor"), TEXT("Landscape"), TEXT("Asphalt") };
	const FString Name = Actor->GetName();
	const FString Label = Actor->GetActorNameOrLabel();
	for (const TCHAR* Token : KeepTokens)
	{
		if (Name.Contains(Token, ESearchCase::IgnoreCase)
			|| Label.Contains(Token, ESearchCase::IgnoreCase)
			|| Class.Contains(Token, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}
