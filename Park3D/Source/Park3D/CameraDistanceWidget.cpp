// Copyright Epic Games, Inc. All Rights Reserved.

#include "CameraDistanceWidget.h"
#include "CameraControlManager.h"
#include "CameraControlLibrary.h"
#include "PTZCameraActor.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/Border.h"
#include "Components/Spacer.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"

void UCameraDistanceWidget::SetCameraManager(ACameraControlManager* InManager) { CameraManager = InManager; }
void UCameraDistanceWidget::SetParentDialogRect(const FVector2D& P,const FVector2D& S)
{
	// 여기서 되돌아가면 ApplyDialogPosition 이 안 돌아 창이 투명(RenderOpacity 0)인 채로 남는다
	// — 화면에는 아무것도 없고 로그도 없어 "안 열렸다"로 보인다. 그래서 되돌아간 이유를 1회 남긴다.
	if (bUserMovedDialog || S.X <= 1.f || S.Y <= 1.f)
	{
		if (!bLoggedParentRectSkip)
		{
			bLoggedParentRectSkip = true;
			UE_LOG(LogTemp, Warning, TEXT("[CameraDistance] 부모 사각형 무시: 사용자이동=%d 크기=%.1fx%.1f → 창이 투명하게 남습니다"),
				bUserMovedDialog ? 1 : 0, S.X, S.Y);
		}
		return;
	}
	ParentScreenPosition=P; ParentScreenSize=S; ApplyDialogPosition();
}

TSharedRef<SWidget> UCameraDistanceWidget::RebuildWidget()
{
	// 위젯 트리는 Slate 를 만들기 "전에" 채워야 한다.
	// NativeConstruct 는 Slate 가 만들어진 뒤에 불리므로, 거기서 WidgetTree->RootWidget 을 채우면
	// 이미 빈 트리로 만들어진 Slate 에는 반영되지 않는다(창이 뷰포트에 있는데 geometry 가 0x0 인 원인).
	BuildDialog();
	return Super::RebuildWidget();
}

void UCameraDistanceWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuildDialog(); // 안전망(이미 만들어졌으면 즉시 반환).
}

void UCameraDistanceWidget::BuildDialog()
{
	if (Btn_Line) return;
	const FLinearColor PanelBg(.716f,.716f,.716f,.97f), TextPrimary(.02f,.02f,.02f,1.f);
	const FLinearColor BtnBg(.82f,.82f,.82f,1.f), CloseBg(.776f,.144f,.144f,1.f), CellBg(1.f,1.f,1.f,1.f);

	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
	WidgetTree->RootWidget = Canvas;

	// 폭은 부모(카메라 컨트롤) 폭에 맞춰 고정, 높이는 콘텐츠에 맡긴다(AutoSize). → 좌우 정렬 + 튀어나옴 방지.
	RootSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	RootSizeBox->SetWidthOverride(DialogWidth);
	Canvas->AddChild(RootSizeBox);
	UCanvasPanelSlot* DialogSlot = Cast<UCanvasPanelSlot>(RootSizeBox->Slot);
	DialogSlot->SetAnchors(FAnchors(0.f,0.f)); DialogSlot->SetAlignment(FVector2D::ZeroVector); DialogSlot->SetAutoSize(true);

	UBorder* Border = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Border->SetPadding(FMargin(10.f)); Border->SetBrushColor(PanelBg);
	RootSizeBox->SetContent(Border);
	UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Border->SetContent(Box);

	auto Text = [this,TextPrimary](const FString& S,int32 Size,bool bBold,ETextJustify::Type J)
	{
		UTextBlock* W=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		W->SetText(FText::FromString(S)); W->SetColorAndOpacity(FSlateColor(TextPrimary));
		FSlateFontInfo F=W->GetFont(); F.Size=Size; F.TypefaceFontName=bBold?TEXT("Bold"):TEXT("Regular"); W->SetFont(F);
		W->SetJustification(J); return W;
	};
	auto Button = [this,Text](const FString& S,const FLinearColor& Color,int32 FontSize)
	{
		UButton* W=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		W->SetBackgroundColor(Color); W->SetContent(Text(S,FontSize,false,ETextJustify::Center)); return W;
	};

	// 1) 제목줄: [제목 …채움…][X] — X를 우측 끝으로.
	UHorizontalBox* TitleRow=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	UTextBlock* Title=Text(TEXT("카메라 측정 (카메라와 거리)"),14,true,ETextJustify::Left);
	if (UHorizontalBoxSlot* TS=TitleRow->AddChildToHorizontalBox(Title)) { TS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); TS->SetVerticalAlignment(VAlign_Center); }
	UButton* Close=Button(TEXT("X"),CloseBg,12);
	if (UHorizontalBoxSlot* CS=TitleRow->AddChildToHorizontalBox(Close)) { CS->SetHorizontalAlignment(HAlign_Right); CS->SetVerticalAlignment(VAlign_Center); }
	if (UVerticalBoxSlot* S=Box->AddChildToVerticalBox(TitleRow)) S->SetPadding(FMargin(0,0,0,6));

	// 2) 안내 + 라인 좌표.
	if (UVerticalBoxSlot* S=Box->AddChildToVerticalBox(Text(TEXT("시작점과 끝점 설정"),11,false,ETextJustify::Left))) S->SetPadding(FMargin(0,0,0,2));
	Txt_Line=Text(TEXT("라인 좌표: --"),11,false,ETextJustify::Left); Txt_Line->SetAutoWrapText(true);
	if (UVerticalBoxSlot* S=Box->AddChildToVerticalBox(Txt_Line)) S->SetPadding(FMargin(0,0,0,8));

	// 3) 버튼 2개(균등 폭).
	UHorizontalBox* BtnRow=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Btn_Line=Button(TEXT("타겟라인 설정"),BtnBg,12);
	Btn_Point=Button(TEXT("타겟점 설치"),BtnBg,12);
	if (UHorizontalBoxSlot* S=BtnRow->AddChildToHorizontalBox(Btn_Line)) { S->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); S->SetPadding(FMargin(0,0,3,0)); }
	if (UHorizontalBoxSlot* S=BtnRow->AddChildToHorizontalBox(Btn_Point)) { S->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); S->SetPadding(FMargin(3,0,0,0)); }
	if (UVerticalBoxSlot* S=Box->AddChildToVerticalBox(BtnRow)) S->SetPadding(FMargin(0,0,0,8));

	// 4) 측정값 셀 3개(균등 폭 Fill → 고정폭 제거로 튀어나옴/잘림 방지).
	UHorizontalBox* CellRow=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	auto Cell=[this,Text,CellRow,CellBg](const FString& Head,UTextBlock*& Value,const FMargin& Pad)
	{
		UVerticalBox* V=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		UTextBlock* H=Text(Head,11,false,ETextJustify::Center); H->SetAutoWrapText(true);
		if (UVerticalBoxSlot* HS=V->AddChildToVerticalBox(H)) { HS->SetPadding(FMargin(0,0,0,2)); HS->SetHorizontalAlignment(HAlign_Fill); }
		UBorder* B=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass()); B->SetBrushColor(CellBg); B->SetPadding(FMargin(4,3));
		Value=Text(TEXT("0 m"),13,true,ETextJustify::Center); B->SetContent(Value);
		if (UVerticalBoxSlot* BS=V->AddChildToVerticalBox(B)) BS->SetHorizontalAlignment(HAlign_Fill);
		if (UHorizontalBoxSlot* CS=CellRow->AddChildToHorizontalBox(V)) { CS->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); CS->SetPadding(Pad); }
	};
	Cell(TEXT("거리(3D)"),Txt_Distance,FMargin(0,0,3,0));
	Cell(TEXT("높이"),Txt_Height,FMargin(3,0,3,0));
	Cell(TEXT("각도(수직/수평)"),Txt_Angle,FMargin(3,0,0,0));
	Box->AddChildToVerticalBox(CellRow);

	Btn_Line->OnClicked.AddUniqueDynamic(this, &UCameraDistanceWidget::HandleTargetLine);
	Btn_Point->OnClicked.AddUniqueDynamic(this, &UCameraDistanceWidget::HandleTargetPoint);
	Close->OnClicked.AddUniqueDynamic(this, &UCameraDistanceWidget::HandleClose);
	ApplyDialogPosition();
}

void UCameraDistanceWidget::ApplyDialogPosition()
{
	// 이 함수가 끝까지 가지 못하면 창은 RenderOpacity 0 인 채 남아 "안 열렸다"로 보인다 → 갈래를 1회 남긴다.
	auto LogOnce = [this](const FString& Msg)
	{
		if (!bLoggedPlacement)
		{
			bLoggedPlacement = true;
			UE_LOG(LogTemp, Warning, TEXT("[CameraDistance] %s"), *Msg);
		}
	};
	if(!WidgetTree || !WidgetTree->RootWidget || !GetOwningPlayer()) { LogOnce(TEXT("배치 중단: 위젯트리/루트/오너 없음")); return; }
	UCanvasPanel* C=Cast<UCanvasPanel>(WidgetTree->RootWidget); if(!C||C->GetChildrenCount()==0) { LogOnce(TEXT("배치 중단: 캔버스 루트 비어 있음")); return; }
	UCanvasPanelSlot* S=Cast<UCanvasPanelSlot>(C->GetChildAt(0)->Slot); if(!S) { LogOnce(TEXT("배치 중단: 캔버스 슬롯 아님")); return; }

	// 절대좌표(스크린 px) → 캔버스 로컬좌표(DPI 미적용) 변환. DPI≠1에서 컨트롤 패널과 어긋나던 원인.
	float DPI = UWidgetLayoutLibrary::GetViewportScale(this); if (DPI <= 0.f) DPI = 1.f;

	// 폭을 부모(카메라 컨트롤) 폭에 맞춰 좌우 정렬(로컬 단위).
	DialogWidth = FMath::Max(180.f, ParentScreenSize.X / DPI);
	if (RootSizeBox) RootSizeBox->SetWidthOverride(DialogWidth);

	const FVector2D ParentLocalPos = ParentScreenPosition / DPI;
	const float ParentLocalBottom = (ParentScreenPosition.Y + ParentScreenSize.Y) / DPI;

	int32 VpW=0,VpH=0; GetOwningPlayer()->GetViewportSize(VpW,VpH);
	const float LocalVpW = (float)VpW / DPI, LocalVpH = (float)VpH / DPI;
	const float DialogH = (RootSizeBox && RootSizeBox->GetDesiredSize().Y > 1.f) ? RootSizeBox->GetDesiredSize().Y : 200.f;

	DialogPosition.X = FMath::Clamp(ParentLocalPos.X, 0.f, FMath::Max(0.f, LocalVpW - DialogWidth));
	DialogPosition.Y = FMath::Clamp(ParentLocalBottom + 8.f, 0.f, FMath::Max(0.f, LocalVpH - DialogH));
	S->SetPosition(DialogPosition); SetRenderOpacity(1.f);
	LogOnce(FString::Printf(TEXT("배치 완료: DPI=%.2f 부모=%.0fx%.0f@(%.0f,%.0f) 뷰포트로컬=%.0fx%.0f 창=%.0fx%.0f → (%.0f,%.0f)"),
		DPI, ParentScreenSize.X, ParentScreenSize.Y, ParentScreenPosition.X, ParentScreenPosition.Y,
		LocalVpW, LocalVpH, DialogWidth, DialogH, DialogPosition.X, DialogPosition.Y));
}

FReply UCameraDistanceWidget::NativeOnMouseButtonDown(const FGeometry& G,const FPointerEvent& E){if(E.GetEffectingButton()==EKeys::LeftMouseButton){bDraggingDialog=true;bUserMovedDialog=true;DragStartScreen=E.GetScreenSpacePosition();DragStartPosition=DialogPosition;return FReply::Handled().CaptureMouse(TakeWidget());}return Super::NativeOnMouseButtonDown(G,E);}
FReply UCameraDistanceWidget::NativeOnMouseMove(const FGeometry& G,const FPointerEvent& E){if(bDraggingDialog){float DPI=UWidgetLayoutLibrary::GetViewportScale(this);if(DPI<=0.f)DPI=1.f;DialogPosition=DragStartPosition+(E.GetScreenSpacePosition()-DragStartScreen)/DPI;if(UCanvasPanel* C=Cast<UCanvasPanel>(WidgetTree->RootWidget))if(UCanvasPanelSlot* S=Cast<UCanvasPanelSlot>(C->GetChildAt(0)->Slot))S->SetPosition(DialogPosition);return FReply::Handled();}return Super::NativeOnMouseMove(G,E);}
FReply UCameraDistanceWidget::NativeOnMouseButtonUp(const FGeometry& G,const FPointerEvent& E){if(bDraggingDialog&&E.GetEffectingButton()==EKeys::LeftMouseButton){bDraggingDialog=false;return FReply::Handled().ReleaseMouseCapture();}return Super::NativeOnMouseButtonUp(G,E);}

void UCameraDistanceWidget::SetLabel(UButton* Button, const FString& Value) const
{
	if (Button) if (UTextBlock* Text = Cast<UTextBlock>(Button->GetContent())) Text->SetText(FText::FromString(Value));
}

void UCameraDistanceWidget::HandleTargetLine()
{
	ACameraControlManager* Mgr = CameraManager.Get(); if (!Mgr) return;
	if (TargetLineState != ETargetLineState::None) { if (Mgr->PickMode == EPickMode::TargetLine) Mgr->ReleasePick(); TargetLineState = ETargetLineState::None; bTargetPointPicking = false; bHasTargetPoint = false; SetLabel(Btn_Line, TEXT("타겟라인 설정")); SetLabel(Btn_Point, TEXT("타겟점 설치")); return; }
	if (Mgr->RequestPick(EPickMode::TargetLine)) { TargetLineState = ETargetLineState::WaitStart; bHasTargetPoint = false; SetLabel(Btn_Line, TEXT("시작점 클릭")); }
}

void UCameraDistanceWidget::HandleTargetPoint()
{
	ACameraControlManager* Mgr = CameraManager.Get(); if (!Mgr) return;
	if (bTargetPointPicking) { if (Mgr->PickMode == EPickMode::TargetPoint) Mgr->ReleasePick(); bTargetPointPicking = false; SetLabel(Btn_Point, TEXT("타겟점 설치")); return; }
	if (TargetLineState == ETargetLineState::Done && Mgr->RequestPick(EPickMode::TargetPoint)) { bTargetPointPicking = true; SetLabel(Btn_Point, TEXT("타겟점 설치 중...")); }
}

void UCameraDistanceWidget::HandleClose()
{
	if (ACameraControlManager* Mgr = CameraManager.Get()) if (Mgr->PickMode == EPickMode::TargetLine || Mgr->PickMode == EPickMode::TargetPoint) Mgr->ReleasePick();
	// 뷰포트에서 빼면 다시 넣을 때 geometry 가 0x0 으로 남아 화면에 안 나온다(CLAUDE.md 2026-08-11 카메라 뷰어와 같은 함정).
	// 접기만 하면 부모(카메라 컨트롤)가 다음 틱에 버튼 라벨을 '거리 측정 열기' 로 되돌린다.
	SetVisibility(ESlateVisibility::Collapsed);
}

void UCameraDistanceWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	// 창이 실제로 자리를 잡았는지 1회 남긴다. 크기가 0 이면 그려지지 않는다는 뜻이고,
	// 그 원인은 대개 "화면 최초 레이아웃 뒤에 AddToViewport" 다(EnsureDistanceDialog 주석 참조).
	if (!bLoggedSelfGeometry && RootSizeBox)
	{
		const FVector2D BoxSize = RootSizeBox->GetCachedGeometry().GetAbsoluteSize();
		if (BoxSize.X > 0.f)
		{
			bLoggedSelfGeometry = true;
			UE_LOG(LogTemp, Log, TEXT("[CameraDistance] 창 배치됨: %.0fx%.0f@(%.0f,%.0f)"),
				BoxSize.X, BoxSize.Y,
				RootSizeBox->GetCachedGeometry().GetAbsolutePosition().X,
				RootSizeBox->GetCachedGeometry().GetAbsolutePosition().Y);
		}
	}
	ACameraControlManager* Mgr = CameraManager.Get(); APlayerController* PC = GetOwningPlayer();
	if (Mgr && PC && (PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl)) && PC->WasInputKeyJustPressed(EKeys::LeftMouseButton))
	{
		FVector Hit; if (Mgr->TraceFloor(PC, Hit))
		{
			if (Mgr->PickMode == EPickMode::TargetLine && TargetLineState == ETargetLineState::WaitStart) { LineStart = Hit; TargetLineState = ETargetLineState::WaitEnd; SetLabel(Btn_Line, TEXT("끝점 클릭")); }
			else if (Mgr->PickMode == EPickMode::TargetLine && TargetLineState == ETargetLineState::WaitEnd) { LineEnd = Hit; TargetLineState = ETargetLineState::Done; Mgr->ReleasePick(); SetLabel(Btn_Line, TEXT("라인 완성 (해제)")); }
			else if (Mgr->PickMode == EPickMode::TargetPoint && bTargetPointPicking) { TargetPoint = Hit; bHasTargetPoint = true; }
		}
	}
	DrawVisuals(); UpdateReadout();
}

void UCameraDistanceWidget::UpdateReadout()
{
	ACameraControlManager* Mgr = CameraManager.Get(); APTZCameraActor* Cam = Mgr ? Mgr->GetCamera(Mgr->SelectedIndex) : nullptr; if (!Cam) return;
	if (TargetLineState == ETargetLineState::Done) { float S=0.f,E=0.f; UCameraControlLibrary::TargetLineAngles(Cam->GetActorLocation(), LineStart, LineEnd, LineRef, S, E); Txt_Line->SetText(FText::FromString(FString::Printf(TEXT("시작(%.0f, %.0f)  끝(%.0f, %.0f)   각도 %+.1f° / %+.1f°"), LineStart.X, LineStart.Y, LineEnd.X, LineEnd.Y, S, E))); }
	if (!bHasTargetPoint) return;
	const FVector P = Cam->GetActorLocation(); const float D = UCameraControlLibrary::WorldCentimetersToMeters(UCameraControlLibrary::Distance3D(P, TargetPoint)); const float H = UCameraControlLibrary::WorldCentimetersToMeters(FMath::Abs(P.Z - TargetPoint.Z)); float V=0.f,A=0.f; UCameraControlLibrary::VertHorzAngleToTarget(P, TargetPoint, LineRef, V, A);
	Txt_Distance->SetText(FText::FromString(FString::Printf(TEXT("%.2f m"), D))); Txt_Height->SetText(FText::FromString(FString::Printf(TEXT("%.2f m"), H))); Txt_Angle->SetText(FText::FromString(FString::Printf(TEXT("%+.1f° / %+.1f°"), V, A)));
}

void UCameraDistanceWidget::DrawVisuals() const
{
	UWorld* W = GetWorld(); if (!W) return; constexpr float L=0.12f;
	if (TargetLineState == ETargetLineState::WaitEnd || TargetLineState == ETargetLineState::Done) DrawDebugSphere(W, LineStart, 30.f, 12, FColor::Green, false, L, 0, 2.f);
	if (TargetLineState == ETargetLineState::Done) { DrawDebugSphere(W, LineEnd, 30.f, 12, FColor::Green, false, L, 0, 2.f); FVector D=(LineEnd-LineStart).GetSafeNormal2D(); if (!D.IsNearlyZero()) { const float Z=FMath::Max(LineStart.Z,LineEnd.Z)+5.f; FVector A=LineStart-D*50000.f; FVector B=LineEnd+D*50000.f; A.Z=Z; B.Z=Z; DrawDebugLine(W,A,B,FColor::Green,false,L,0,5.f); DrawDebugSphere(W,FVector(LineRef.X,LineRef.Y,Z),30.f,12,FColor::Blue,false,L,0,2.f); } }
	if (bHasTargetPoint) DrawDebugSphere(W, TargetPoint+FVector(0,0,5),25.f,12,FColor::Yellow,false,L,0,2.f);
}
