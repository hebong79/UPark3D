// Copyright Epic Games, Inc. All Rights Reserved.
// CarActor : 배치된 차량 1대. Unity CObjCar 포팅.
// FCarPos(Unity 좌표/회전) 로부터 메시·위치·회전을 설정하고, 도색(UCarColorComponent)·선택표시를 제공한다.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ParkingCarTypes.h"
#include "CarActor.generated.h"

class UStaticMeshComponent;
class UTextRenderComponent;
class UCarColorComponent;
class UMaterialInterface;

UCLASS()
class PARK3D_API ACarActor : public AActor
{
	GENERATED_BODY()

public:
	ACarActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Car")
	UStaticMeshComponent* MeshComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Car")
	UCarColorComponent* ColorComp;

	/** Content/Cars/번호판의 앞/뒤 번호판 메시. 차량 메시와 별도라 도색/선택 overlay 영향을 받지 않는다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Car|Plate")
	UStaticMeshComponent* FrontPlateComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Car|Plate")
	UStaticMeshComponent* BackPlateComp;

	/** Content plate 뒤에만 보이는 검정 520×110 비율 backing/frame. 런타임 Cube/MID이며 Content를 수정하지 않는다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Car|Plate")
	UStaticMeshComponent* FrontPlateFrameComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Car|Plate")
	UStaticMeshComponent* BackPlateFrameComp;

	/** 한국 일반 번호판의 좌측 KOR 보안 띠를 근사한 파란 runtime Cube. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Car|Plate")
	UStaticMeshComponent* FrontPlateSecurityStripComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Car|Plate")
	UStaticMeshComponent* BackPlateSecurityStripComp;

	/** 번호판 위에 1회 설정한 동일 문자열을 표시하는 앞/뒤 텍스트. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Car|Plate")
	UTextRenderComponent* FrontPlateText;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Car|Plate")
	UTextRenderComponent* BackPlateText;

	/** 이 actor 수명 동안 유지되는 결정적 한국 일반 승용차 형식(123다4567) 번호. JSON에는 저장하지 않는다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Car|Plate")
	FString PlateNumber;

	/**
	 * 메시 시각 정면축 보정. 공통 좌표에서 Unity rotY=0의 논리 전방(+Z)은 UE +X다.
	 * PIE 시각 검증에서 Unity 전면주차가 UE 후면으로 보인 것을 보정하기 위해 액터에는 +270도를 더한다.
	 * isFront=false는 이 보정 뒤에 180도를 추가해 항상 정확한 반대 방향을 유지한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Car")
	float MeshForwardYawOffset = 270.f;

	/** 선택 시 메시에 덧입히는 오버레이 머티리얼(반투명 청록 림 발광). 기본 /Game/UI/M_CarSelect. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Car")
	TObjectPtr<UMaterialInterface> SelectionOverlayMaterial;

	/** 이 차량의 데이터(id/type/preset/slot/prefab/pos/rotY/isFront). */
	UPROPERTY(BlueprintReadOnly, Category = "Car")
	FCarPos CarData;

	/** FCarPos + 메시로 초기화: 데이터 저장 → 메시 적용 → 트랜스폼 적용. */
	UFUNCTION(BlueprintCallable, Category = "Car")
	void InitFromPos(const FCarPos& Pos, UStaticMesh* InMesh, float MetersToUU = 100.f);

	/** CarData(Unity 좌표/회전) → 액터 월드 트랜스폼 적용. */
	UFUNCTION(BlueprintCallable, Category = "Car")
	void ApplyTransformFromData(float MetersToUU = 100.f);

	/** 현재 액터 트랜스폼 → FCarPos (UE→Unity 역변환). id/type 등 메타는 CarData 유지. */
	UFUNCTION(BlueprintCallable, Category = "Car")
	FCarPos ToCarPos(float MetersToUU = 100.f) const;

	/** 선택 표시(커스텀 뎁스 스텐실 — 외곽선 포스트프로세스는 위젯 단계에서 연동). */
	UFUNCTION(BlueprintCallable, Category = "Car")
	void SetSelected(bool bInSelected);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Car")
	bool IsSelected() const { return bSelected; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Car|Plate")
	FString GetPlateNumber() const { return PlateNumber; }

	/** 동일 차량 데이터에서 같은 pseudo-random 번호를 만드는 순수 규약(Automation/재로드 검증용). */
	static FString MakeDeterministicPlateNumber(const FCarPos& Pos);

private:
	/** 최초 InitFromPos에서만 PlateNumber와 앞/뒤 텍스트를 설정한다. */
	void InitializePlateNumberOnce();

	/** canonical 123다4567을 TextRender용 123 다 4567로만 분리한다. */
	static FString MakePlateDisplayText(const FString& CanonicalNumber);

	/** 차량 메시 local bounds를 기준으로 앞/뒤 plate 부착 위치/가시성을 갱신한다. */
	void UpdatePlatePresentation();

	bool bSelected = false;
};
