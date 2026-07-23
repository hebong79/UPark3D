// Copyright Epic Games, Inc. All Rights Reserved.
// CarColorComponent : 차량 도색 제어. Unity CVehicleColorController 포팅.
// 차량 메시의 "carpaint" 슬롯에 동적 머티리얼 인스턴스를 만들어 BaseColorFactor 를 바꾼다.
//  (carpaint 머티리얼은 glTF MI_Default_Opaque_DS 기반 → 색 파라미터 이름은 BaseColorFactor)

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ParkingCarTypes.h"
#include "CarColorComponent.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;

UCLASS(ClassGroup = (Car), meta = (BlueprintSpawnableComponent))
class PARK3D_API UCarColorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCarColorComponent();

	/** 도색 대상 머티리얼 슬롯 이름(Unity carpaint). 차종에 따라 carpaint_second/third 도 추가 가능. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Car|Color")
	TArray<FName> PaintSlotNames;

	/** 색 파라미터 이름 (glTF 기반 carpaint → BaseColorFactor). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Car|Color")
	FName ColorParamName = TEXT("BaseColorFactor");

	/** 도색 색상 적용. 동적 머티리얼이 없으면 생성 후 적용. */
	UFUNCTION(BlueprintCallable, Category = "Car|Color")
	void SetColor(FLinearColor Color);

	/** ECarColor 프리셋으로 도색. */
	UFUNCTION(BlueprintCallable, Category = "Car|Color")
	void SetColorByEnum(ECarColor InColor);

	/** 원래 색(임포트 기본 BaseColorFactor)으로 복원. */
	UFUNCTION(BlueprintCallable, Category = "Car|Color")
	void ResetColor();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Car|Color")
	FLinearColor GetCurrentColor() const { return CurrentColor; }

	/** ECarColor → FLinearColor 매핑(순수). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Car|Color")
	static FLinearColor ColorForEnum(ECarColor InColor);

private:
	/** 소유 액터의 StaticMeshComponent 검색. */
	UStaticMeshComponent* FindMesh() const;

	/** PaintSlotNames 슬롯에 동적 머티리얼 인스턴스를 생성(1회). 원래 색도 캡처. */
	void EnsureMIDs();

	UPROPERTY(Transient)
	TArray<UMaterialInstanceDynamic*> PaintMIDs;

	bool bInitialized = false;
	FLinearColor OriginalColor = FLinearColor::White;
	FLinearColor CurrentColor = FLinearColor::White;
};
