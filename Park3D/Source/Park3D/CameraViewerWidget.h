// Copyright Epic Games, Inc. All Rights Reserved.
// CameraViewerWidget : 선택 카메라의 렌더타겟을 화면에 독립 표시하는 뷰어(WBP_CameraViewer).
// Unity CPCamViewerUI 대응. 컨트롤 패널(UCameraControlWidget)과 분리되어 우하단에 고정 출력된다.
// 화면 우하단 앵커/크기는 WBP_CameraViewer 디자이너에서 지정한다.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CameraViewerWidget.generated.h"

class UImage;
class UTextureRenderTarget2D;

/** 뷰어 표시 크기 저장용(16:9). 멤버명 소문자 시작 = 프로젝트 JSON 직렬화 관례. */
USTRUCT()
struct FCameraViewerSizeConfig
{
	GENERATED_BODY()
	UPROPERTY() float width = 480.f;
	UPROPERTY() float height = 270.f;
};

UCLASS()
class PARK3D_API UCameraViewerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 렌더타겟 표시 이미지(WBP 위젯명과 일치). 우하단 앵커/크기는 디자이너에서 지정. */
	UPROPERTY(meta = (BindWidget)) UImage* Img_View = nullptr;

	/** 선택 카메라의 렌더타겟을 이미지 브러시로 지정한다. RT가 null이면 무시. */
	UFUNCTION(BlueprintCallable, Category = "Camera")
	void SetRenderTarget(UTextureRenderTarget2D* RT);

	// ---- 표시 크기 영속화(리사이즈 → 저장, 재시작 → 복원) ----
	/** 크기 저장 파일 경로(ProjectSaved/CameraViewer/ViewerSize.json). */
	static FString GetSizeConfigPath();
	/** 표시 크기를 JSON 파일로 저장한다. */
	static bool SaveSizeConfigToPath(const FString& Path, const FVector2D& Size);
	/** JSON 파일에서 표시 크기를 로드한다(파일 없거나 오류면 false). */
	static bool LoadSizeConfigFromPath(const FString& Path, FVector2D& OutSize);

	// ---- 이동/리사이즈 (16:9 유지) ----
	/** 조절 가능한 기본 표시 크기(16:9). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewer")
	FVector2D DefaultViewSize = FVector2D(480.f, 270.f);

	/** 리사이즈 폭 하한/상한(px). 높이는 16:9로 파생. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewer")
	float MinViewWidth = 160.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewer")
	float MaxViewWidth = 1280.f;

	/** 우하단 리사이즈 코너 감지 영역(로컬 px). 그 밖은 이동. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewer")
	float ResizeCornerZone = 28.f;

	/** 뷰어 외곽 프레임(구멍난 사각형) 색/두께. 두께 0 이하면 미표시. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewer")
	FLinearColor FrameColor = FLinearColor(0.9f, 0.9f, 0.9f, 1.f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewer")
	float FrameThickness = 2.f;

protected:
	virtual void NativeConstruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	/** 이미지 우하단 코너 위에서 리사이즈 커서 표시(그 밖은 이동 커서). */
	virtual FCursorReply NativeOnCursorQuery(const FGeometry& InGeometry, const FPointerEvent& InCursorEvent) override;
	/** 이미지 사각형 둘레에 외곽 프레임을 그린다(이동·리사이즈 자동 추종). */
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	/** 현재 표시 크기를 Img_View 에 반영(Brush.ImageSize 보다 우선). */
	void ApplyViewSize();

	FVector2D CurrentViewSize = FVector2D(480.f, 270.f); // 현재 표시 크기(16:9)
	FVector2D ViewerTranslation = FVector2D::ZeroVector;  // 누적 이동 오프셋
	FVector2D DragStartLocal = FVector2D::ZeroVector;
	FVector2D DragStartTranslation = FVector2D::ZeroVector;
	FVector2D DragStartSize = FVector2D::ZeroVector;
	FVector2D DragStartSlotPos = FVector2D::ZeroVector; // 캔버스 슬롯 시작 위치(이동용)
	bool bMovingViewer = false;
	bool bResizingViewer = false;
	// 리사이즈 시 잡은 변(방향). 코너는 두 변이 동시에 true.
	bool bEdgeL = false, bEdgeR = false, bEdgeT = false, bEdgeB = false;
};
