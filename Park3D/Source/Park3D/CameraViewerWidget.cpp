// Copyright Epic Games, Inc. All Rights Reserved.

#include "CameraViewerWidget.h"
#include "Components/Image.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/TextureRenderTarget2D.h"
#include "InputCoreTypes.h"
#include "Rendering/DrawElements.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	// 이미지 로컬좌표 L, 크기 S, 존 두께 Z에서 각 변 근접 여부.
	void ComputeViewerEdges(const FVector2D& L, const FVector2D& S, float Z, bool& bL, bool& bR, bool& bT, bool& bB)
	{
		bL = L.X <= Z;
		bR = (S.X - L.X) <= Z;
		bT = L.Y <= Z;
		bB = (S.Y - L.Y) <= Z;
	}
}

void UCameraViewerWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 저장된 표시 크기가 있으면 복원(clamp + 16:9 유지), 없으면 기본값.
	FVector2D Saved;
	if (LoadSizeConfigFromPath(GetSizeConfigPath(), Saved) && Saved.X > 0.f)
	{
		const float W = FMath::Clamp((float)Saved.X, MinViewWidth, MaxViewWidth);
		CurrentViewSize = FVector2D(W, W * 9.f / 16.f);
	}
	else
	{
		CurrentViewSize = DefaultViewSize;
	}
	ApplyViewSize();

	// 뷰어 WBP 루트가 전체화면 캔버스라 그대로 두면 화면 전체가 드래그·입력을 가로챈다.
	// 루트는 자기 히트테스트 off(입력 통과), 이미지만 히트테스트 on → 이미지 영역에서만 드래그 시작.
	if (UWidget* Root = GetRootWidget())
	{
		Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	if (Img_View)
	{
		Img_View->SetVisibility(ESlateVisibility::Visible);
	}
}

void UCameraViewerWidget::ApplyViewSize()
{
	if (!Img_View)
	{
		return;
	}
	// 캔버스 패널 슬롯은 DesiredSizeOverride를 무시하고 슬롯 크기를 사용하므로 슬롯 크기를 직접 지정.
	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Img_View->Slot))
	{
		CanvasSlot->SetSize(CurrentViewSize);
	}
	else
	{
		Img_View->SetDesiredSizeOverride(CurrentViewSize);
	}
}

FReply UCameraViewerWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && Img_View)
	{
		// 클릭이 이미지 영역 안일 때만 드래그 시작(밖은 통과 → 3D 뷰포트로).
		const FGeometry ImgGeo = Img_View->GetCachedGeometry();
		const FVector2D InImg = ImgGeo.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		const FVector2D ImgSize = ImgGeo.GetLocalSize();
		const bool bInsideImg = InImg.X >= 0.f && InImg.Y >= 0.f && InImg.X <= ImgSize.X && InImg.Y <= ImgSize.Y;
		if (bInsideImg)
		{
			DragStartLocal = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
			DragStartTranslation = ViewerTranslation;
			DragStartSize = CurrentViewSize;
			if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Img_View->Slot))
			{
				DragStartSlotPos = CanvasSlot->GetPosition();
			}

			// 어느 변/코너든 근접하면 리사이즈, 아니면 이동.
			ComputeViewerEdges(InImg, ImgSize, ResizeCornerZone, bEdgeL, bEdgeR, bEdgeT, bEdgeB);
			bResizingViewer = (bEdgeL || bEdgeR || bEdgeT || bEdgeB);
			bMovingViewer = !bResizingViewer;

			// 마우스 캡처 + 입력 소비 → 게임 화면제어(카메라 회전 등)로 전파 방지(R1).
			return FReply::Handled().CaptureMouse(TakeWidget());
		}
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UCameraViewerWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bMovingViewer || bResizingViewer)
	{
		const FVector2D Now = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		const FVector2D Delta = Now - DragStartLocal;

		if (bMovingViewer)
		{
			if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Img_View->Slot))
			{
				CanvasSlot->SetPosition(DragStartSlotPos + Delta);
			}
			else if (Img_View)
			{
				ViewerTranslation = DragStartTranslation + Delta;
				Img_View->SetRenderTranslation(ViewerTranslation);
			}
		}
		else // 리사이즈: 잡은 변 기준 폭 변화, 16:9 유지(R2).
		{
			float DeltaW = 0.f;
			if (bEdgeR)      DeltaW = Delta.X;
			else if (bEdgeL) DeltaW = -Delta.X;
			else if (bEdgeB) DeltaW = Delta.Y * (16.f / 9.f);
			else if (bEdgeT) DeltaW = -Delta.Y * (16.f / 9.f);
			const float NewW = FMath::Clamp(DragStartSize.X + DeltaW, MinViewWidth, MaxViewWidth);
			CurrentViewSize = FVector2D(NewW, NewW * 9.f / 16.f);
			ApplyViewSize();
		}
		return FReply::Handled();
	}
	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

FReply UCameraViewerWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if ((bMovingViewer || bResizingViewer) && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		const bool bWasResizing = bResizingViewer;
		bMovingViewer = false;
		bResizingViewer = false;
		// 리사이즈로 크기가 바뀐 경우에만 저장(이동은 크기 무관 → 저장 안 함).
		if (bWasResizing)
		{
			SaveSizeConfigToPath(GetSizeConfigPath(), CurrentViewSize);
		}
		return FReply::Handled().ReleaseMouseCapture();
	}
	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

FCursorReply UCameraViewerWidget::NativeOnCursorQuery(const FGeometry& InGeometry, const FPointerEvent& InCursorEvent)
{
	// 이미지 우하단 코너 위: 리사이즈 커서. 이미지 본체 위: 이동 커서. 이미지 밖: 미처리(통과).
	if (Img_View)
	{
		const FGeometry ImgGeo = Img_View->GetCachedGeometry();
		const FVector2D L = ImgGeo.AbsoluteToLocal(InCursorEvent.GetScreenSpacePosition());
		const FVector2D S = ImgGeo.GetLocalSize();
		if (L.X >= 0.f && L.Y >= 0.f && L.X <= S.X && L.Y <= S.Y)
		{
			bool bL, bR, bT, bB;
			ComputeViewerEdges(L, S, ResizeCornerZone, bL, bR, bT, bB);
			if ((bL && bT) || (bR && bB)) return FCursorReply::Cursor(EMouseCursor::ResizeSouthEast); // ↘↖
			if ((bR && bT) || (bL && bB)) return FCursorReply::Cursor(EMouseCursor::ResizeSouthWest); // ↙↗
			if (bL || bR)                 return FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);  // ↔
			if (bT || bB)                 return FCursorReply::Cursor(EMouseCursor::ResizeUpDown);     // ↕
			return FCursorReply::Cursor(EMouseCursor::CardinalCross);                                 // 본체=이동
		}
	}
	return FCursorReply::Unhandled();
}

int32 UCameraViewerWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	// 이미지 사각형 둘레에 외곽 프레임(구멍난 사각형)을 라인으로 그린다. 이미지 지오메트리를 쓰므로 이동·리사이즈 자동 추종.
	if (Img_View && FrameThickness > 0.f)
	{
		const FGeometry ImgGeo = Img_View->GetCachedGeometry();
		const FVector2D Size = ImgGeo.GetLocalSize();
		if (Size.X > 0.f && Size.Y > 0.f)
		{
			TArray<FVector2f> Pts;
			Pts.Add(FVector2f(0.f, 0.f));
			Pts.Add(FVector2f((float)Size.X, 0.f));
			Pts.Add(FVector2f((float)Size.X, (float)Size.Y));
			Pts.Add(FVector2f(0.f, (float)Size.Y));
			Pts.Add(FVector2f(0.f, 0.f));
			FSlateDrawElement::MakeLines(OutDrawElements, (uint32)(Layer + 1), ImgGeo.ToPaintGeometry(), Pts,
				ESlateDrawEffect::None, FrameColor, true, FrameThickness);
		}
	}
	return Layer + 1;
}

void UCameraViewerWidget::SetRenderTarget(UTextureRenderTarget2D* RT)
{
	if (!Img_View || !RT)
	{
		return;
	}
	// UImage에는 SetBrushFromTextureRenderTarget2D가 없다. RenderTarget(UTexture)을
	// Slate 브러시 리소스로 직접 지정한다(SetBrushFromTexture는 UTexture2D만 받으므로 불가).
	FSlateBrush Brush;
	Brush.SetResourceObject(RT);
	Brush.ImageSize = FVector2D(RT->SizeX, RT->SizeY);
	Img_View->SetBrush(Brush);
}

// ===== 표시 크기 영속화 (UCameraControlLibrary::Save/LoadToJson 와 동일 패턴) =====
FString UCameraViewerWidget::GetSizeConfigPath()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CameraViewer"), TEXT("ViewerSize.json"));
}

bool UCameraViewerWidget::SaveSizeConfigToPath(const FString& Path, const FVector2D& Size)
{
	FCameraViewerSizeConfig Cfg;
	Cfg.width = (float)Size.X;
	Cfg.height = (float)Size.Y;

	FString Json;
	if (!FJsonObjectConverter::UStructToJsonObjectString(Cfg, Json))
	{
		UE_LOG(LogTemp, Warning, TEXT("[CameraViewer] 크기 JSON 직렬화 실패"));
		return false;
	}
	if (!FFileHelper::SaveStringToFile(Json, *Path))
	{
		UE_LOG(LogTemp, Warning, TEXT("[CameraViewer] 크기 파일 쓰기 실패: %s"), *Path);
		return false;
	}
	UE_LOG(LogTemp, Log, TEXT("[CameraViewer] 표시 크기 저장 %.0fx%.0f → %s"), Cfg.width, Cfg.height, *Path);
	return true;
}

bool UCameraViewerWidget::LoadSizeConfigFromPath(const FString& Path, FVector2D& OutSize)
{
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		return false; // 파일 없음(최초 실행) → 조용히 실패 → 호출부가 기본값 사용.
	}
	FCameraViewerSizeConfig Cfg;
	if (!FJsonObjectConverter::JsonObjectStringToUStruct(Json, &Cfg, 0, 0))
	{
		UE_LOG(LogTemp, Warning, TEXT("[CameraViewer] 크기 JSON 역직렬화 실패: %s"), *Path);
		return false;
	}
	OutSize = FVector2D(Cfg.width, Cfg.height);
	return true;
}
