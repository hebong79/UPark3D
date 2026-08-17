// Copyright Epic Games, Inc. All Rights Reserved.

#include "CameraViewerWidget.h"
#include "CameraControlManager.h"
#include "Components/Image.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/GameplayStatics.h"
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
		// 최초 실행 — 화면 가로의 DefaultViewWidthRatio 만큼으로 시작한다.
		// NativeConstruct 시점에는 루트 지오메트리가 아직 0이므로 첫 틱으로 미룬다.
		CurrentViewSize = DefaultViewSize;
		bPendingDefaultSize = true;
	}
	ApplyViewSize();

	// 뷰어 WBP 루트가 전체화면 캔버스라 그대로 두면 화면 전체가 드래그·입력을 가로챈다.
	// 루트는 자기 히트테스트 off(입력 통과), 이미지만 히트테스트 on → 이미지 영역에서만 드래그 시작.
	if (UWidget* Root = GetRootWidget())
	{
		Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	// 이미지는 접힌 상태로 시작한다. RT 가 없을 때 NativeTick 은 (nullptr == AppliedRT) 라 SetRenderTarget 을
	// 호출하지 않으므로, 여기서 Visible 로 켜두면 브러시 없는 기본 흰 사각형이 그대로 남는다.
	// 첫 틱에 선택 카메라의 RT 가 잡히면 SetRenderTarget 이 Visible 로 되돌린다.
	if (Img_View)
	{
		Img_View->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UCameraViewerWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 저장값이 없던 최초 실행 — 화면 대비 실제 렌더 비율을 재서 목표 비율에 수렴시킨다.
	// 두 지오메트리 모두 tick 공간이라 계수가 같다(크기만 쓰므로 창 위치는 무관).
	// 사용자가 잡아끌기 시작하면 그 즉시 손을 뗀다.
	if (bPendingDefaultSize && (bMovingViewer || bResizingViewer))
	{
		bPendingDefaultSize = false;
	}
	if (bPendingDefaultSize && Img_View)
	{
		const float ImgW  = (float)Img_View->GetCachedGeometry().GetAbsoluteSize().X;
		const float RootW = (float)MyGeometry.GetAbsoluteSize().X;
		if (ImgW > 0.f && RootW > 0.f)
		{
			const float Ratio = ImgW / RootW;
			if (FMath::IsNearlyEqual(Ratio, DefaultViewWidthRatio, DefaultSizeTolerance)
				|| ++DefaultSizeAttempts > DefaultSizeMaxAttempts)
			{
				bPendingDefaultSize = false;
				UE_LOG(LogTemp, Log, TEXT("[CameraViewer] 기본 크기 확정: 화면비 %.4f (목표 %.4f, 보정 %d회, 폭 %.1f)"),
					Ratio, DefaultViewWidthRatio, DefaultSizeAttempts, (float)CurrentViewSize.X);
			}
			else
			{
				const float W = ComputeCorrectedViewWidth(
					(float)CurrentViewSize.X, Ratio, DefaultViewWidthRatio, MinViewWidth, MaxViewWidth);
				CurrentViewSize = FVector2D(W, W * 9.f / 16.f);
				ApplyViewSize();
			}
		}
	}

	// 컨트롤 패널이 뷰어를 갱신해 주던 경로(RefreshViewerBrush)는 패널이 닫히면 멈춘다.
	// 상시 표시가 되려면 뷰어가 스스로 선택 카메라의 렌더타겟을 따라가야 한다.
	// (RPC cam.select/cam.create/cam.delete 로 패널 없이 선택이 바뀌는 경우 포함)
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (!CachedManager.IsValid())
	{
		// 없으면 null 로 두고 다음 틱에 다시 찾는다. 뷰어가 매니저를 스폰하지는 않는다(소유 역전 방지).
		CachedManager = Cast<ACameraControlManager>(
			UGameplayStatics::GetActorOfClass(World, ACameraControlManager::StaticClass()));
	}

	ACameraControlManager* Mgr = CachedManager.Get();
	UTextureRenderTarget2D* RT = Mgr ? Mgr->GetSelectedRenderTarget() : nullptr;
	if (RT != AppliedRT.Get())
	{
		SetRenderTarget(RT); // 바뀐 경우에만 브러시 재생성.
	}
}

float UCameraViewerWidget::ComputeCorrectedViewWidth(float CurrentWidth, float CurrentRenderedRatio,
	float TargetRatio, float MinWidth, float MaxWidth)
{
	if (CurrentWidth <= 0.f || CurrentRenderedRatio <= 0.f || TargetRatio <= 0.f)
	{
		return CurrentWidth; // 아직 측정 불가 → 그대로 두고 다음 틱에 재시도.
	}
	// 렌더 폭은 슬롯 폭에 선형 → 목표/현재 비율만큼 곱하면 한 번에 맞는다.
	return FMath::Clamp(CurrentWidth * (TargetRatio / CurrentRenderedRatio), MinWidth, MaxWidth);
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
	// 반드시 paint 공간 지오메트리를 쓴다. GetCachedGeometry()는 tick 공간(=데스크톱 좌표)이라
	// 그리기(윈도우 좌표)와 공간이 다르고, 창 위치·DPI 배율만큼 프레임이 어긋난다.
	// 에디터 PIE 에서는 두 공간이 거의 일치해 드러나지 않고, 독립 실행/패키지에서만 크게 밀린다.
	if (Img_View && FrameThickness > 0.f)
	{
		const FGeometry ImgGeo = Img_View->GetPaintSpaceGeometry();
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

			// 화면 한가운데 십자 조준선 — 카메라가 어디를 겨누는지 눈으로 잡기 위한 것.
			// 위젯을 얹지 않고 여기서 그리는 이유: Img_View 를 다른 패널로 옮기면
			// ApplyViewSize/드래그가 쓰는 CanvasPanelSlot 캐스팅이 깨진다.
			if (CrossThickness > 0.f && CrossLength > 0.f)
			{
				// 흰 선 하나만 그리면 밝은 노면 위에서 묻힌다(실측으로 확인).
				// 어두운 테두리를 먼저 굵게 깔고 그 위에 밝은 심을 얹어 어느 배경에서도 보이게 한다.
				const FLinearColor Outline(0.f, 0.f, 0.f, 0.55f);
				const FLinearColor Core(1.f, 1.f, 1.f, 0.95f);
				const float Cx = (float)Size.X * 0.5f;
				const float Cy = (float)Size.Y * 0.5f;
				const float Half = FMath::Min(CrossLength, (float)FMath::Min(Size.X, Size.Y) * 0.4f) * 0.5f;

				TArray<FVector2f> H;
				H.Add(FVector2f(Cx - Half, Cy));
				H.Add(FVector2f(Cx + Half, Cy));
				TArray<FVector2f> V;
				V.Add(FVector2f(Cx, Cy - Half));
				V.Add(FVector2f(Cx, Cy + Half));

				for (const TArray<FVector2f>* Line : { &H, &V })
				{
					FSlateDrawElement::MakeLines(OutDrawElements, (uint32)(Layer + 1), ImgGeo.ToPaintGeometry(),
						*Line, ESlateDrawEffect::None, Outline, true, CrossThickness + 2.f);
					FSlateDrawElement::MakeLines(OutDrawElements, (uint32)(Layer + 1), ImgGeo.ToPaintGeometry(),
						*Line, ESlateDrawEffect::None, Core, true, CrossThickness);
				}
			}
		}
	}
	return Layer + 1;
}

void UCameraViewerWidget::SetRenderTarget(UTextureRenderTarget2D* RT)
{
	if (!Img_View)
	{
		return;
	}
	if (!RT)
	{
		// 표시할 카메라가 없다(카메라 0대 등). 빈 사각형·외곽 프레임이 남지 않도록 이미지를 접는다.
		// NativePaint 의 외곽 프레임은 Img_View 지오메트리 크기로 그리므로 Collapsed 면 함께 사라진다.
		Img_View->SetVisibility(ESlateVisibility::Collapsed);
		AppliedRT = nullptr;
		return;
	}
	// UImage에는 SetBrushFromTextureRenderTarget2D가 없다. RenderTarget(UTexture)을
	// Slate 브러시 리소스로 직접 지정한다(SetBrushFromTexture는 UTexture2D만 받으므로 불가).
	FSlateBrush Brush;
	Brush.SetResourceObject(RT);
	Brush.ImageSize = FVector2D(RT->SizeX, RT->SizeY);
	Img_View->SetBrush(Brush);
	Img_View->SetVisibility(ESlateVisibility::Visible);
	AppliedRT = RT;
	// SetBrush 가 슬롯 크기를 건드리지는 않지만, 최초 표시 복원 경로와 동일한 크기를 보장한다.
	ApplyViewSize();
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
	// 인코딩을 명시하지 않으면 AutoDetect 가 비-ASCII 한 글자에도 UTF-16 으로 쓴다.
	if (!FFileHelper::SaveStringToFile(Json, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
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
