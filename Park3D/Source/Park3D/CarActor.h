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
class UWidgetComponent;
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

	/**
	 * 실제로 화면에 번호를 그리는 3D 위젯(앞/뒤). 위의 TextRender 는 Runtime 캐시 폰트를 못 그려
	 * 아무것도 표시하지 못한다(Docs/20260817_225749) — 컴포넌트는 호환을 위해 남기되 숨긴다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Car|Plate")
	UWidgetComponent* FrontPlateWidget;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Car|Plate")
	UWidgetComponent* BackPlateWidget;

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

	/**
	 * 차량을 지면에 앉힌다(바퀴 접지). 아래로 라인트레이스한 지면 높이에 메시 로컬 바운즈의 바닥을 맞추고,
	 * 보정한 높이를 CarData.pos.z 에도 되쓴다 — 안 그러면 car.list/저장이 화면과 다른 z 를 보고한다.
	 *
	 * 물리(리지드바디)를 쓰지 않는 이유: 차량은 QueryOnly 콜리전의 정적 소품이라 시뮬레이션을 켜면
	 * 65대가 매 틱 적분되며 미끄러짐·떨림이 생기고, 픽셀 기반 가림률 측정(scenario.*)의 재현성이 깨진다.
	 * 접지는 트레이스 1회로 결정적으로 끝난다.
	 *
	 * @return 실제로 높이를 옮겼으면 true. 지면을 못 찾거나 이미 앉아 있으면 false(높이를 지어내지 않는다).
	 */
	UFUNCTION(BlueprintCallable, Category = "Car")
	bool SnapToGround(float MetersToUU = 100.f);

	/** 선택 표시(커스텀 뎁스 스텐실 — 외곽선 포스트프로세스는 위젯 단계에서 연동). */
	UFUNCTION(BlueprintCallable, Category = "Car")
	void SetSelected(bool bInSelected);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Car")
	bool IsSelected() const { return bSelected; }

	/**
	 * 선택 표시(청록 오버레이)를 화면에 낼지 여부. 선택 상태(bSelected) 자체는 건드리지 않는다 —
	 * 표시를 꺼도 이동·회전·수정 대상은 그대로 선택된 차량이어야 하기 때문이다.
	 * 전체 일괄 설정은 ACarPlacementManager::SetSelectionMarkVisible 가 창구다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Car")
	void SetSelectionMarkVisible(bool bInVisible);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Car")
	bool IsSelectionMarkVisible() const { return bSelectionMarkVisible; }

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

	/**
	 * 번호판 메시의 실제 축에 맞춰 판을 돌리고 글자를 면 바깥에 놓는다.
	 * 축을 코드에 박으면 콘텐츠(판 메시)가 바뀌는 순간 글자가 모서리로 서서 사라진다 —
	 * 실제로 2026-08-16 콘텐츠 교체 때 그렇게 됐다(로그에는 vis=1 인데 화면에는 없다).
	 */
	void AlignPlateAndText(UStaticMeshComponent* Plate, UTextRenderComponent* Text, bool bFront);

	/** 글자를 판 표면에서 얼마나 띄울지(cm). z-fighting 만 피하면 되므로 작게. */
	static constexpr float PlateTextSurfaceGap = 0.3f;
	/** 좌측 파란 KOR 영역을 피해 글자를 오른쪽으로 미는 양(cm, 판의 긴 축 방향). */
	static constexpr float PlateTextSideShift = 4.f;
	/** 번호 위젯을 판 표면에서 띄우는 양(cm). 글자(TextRender)보다 살짝 더 바깥. */
	static constexpr float PlateWidgetSurfaceGap = 0.5f;
	/**
	 * 위젯이 판 폭에서 차지하는 비율(좌측 파란 KOR 영역을 빼고 남는 만큼).
	 * 글자는 ScaleBox 가 담기는 만큼 줄이므로 이 값은 "번호가 쓰이는 영역"의 폭만 정한다
	 * (좌측 파란 KOR 영역 + 테두리를 뺀 비율).
	 */
	static constexpr float PlateWidgetFill = 0.82f;

	/**
	 * 접지 트레이스 구간(cm). 차량 바닥에서 위/아래로 이만큼만 본다.
	 * 넓히면 육교·지하 구조물처럼 이 차와 무관한 정적 지오메트리를 집을 수 있다.
	 */
	static constexpr float GroundTraceUpCm = 100.f;
	static constexpr float GroundTraceDownCm = 100.f;

	/** 이보다 작은 차이는 옮기지 않는다(cm). 매 틱 도는 시뮬 주행에서 미세 진동을 막는다. */
	static constexpr float GroundSnapToleranceCm = 0.1f;

	/** 선택 여부와 표시 설정을 함께 반영한다(둘 다 참일 때만 오버레이가 붙는다). */
	void ApplySelectionVisual();

	bool bSelected = false;
	bool bSelectionMarkVisible = true;
};
