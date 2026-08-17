// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderPanelWidget.h"

#include "CarActor.h"
#include "CarColorComponent.h"
#include "CarPlacementManager.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
	/** 콤보 항목 순서 = ERandomResetMode 값 순서. 문구를 바꾸더라도 순서는 지켜야 한다. */
	const TCHAR* ModeLabels[] =
	{
		TEXT("색상만"),
		TEXT("차종 + 색상"),
		TEXT("대수 + 차종 + 색상"),
	};
}

void URenderPanelWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 재-AddToViewport 마다 다시 도므로 중복 바인딩을 먼저 끊는다(다른 패널과 같은 규율).
	if (Btn_Randomize)    { Btn_Randomize->OnClicked.RemoveAll(this);    Btn_Randomize->OnClicked.AddDynamic(this, &URenderPanelWidget::HandleRandomize); }
	if (Btn_ResetColor)   { Btn_ResetColor->OnClicked.RemoveAll(this);   Btn_ResetColor->OnClicked.AddDynamic(this, &URenderPanelWidget::HandleResetColor); }
	if (Btn_HideRandom)   { Btn_HideRandom->OnClicked.RemoveAll(this);   Btn_HideRandom->OnClicked.AddDynamic(this, &URenderPanelWidget::HandleHideRandom); }
	if (Btn_ToggleRandom) { Btn_ToggleRandom->OnClicked.RemoveAll(this); Btn_ToggleRandom->OnClicked.AddDynamic(this, &URenderPanelWidget::HandleToggleRandom); }
	if (Btn_ShowAll)      { Btn_ShowAll->OnClicked.RemoveAll(this);      Btn_ShowAll->OnClicked.AddDynamic(this, &URenderPanelWidget::HandleShowAll); }

	if (Combo_Mode)
	{
		Combo_Mode->ClearOptions();
		for (const TCHAR* L : ModeLabels)
		{
			Combo_Mode->AddOption(L);
		}
		Combo_Mode->SetSelectedIndex(1);   // 차종 + 색상 — 이전 패널의 기본 동작
	}

	if (Field_Count && Field_Count->GetText().IsEmpty())     { Field_Count->SetText(FText::FromString(TEXT("0"))); }
	if (Field_Seed && Field_Seed->GetText().IsEmpty())       { Field_Seed->SetText(FText::FromString(TEXT("0"))); }
	if (Field_HideCount && Field_HideCount->GetText().IsEmpty()) { Field_HideCount->SetText(FText::FromString(TEXT("0"))); }

	// 체크 상태는 위젯이 아니라 월드에서 읽는다 — RPC(car.hideAll)로 바뀐 뒤 패널을 열면
	// 위젯이 들고 있던 값과 어긋난다(차량 배치 패널의 같은 체크박스 선례).
	if (Check_HideAll)
	{
		Check_HideAll->OnCheckStateChanged.RemoveAll(this);
		if (const ACarPlacementManager* Mgr = GetCarManager())
		{
			Check_HideAll->SetIsChecked(Mgr->AreAllCarsHidden());
		}
		Check_HideAll->OnCheckStateChanged.AddDynamic(this, &URenderPanelWidget::HandleHideAllChanged);
	}

	Say(TEXT("대기"));
}

ACarPlacementManager* URenderPanelWidget::GetCarManager() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ACarPlacementManager> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;   // 스폰하지 않는다 — 차량이 없는 상태에서 패널만 열었을 때 매니저를 만들지 않는다.
}

int32 URenderPanelWidget::ReadInt(const UEditableTextBox* Field, int32 Fallback) const
{
	if (!Field)
	{
		return Fallback;
	}
	const FString S = Field->GetText().ToString().TrimStartAndEnd();
	return S.IsNumeric() ? FCString::Atoi(*S) : Fallback;
}

void URenderPanelWidget::Say(const FString& Message)
{
	if (Txt_Status)
	{
		Txt_Status->SetText(FText::FromString(Message));
	}
}

void URenderPanelWidget::HandleRandomize()
{
	ACarPlacementManager* Mgr = GetCarManager();
	if (!Mgr)
	{
		Say(TEXT("차량이 없습니다 — 먼저 차량을 배치하세요."));
		return;
	}

	const int32 ModeIdx = Combo_Mode ? FMath::Clamp(Combo_Mode->GetSelectedIndex(), 0, 2) : 1;
	const ERandomResetMode Mode = static_cast<ERandomResetMode>(ModeIdx);

	// 카탈로그는 매니저의 관문을 그대로 쓴다(테이블이 없으면 car_catalog.json 으로 폴백한다).
	const TArray<FCarPresetEntry> Catalog = ACarPlacementManager::CatalogFromTable(nullptr);
	const int32 Count = ReadInt(Field_Count, 0);
	const int32 Seed = ReadInt(Field_Seed, 0);

	const int32 Affected = Mgr->ResetRandomPlacement(Mode, Catalog, Count, Seed);
	Say(FString::Printf(TEXT("%s 적용 — 차량 %d대"), ModeLabels[ModeIdx], Affected));
}

void URenderPanelWidget::HandleResetColor()
{
	ACarPlacementManager* Mgr = GetCarManager();
	if (!Mgr)
	{
		Say(TEXT("차량이 없습니다."));
		return;
	}

	int32 N = 0;
	for (const TObjectPtr<ACarActor>& Car : Mgr->GetCars())
	{
		if (Car && Car->ColorComp)
		{
			Car->ColorComp->ResetColor();
			++N;
		}
	}
	Say(FString::Printf(TEXT("도색 원복 — 차량 %d대"), N));
}

void URenderPanelWidget::HandleHideRandom()
{
	ACarPlacementManager* Mgr = GetCarManager();
	if (!Mgr)
	{
		Say(TEXT("차량이 없습니다."));
		return;
	}
	const TArray<ACarActor*> Hidden = Mgr->HideRandomCars(ReadInt(Field_HideCount, 0), ReadInt(Field_Seed, 0));
	Say(FString::Printf(TEXT("무작위 숨김 — %d대"), Hidden.Num()));
	if (Check_HideAll) { Check_HideAll->SetIsChecked(Mgr->AreAllCarsHidden()); }
}

void URenderPanelWidget::HandleToggleRandom()
{
	ACarPlacementManager* Mgr = GetCarManager();
	if (!Mgr)
	{
		Say(TEXT("차량이 없습니다."));
		return;
	}
	const TArray<ACarActor*> Toggled = Mgr->ToggleRandomCars(ReadInt(Field_HideCount, 0), ReadInt(Field_Seed, 0));
	Say(FString::Printf(TEXT("표시 반전 — %d대"), Toggled.Num()));
	if (Check_HideAll) { Check_HideAll->SetIsChecked(Mgr->AreAllCarsHidden()); }
}

void URenderPanelWidget::HandleShowAll()
{
	ACarPlacementManager* Mgr = GetCarManager();
	if (!Mgr)
	{
		Say(TEXT("차량이 없습니다."));
		return;
	}
	const int32 N = Mgr->SetAllCarsHidden(false);
	Say(FString::Printf(TEXT("전체 표시 — %d대"), N));
	if (Check_HideAll) { Check_HideAll->SetIsChecked(false); }
}

void URenderPanelWidget::HandleHideAllChanged(bool bIsChecked)
{
	ACarPlacementManager* Mgr = GetCarManager();
	if (!Mgr)
	{
		Say(TEXT("차량이 없습니다."));
		return;
	}
	const int32 N = Mgr->SetAllCarsHidden(bIsChecked);
	Say(FString::Printf(TEXT("%s — %d대"), bIsChecked ? TEXT("전체 숨김") : TEXT("전체 표시"), N));
}

//======================================================================================
// 드래그 — UCarPlacementWidget 과 같은 방식(루트 보더에 렌더 이동)
//======================================================================================
FReply URenderPanelWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
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

FReply URenderPanelWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
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

FReply URenderPanelWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDraggingPanel && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDraggingPanel = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}
