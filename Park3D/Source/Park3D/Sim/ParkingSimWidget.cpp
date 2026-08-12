// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParkingSimWidget.h"

#include "ParkingSimManager.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace
{
	constexpr float SimTitleFontSize = 16.f;
	constexpr float SimBodyFontSize = 13.f;

	/**
	 * 패널 배경. 기존 패널(프리셋 메이커 등)과 같은 "밝은 회녹색 반투명" 계열로 맞춘 값이다.
	 * 화면 캡처에서 프리셋 메이커 패널이 바닥 위에 sRGB(139,142,127)로 합성되는 것을 재서,
	 * 알파 0.85 기준으로 역산했다. 배경이 밝아지므로 글자색은 검정으로 둔다.
	 */
	const FLinearColor SimPanelColor(0.27f, 0.29f, 0.23f, 0.85f);
	const FSlateColor SimTextColor(FLinearColor::Black);

	/** 버튼 + 가운데 검은 라벨(다른 패널과 같은 규약 — 기본 흰 라벨은 밝은 버튼 위에서 안 보인다). */
	UButton* MakeSimButton(UWidgetTree* Tree, const TCHAR* Name, const FText& Label)
	{
		UButton* B = Tree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		UTextBlock* T = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		T->SetText(Label);
		T->SetJustification(ETextJustify::Center);
		T->SetFontSize(SimBodyFontSize);
		T->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
		B->AddChild(T);
		return B;
	}
}

TSharedRef<SWidget> UParkingSimWidget::RebuildWidget()
{
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("SimWidgetTree"), RF_Transient);
	}
	if (!WidgetTree->RootWidget)
	{
		BuildUI();
	}
	return Super::RebuildWidget();
}

void UParkingSimWidget::BuildUI()
{
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("SimRootCanvas"));
	WidgetTree->RootWidget = Canvas;

	RootBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SimPanel"));
	UBorder* Panel = RootBorder;
	Panel->SetBrushColor(SimPanelColor);
	Panel->SetPadding(FMargin(10));

	// 좌하단이 기본 자리(카메라 뷰어·메인 메뉴와 겹치지 않는다). 드래그로 옮길 수 있다.
	if (UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(Canvas->AddChild(Panel)))
	{
		CS->SetAnchors(FAnchors(0.f, 1.f));
		CS->SetAlignment(FVector2D(0.f, 1.f));
		CS->SetPosition(FVector2D(20.f, -20.f));
		CS->SetSize(FVector2D(520.f, 132.f));   // 버튼 4개(입차·출차·리플레이·정지)가 들어간다.
		CS->SetAutoSize(false);
	}

	UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Panel->AddChild(Box);

	UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Title->SetText(FText::FromString(TEXT("주차 시뮬레이션")));
	Title->SetFontSize(SimTitleFontSize);
	Title->SetColorAndOpacity(SimTextColor);
	Box->AddChild(Title);

	StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SimStatusText"));
	StatusText->SetFontSize(SimBodyFontSize);
	StatusText->SetColorAndOpacity(SimTextColor);
	StatusText->SetText(FText::FromString(TEXT("상태: 대기")));
	if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(Box->AddChild(StatusText)))
	{
		S->SetPadding(FMargin(0, 4, 0, 0));
	}

	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		Btn_Start = MakeSimButton(WidgetTree, TEXT("Btn_SimStart"), FText::FromString(TEXT("입차 (F9)")));
		Btn_Exit = MakeSimButton(WidgetTree, TEXT("Btn_SimExit"), FText::FromString(TEXT("출차 (F8)")));
		Btn_Replay = MakeSimButton(WidgetTree, TEXT("Btn_SimReplay"), FText::FromString(TEXT("리플레이 (F10)")));
		Btn_Stop = MakeSimButton(WidgetTree, TEXT("Btn_SimStop"), FText::FromString(TEXT("정지")));

		for (UButton* B : { Btn_Start.Get(), Btn_Exit.Get(), Btn_Replay.Get(), Btn_Stop.Get() })
		{
			if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(Row->AddChild(B)))
			{
				S->SetPadding(FMargin(0, 0, 6, 0));
				S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			}
		}
		if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(Box->AddChild(Row)))
		{
			S->SetPadding(FMargin(0, 8, 0, 4));
		}
	}

	MessageText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SimMessageText"));
	MessageText->SetFontSize(SimBodyFontSize - 1.f);
	MessageText->SetColorAndOpacity(SimTextColor);
	MessageText->SetAutoWrapText(true);
	MessageText->SetText(FText::FromString(TEXT("입차: 입구→랜덤 주차면 / 출차: 랜덤 주차면→출구(도착 시 차량 제거). 여러 번 눌러 동시 주행.")));
	Box->AddChild(MessageText);
}

void UParkingSimWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (Btn_Start)  { Btn_Start->OnClicked.AddUniqueDynamic(this, &UParkingSimWidget::HandleStart); }
	if (Btn_Exit)   { Btn_Exit->OnClicked.AddUniqueDynamic(this, &UParkingSimWidget::HandleExit); }
	if (Btn_Stop)   { Btn_Stop->OnClicked.AddUniqueDynamic(this, &UParkingSimWidget::HandleStop); }
	if (Btn_Replay) { Btn_Replay->OnClicked.AddUniqueDynamic(this, &UParkingSimWidget::HandleReplay); }
}

AParkingSimManager* UParkingSimWidget::SpawnRun()
{
	FString Err;
	AParkingSimManager* Sim = AParkingSimManager::SpawnRun(GetWorld(), Err);
	if (!Sim)
	{
		SetStatus(Err);
	}
	return Sim;
}

void UParkingSimWidget::SetStatus(const FString& Text)
{
	if (MessageText)
	{
		MessageText->SetText(FText::FromString(Text));
	}
}

void UParkingSimWidget::HandleStart()
{
	AParkingSimManager* Sim = SpawnRun();
	if (!Sim)
	{
		return;
	}

	FString Err;
	// HUD/단축키는 전면·후면을 랜덤으로 섞는다(특정 모드가 필요하면 sim.start 의 parkMode 를 쓴다).
	if (!Sim->StartSim(EParkSimDir::Enter, 0, 0, 0, EParkSimParkMode::Random, Err))
	{
		Sim->Destroy();
		SetStatus(Err);
		return;
	}
	const FParkSimRecord& R = Sim->GetRecord();
	SetStatus(FString::Printf(TEXT("입차 시작 #%d — 목표 프리셋 %d / %d번 면 (%s), 입구(%.1f, %.1f)"),
		Sim->GetRunId(), R.presetId, R.slotIndex, *R.parkMode, R.entranceX, R.entranceY));
}

void UParkingSimWidget::HandleExit()
{
	AParkingSimManager* Sim = SpawnRun();
	if (!Sim)
	{
		return;
	}

	FString Err;
	// 요구사항상 기본은 후면주차 상태에서의 출차다(전면/랜덤은 sim.exit 의 parkMode 로).
	if (!Sim->StartSim(EParkSimDir::Exit, 0, 0, 0, EParkSimParkMode::Rear, Err))
	{
		Sim->Destroy();
		SetStatus(Err);
		return;
	}
	const FParkSimRecord& R = Sim->GetRecord();
	SetStatus(FString::Printf(TEXT("출차 시작 #%d — 프리셋 %d / %d번 면 (%s) 에서 출구(%.1f, %.1f)로"),
		Sim->GetRunId(), R.presetId, R.slotIndex, *R.parkMode, R.entranceX, R.entranceY));
}

void UParkingSimWidget::HandleStop()
{
	// 버튼 하나로 개별 주행을 고를 수 없으므로 도는 주행을 전부 세운다(개별 정지는 sim.stop {runId}).
	TArray<AParkingSimManager*> Runs;
	AParkingSimManager::CollectRuns(GetWorld(), Runs);

	int32 Stopped = 0;
	for (AParkingSimManager* R : Runs)
	{
		if (!R->IsBusy()) { continue; }
		R->StopSim(/*bRemoveCar=*/false);
		++Stopped;
	}
	SetStatus(Stopped > 0
		? FString::Printf(TEXT("정지 — 주행 %d건을 세웠습니다."), Stopped)
		: TEXT("정지 — 도는 주행이 없습니다."));
}

void UParkingSimWidget::HandleReplay()
{
	AParkingSimManager* Sim = AParkingSimManager::LatestRun(GetWorld());
	if (!Sim)
	{
		SetStatus(TEXT("재생할 주행이 없습니다 — 먼저 입차/출차를 실행하세요."));
		return;
	}

	FString Err;
	if (!Sim->StartReplay(1.f, Err))
	{
		SetStatus(Err);
		return;
	}
	SetStatus(FString::Printf(TEXT("리플레이 재생 #%d — 프레임 %d개"), Sim->GetRunId(), Sim->GetRecord().frames.Num()));
}

// ===== 패널 드래그 (기존 패널 선례와 동일) =====

FReply UParkingSimWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (RootBorder && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDraggingPanel = true;
		DragStartLocal = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		DragStartTranslation = PanelTranslation;
		return FReply::Handled().CaptureMouse(TakeWidget());
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UParkingSimWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDraggingPanel && RootBorder)
	{
		const FVector2D Now = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		PanelTranslation = DragStartTranslation + (Now - DragStartLocal);
		RootBorder->SetRenderTranslation(PanelTranslation);
		return FReply::Handled();
	}
	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

FReply UParkingSimWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDraggingPanel && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDraggingPanel = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

void UParkingSimWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!StatusText)
	{
		return;
	}
	// 여러 건이 동시에 돌 수 있으므로 "도는 건수 + 가장 최근 주행"을 한 줄에 보여준다.
	AParkingSimManager* Sim = AParkingSimManager::LatestRun(GetWorld());
	if (!Sim)
	{
		StatusText->SetText(FText::FromString(TEXT("상태: 대기   주행 0건")));
		return;
	}

	const FParkSimRecord& R = Sim->GetRecord();
	// 면 번호의 뜻이 방향에 따라 다르다(입차=목표, 출차=출발).
	const FString SlotLabel = R.simMode.IsEmpty() ? TEXT("대상") : R.simMode;
	StatusText->SetText(FText::FromString(FString::Printf(
		TEXT("주행 %d건   #%d %s   경과 %.1fs   이동 %.1fm   %s %d-%d면"),
		AParkingSimManager::CountBusyRuns(GetWorld()), Sim->GetRunId(), *Sim->GetPhaseLabel(),
		Sim->GetElapsed(), Sim->GetDistance(), *SlotLabel, R.presetId, R.slotIndex)));
}
