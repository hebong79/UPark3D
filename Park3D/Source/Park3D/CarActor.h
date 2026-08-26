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
class UMaterialInstanceDynamic;
class UText3DComponent;

/**
 * 번호판 메시에서 읽어 낸 로컬 기준틀. 축을 코드에 박으면 콘텐츠(판 메시)가 바뀌는 순간
 * 글자가 모서리로 서서 사라진다 — 2026-08-16 콘텐츠 교체 때 실제로 그렇게 됐다.
 * 그래서 판을 쓰는 쪽(글자·위젯·양각)이 전부 이 한 곳에서 축을 받는다.
 */
struct FCarPlateFrame
{
	/** 판 메시 바운즈 원점(판 로컬). 메시가 원점에서 치우쳐 있으면 두께만으로는 면을 못 찾는다. */
	FVector Origin = FVector::ZeroVector;
	/** 판 로컬에서 차량 바깥(보이는 면)을 향하는 방향. */
	FVector Outward = FVector::ZeroVector;
	/** 판의 긴 축. 글자가 이 방향으로 흐른다. */
	FVector Wide = FVector::ZeroVector;
	/** 원점에서 면까지의 거리(cm). */
	double HalfThickness = 0.0;
	/** 판 폭(긴 축 지름, cm). */
	double WidthCm = 0.0;
	/** 판 높이(얇지도 넓지도 않은 축의 지름, cm). */
	double HeightCm = 0.0;
};

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

	/**
	 * 번호를 **압출 지오메트리**로 그리는 양각(앞/뒤). 실물 번호판은 문자가 1.3~1.6mm 양각이고
	 * (「자동차 등록번호판 등의 기준에 관한 고시」), 화면에서 양각으로 읽히는 것은 색이 아니라
	 * 그 측벽에 생기는 명암이다. 위의 위젯은 Unlit 이라 조명을 아예 받지 못한다.
	 * 빌드가 실제로 됐는지 확인되기 전까지 위젯을 폴백으로 남긴다(HandlePlateEmbossBuilt).
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Car|Plate")
	UText3DComponent* FrontPlateEmboss;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Car|Plate")
	UText3DComponent* BackPlateEmboss;

	/** 양각 글자의 검은 도색면. 앞/뒤가 같은 색이라 하나를 공유한다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Car|Plate")
	UMaterialInstanceDynamic* PlateEmbossMaterial;

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

	/** canonical 123다4567을 표시용 123다 4567로만 분리한다(앞 세 자리와 한글은 붙는다). */
	static FString MakePlateDisplayText(const FString& CanonicalNumber);

	/** 차량 메시 local bounds를 기준으로 앞/뒤 plate 부착 위치/가시성을 갱신한다. */
	void UpdatePlatePresentation();

	/**
	 * 번호판 메시의 실제 축에 맞춰 판을 돌리고 글자를 면 바깥에 놓는다.
	 * 축을 코드에 박으면 콘텐츠(판 메시)가 바뀌는 순간 글자가 모서리로 서서 사라진다 —
	 * 실제로 2026-08-16 콘텐츠 교체 때 그렇게 됐다(로그에는 vis=1 인데 화면에는 없다).
	 */
	void AlignPlateAndText(UStaticMeshComponent* Plate, UTextRenderComponent* Text, bool bFront);

	/**
	 * 양각 번호를 판 면 위에 놓는다. 축은 `AlignPlateAndText` 와 같은 규약(메시 바운즈에서 읽는다)이고,
	 * 번호가 놓이는 영역은 위젯(`UCarPlateNumberWidget`)의 여백과 같은 비율을 쓴다 — 두 경로가
	 * 어긋나면 폴백으로 위젯이 켜지는 순간 번호가 옆으로 튄다.
	 */
	void AlignPlateEmboss(UText3DComponent* Emboss, const FCarPlateFrame& Frame);

	/** 양각이 실제로 만들어졌는지 확인하고, 됐으면 폴백 위젯을 끈다. Text3D 빌드 완료 시 호출된다. */
	void HandlePlateEmbossBuilt();

	/** 빌드가 만든 글자 메시 컴포넌트의 머티리얼 슬롯을 전부 검게 칠한다(구현부 주석에 근거). */
	void PaintPlateEmboss(UText3DComponent* Emboss);


	/** 양각 메시가 실제로 생겼는가(바운즈가 0이면 폰트 아웃라인을 못 읽은 것이다). */
	static bool IsEmbossBuilt(const UText3DComponent* Emboss);

	/** 글자를 판 표면에서 얼마나 띄울지(cm). z-fighting 만 피하면 되므로 작게. */
	static constexpr float PlateTextSurfaceGap = 0.3f;
	/** 좌측 파란 KOR 영역을 피해 글자를 오른쪽으로 미는 양(cm, 판의 긴 축 방향). */
	static constexpr float PlateTextSideShift = 4.f;
	/** 번호 위젯을 판 표면에서 띄우는 양(cm). 글자(TextRender)보다 살짝 더 바깥. */
	static constexpr float PlateWidgetSurfaceGap = 0.5f;
	/**
	 * 번호 위젯의 DrawSize(px) 기준 폭. 판 폭을 이 값으로 나눈 것이 위젯 스케일이므로
	 * 위젯은 판에 1:1 로 덮인다 — 번호가 놓이는 영역(좌측 파란 KOR 영역 제외)은
	 * 위젯 안쪽 여백(`UCarPlateNumberWidget`)이 정한다.
	 */
	static constexpr float PlateWidgetDrawWidth = 520.f;

	/**
	 * 양각 돌출 높이(cm). 고시 규격 1.3~1.6mm 의 중앙값이다. 임의로 키우면 근접 캡처에서
	 * 장난감처럼 보인다 — 규격대로 두고, 부족하다는 판단이 서면 그때 올린다.
	 */
	static constexpr float PlateEmbossReliefCm = 0.15f;
	/**
	 * Text3D 에 넣는 압출값(cm). 규격값을 그대로 넣는다.
	 *
	 * **완성된 메시의 두께는 0.1cm 단위로만 나온다** — 실측 세 점 `0.150 → 0.100`,
	 * `0.1875 → 0.100`, `0.225 → 0.200` 이 전부 `내림(입력, 0.1cm)` 에 맞는다(베벨 0.02·세그먼트 2).
	 * 그래서 1.3~1.6mm 를 정확히 맞출 수단이 없고, 실효 두께는 1.0mm 다(규격보다 얇은 쪽).
	 * 처음엔 두 점만 보고 `두께 = 4/3 × 입력 − 0.1` 로 읽어 0.1875 를 넣었는데 세 번째 점이
	 * 그 직선을 반증했다 — **두 점으로 직선을 긋지 말 것.**
	 * 로그의 `양각 … 크기=X=` 가 실측 두께이고, 차량이 축에 정렬된 것만 읽어야 한다
	 * (비스듬한 차량은 월드 AABB 라 폭이 섞여 들어온다).
	 */
	static constexpr float PlateEmbossExtrudeInput = PlateEmbossReliefCm;
	/** 압인 모서리는 칼각이 아니라 둥글다. 돌출의 1/7 정도만 준다(Extrude/2 를 넘으면 엔진이 클램프한다). */
	static constexpr float PlateEmbossBevel = 0.02f;
	/** 베벨 분할 수. 1.5mm 짜리 모서리라 2면 충분하고, 늘리면 글자 수만큼 삼각형이 곱해진다. */
	static constexpr int32 PlateEmbossBevelSegments = 2;
	/** 글자 밑면을 판 표면 **안쪽**으로 이만큼 묻는다(cm). 면과 정확히 맞추면 z-fighting 이 난다. */
	static constexpr float PlateEmbossSurfaceSink = 0.02f;
	/**
	 * Pretendard Bold 로 "123가 4567"(8자+간격)을 그렸을 때 **폰트 크기 1 당 가로 폭(cm)**.
	 * 실측 근거: FontSize 24 + 영역 맞춤 축소 0.04 에서 폭 9.585cm → 축소 전 239.6cm → 24로 나눈 값.
	 * 번호에 따라 ±1% 흔들린다(9.49~9.66/24 범위) — 판 오른쪽 여백 2cm 가 흡수한다.
	 *
	 * **Text3D 의 자동 영역 맞춤(SetHasMaxWidth/Height)은 쓰지 않는다.** 두 가지 이유다 —
	 *  ① 그 축소는 가로·세로뿐 아니라 **압출 축까지** 줄여 규격 1.5mm 를 깎는다
	 *     (`Text3DDefaultLayoutExtension::CalculateTextScale` 의 `Scale.X = Scale.Y`).
	 *  ② Max 값이 월드 cm 가 아니라 레이아웃 단위로 비교된다 — 영역 폭 42cm 를 그대로 넣고 폰트를
	 *     키웠더니 글자가 커지기는커녕 2.2cm 로 쪼그라들고 메모리가 21GB 까지 치솟았다(실측).
	 * 그래서 처음부터 영역에 맞는 크기로 그리고, 축소는 아예 타지 않는다(TextScale = 1 → 압출 정확).
	 * 최종 보정: 9.98 로 그렸을 때 폭이 35.2cm(목표 42.08)여서 42.08/35.2 만큼 낮췄다.
	 */
	static constexpr float PlateEmbossWidthPerFontSize = 9.98f * 35.16f / 42.08f;

	// 번호가 놓이는 영역. 위젯(`UCarPlateNumberWidget`)의 DrawSize(520×110px) 여백과 같은 비율이라
	// 양각과 폴백 위젯이 같은 자리를 쓴다. 좌측 여백이 큰 것은 파란 KOR 스트립을 피하기 때문이다.
	/** 번호 영역 왼쪽 여백(판 폭 대비). 위젯 80/520. */
	static constexpr float PlateNumberAreaLeftFrac = 80.f / 520.f;
	/** 번호 영역 오른쪽 여백(판 폭 대비). 위젯 20/520. */
	static constexpr float PlateNumberAreaRightFrac = 20.f / 520.f;
	/** 번호 영역 위아래 여백(판 높이 대비). 위젯 10/110. */
	static constexpr float PlateNumberAreaVerticalFrac = 10.f / 110.f;

	/**
	 * Text3D 의 **읽는 면이 로컬 -X 를 본다**는 규약. 근거는 엔진 소스다 — 글리프는 YZ 평면에 놓이고
	 * 압출이 +X 로 나간다(`Text3DGlyphMeshBuilder.cpp` 가 Front 를 X=0, Back 을 X=Extrude 에 미러로
	 * 놓으므로 X=0 면의 법선은 -X 다).
	 * **글자가 좌우로 뒤집혀 보이면(뒷면을 보고 있는 것이다) 이 값을 +1 로 바꾼다** — 여기 한 곳만
	 * 고치면 회전·글자 흐름·번호 영역 오프셋이 함께 따라온다(세로 방향은 영향받지 않는다).
	 */
	static constexpr float PlateEmbossFaceSign = -1.f;

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

	/** 양각 빌드 완료 델리게이트를 이미 걸었는가(정렬은 재초기화 때 다시 도는데 델리게이트는 한 번만 건다). */
	bool bPlateEmbossHooked = false;
};
