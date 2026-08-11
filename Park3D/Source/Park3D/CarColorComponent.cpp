// Copyright Epic Games, Inc. All Rights Reserved.

#include "CarColorComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

UCarColorComponent::UCarColorComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PaintSlotNames = { TEXT("carpaint") };  // 1차: 주 도색 슬롯만. (carpaint_second/third 는 필요 시 추가)
}

UStaticMeshComponent* UCarColorComponent::FindMesh() const
{
	if (const AActor* Owner = GetOwner())
	{
		return Owner->FindComponentByClass<UStaticMeshComponent>();
	}
	return nullptr;
}

void UCarColorComponent::EnsureMIDs()
{
	if (bInitialized)
	{
		return;
	}

	UStaticMeshComponent* Mesh = FindMesh();
	if (!Mesh)
	{
		return;
	}

	PaintMIDs.Reset();
	for (const FName& Slot : PaintSlotNames)
	{
		const int32 Idx = Mesh->GetMaterialIndex(Slot);
		if (Idx == INDEX_NONE)
		{
			continue;
		}
		UMaterialInstanceDynamic* MID = Mesh->CreateDynamicMaterialInstance(Idx);
		if (MID)
		{
			// 첫 슬롯의 임포트 기본 색을 원래 색으로 캡처(복원용).
			if (PaintMIDs.Num() == 0)
			{
				FLinearColor Captured;
				if (MID->GetVectorParameterValue(FMaterialParameterInfo(ColorParamName), Captured))
				{
					OriginalColor = Captured;
				}
			}
			PaintMIDs.Add(MID);
		}
	}
	bInitialized = true;
}

void UCarColorComponent::SetColor(FLinearColor Color)
{
	EnsureMIDs();
	for (UMaterialInstanceDynamic* MID : PaintMIDs)
	{
		if (MID)
		{
			MID->SetVectorParameterValue(ColorParamName, Color);
		}
	}
	CurrentColor = Color;
}

void UCarColorComponent::SetColorByEnum(ECarColor InColor)
{
	SetColor(ColorForEnum(InColor));
}

void UCarColorComponent::ResetColor()
{
	EnsureMIDs();
	for (UMaterialInstanceDynamic* MID : PaintMIDs)
	{
		if (MID)
		{
			MID->SetVectorParameterValue(ColorParamName, OriginalColor);
		}
	}
	CurrentColor = OriginalColor;
}

bool UCarColorComponent::SetMetallic(float Metallic, float Smoothness)
{
	EnsureMIDs();
	CurrentMetallic = FMath::Clamp(Metallic, 0.f, 1.f);
	CurrentSmoothness = FMath::Clamp(Smoothness, 0.f, 1.f);

	bool bApplied = false;
	for (UMaterialInstanceDynamic* MID : PaintMIDs)
	{
		if (!MID)
		{
			continue;
		}
		// SetScalarParameterValue 는 없는 파라미터를 조용히 무시한다 → 존재 여부를 먼저 물어본다.
		float Probe = 0.f;
		const bool bHasMetallic = MID->GetScalarParameterValue(FMaterialParameterInfo(MetallicParamName), Probe);
		const bool bHasRoughness = MID->GetScalarParameterValue(FMaterialParameterInfo(RoughnessParamName), Probe);
		if (bHasMetallic)
		{
			MID->SetScalarParameterValue(MetallicParamName, CurrentMetallic);
		}
		if (bHasRoughness)
		{
			MID->SetScalarParameterValue(RoughnessParamName, 1.f - CurrentSmoothness);
		}
		bApplied |= (bHasMetallic || bHasRoughness);
	}
	return bApplied;
}

FLinearColor UCarColorComponent::ColorForEnum(ECarColor InColor)
{
	switch (InColor)
	{
	case ECarColor::White:  return FLinearColor(1.00f, 1.00f, 1.00f);
	case ECarColor::Black:  return FLinearColor(0.02f, 0.02f, 0.02f);
	case ECarColor::Silver: return FLinearColor(0.75f, 0.75f, 0.78f);
	case ECarColor::Gray:   return FLinearColor(0.35f, 0.35f, 0.37f);
	case ECarColor::Red:    return FLinearColor(0.60f, 0.03f, 0.03f);
	case ECarColor::Blue:   return FLinearColor(0.03f, 0.08f, 0.50f);
	case ECarColor::Green:  return FLinearColor(0.03f, 0.35f, 0.08f);
	case ECarColor::Yellow: return FLinearColor(0.80f, 0.70f, 0.02f);
	case ECarColor::Orange: return FLinearColor(0.80f, 0.30f, 0.02f);
	case ECarColor::Purple: return FLinearColor(0.30f, 0.03f, 0.40f);
	default:                return FLinearColor::White;
	}
}
