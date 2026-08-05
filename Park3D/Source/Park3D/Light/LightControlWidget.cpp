// Copyright Epic Games, Inc. All Rights Reserved.

#include "LightControlWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Engine.h"
#include "LightControlLibrary.h"
#include "LightControlManager.h"
#include "Misc/Paths.h"

#if PARK3D_USE_FILE_DIALOG
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "IDesktopPlatform.h"
#endif

namespace
{
	// Ui 접두: LightControlLibrary.cpp 의 익명 네임스페이스에 같은 이름의 상수가 있어,
	// 유니티 빌드로 두 파일이 한 TU 에 묶이면 재정의 에러가 난다(값은 동일).
	constexpr float UiExposureMin = -5.0f, UiExposureMax = 20.0f;
	constexpr float UiSunIntensityMin = 0.0f, UiSunIntensityMax = 150.0f;
	constexpr float AltitudeMin = 0.0f, AltitudeMax = 90.0f;
	constexpr float AzimuthMin = 0.0f, AzimuthMax = 360.0f;
	constexpr float UiSkyIntensityMin = 0.0f, UiSkyIntensityMax = 20.0f;

	// 열 너비. 라벨/수치를 고정폭으로 잡아야 슬라이더가 남는 폭을 차지하고 서로 겹치지 않는다.
	constexpr float LabelColumnWidth = 150.0f;
	constexpr float ValueColumnWidth = 84.0f;

	// UMG 기본 폰트는 24 라 라벨이 열 너비를 넘어 슬라이더 위로 흘러넘친다. 패널 전용으로 줄인다.
	constexpr float LabelFontSize = 14.0f;
	constexpr float TitleFontSize = 18.0f;
	constexpr float StatusFontSize = 12.0f;

	/** UEditableTextBox 는 SetFont 가 없어 스타일을 통째로 바꿔야 폰트가 적용된다. */
	void SetBoxFontSize(UEditableTextBox* Box, float Size)
	{
		if (!Box)
		{
			return;
		}
		FEditableTextBoxStyle St = Box->GetWidgetStyle();
		St.TextStyle.Font.Size = Size;
		Box->SetWidgetStyle(St);
	}

	FString Fmt(float V) { return FString::Printf(TEXT("%.2f"), V); }

	float ParseOr(const UEditableTextBox* Box, float Fallback)
	{
		if (!Box)
		{
			return Fallback;
		}
		const FString S = Box->GetText().ToString().TrimStartAndEnd();
		return S.IsNumeric() || S.Contains(TEXT(".")) || S.Contains(TEXT("-"))
			? FCString::Atof(*S)
			: Fallback;
	}

	/** 버튼 + 가운데 라벨. */
	UButton* MakeButton(UWidgetTree* Tree, const TCHAR* Name, const FText& Label)
	{
		UButton* B = Tree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		UTextBlock* T = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		T->SetText(Label);
		T->SetJustification(ETextJustify::Center);
		T->SetFontSize(LabelFontSize);
		B->AddChild(T);
		return B;
	}
}

// ===== 위젯 트리 구성 =====

TSharedRef<SWidget> ULightControlWidget::RebuildWidget()
{
	// WBP 가 없으므로 WidgetTree 가 비어 있을 수 있다 — 직접 만든다.
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("LightWidgetTree"), RF_Transient);
	}
	if (!WidgetTree->RootWidget)
	{
		BuildUI();
	}
	return Super::RebuildWidget();
}

void ULightControlWidget::AddSliderRow(UVerticalBox* Parent, const FText& Label, float Min, float Max,
	TObjectPtr<USlider>& OutSlider, TObjectPtr<UEditableTextBox>& OutField)
{
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

	// 라벨은 고정폭 상자에 넣어 열을 맞춘다. Fill 로 두면 슬라이더 자리를 잠식해 겹친다.
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Text->SetText(Label);
	Text->SetFontSize(LabelFontSize);
	USizeBox* LabelBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	LabelBox->SetWidthOverride(LabelColumnWidth);
	LabelBox->AddChild(Text);
	if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(Row->AddChild(LabelBox)))
	{
		S->SetPadding(FMargin(0, 0, 8, 0));
		S->SetVerticalAlignment(VAlign_Center);
		S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	}

	OutSlider = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass());
	OutSlider->SetMinValue(Min);
	OutSlider->SetMaxValue(Max);
	if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(Row->AddChild(OutSlider)))
	{
		S->SetPadding(FMargin(0, 0, 8, 0));
		S->SetVerticalAlignment(VAlign_Center);
		S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	OutField = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass());
	SetBoxFontSize(OutField, LabelFontSize);
	USizeBox* FieldBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	FieldBox->SetWidthOverride(ValueColumnWidth);
	FieldBox->AddChild(OutField);
	if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(Row->AddChild(FieldBox)))
	{
		S->SetVerticalAlignment(VAlign_Center);
		S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	}

	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Parent->AddChild(Row)))
	{
		VS->SetPadding(FMargin(0, 4));
	}
}

void ULightControlWidget::BuildUI()
{
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
	WidgetTree->RootWidget = Canvas;

	RootBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("RootBorder"));
	RootBorder->SetBrushColor(FLinearColor(0.06f, 0.06f, 0.07f, 0.94f));
	RootBorder->SetPadding(FMargin(10));

	if (UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(Canvas->AddChild(RootBorder)))
	{
		CS->SetAnchors(FAnchors(0.f, 0.f));
		CS->SetAlignment(FVector2D(0.f, 0.f));
		CS->SetPosition(FVector2D(320.f, 40.f));
		CS->SetSize(FVector2D(560.f, 380.f));
		CS->SetAutoSize(false);
	}

	UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	RootBorder->AddChild(Box);

	UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Title->SetText(FText::FromString(TEXT("조명 설정")));
	Title->SetFontSize(TitleFontSize);
	if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(Box->AddChild(Title)))
	{
		S->SetPadding(FMargin(0, 0, 0, 8));
		S->SetHorizontalAlignment(HAlign_Center);
	}

	AddSliderRow(Box, FText::FromString(TEXT("노출 (EV100)")), UiExposureMin, UiExposureMax, Slider_Exposure, Field_Exposure);
	AddSliderRow(Box, FText::FromString(TEXT("태양 광량 (lux)")), UiSunIntensityMin, UiSunIntensityMax, Slider_SunIntensity, Field_SunIntensity);
	AddSliderRow(Box, FText::FromString(TEXT("태양 고도 (도)")), AltitudeMin, AltitudeMax, Slider_Altitude, Field_Altitude);
	AddSliderRow(Box, FText::FromString(TEXT("태양 방위 (도)")), AzimuthMin, AzimuthMax, Slider_Azimuth, Field_Azimuth);
	AddSliderRow(Box, FText::FromString(TEXT("하늘빛 광량")), UiSkyIntensityMin, UiSkyIntensityMax, Slider_SkyIntensity, Field_SkyIntensity);

	// 태양 색 (R/G/B 0~1)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Text->SetText(FText::FromString(TEXT("태양 색 (R/G/B)")));
		Text->SetFontSize(LabelFontSize);
		USizeBox* LabelBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		LabelBox->SetWidthOverride(LabelColumnWidth);
		LabelBox->AddChild(Text);
		if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(Row->AddChild(LabelBox)))
		{
			S->SetPadding(FMargin(0, 0, 8, 0));
			S->SetVerticalAlignment(VAlign_Center);
			S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		}

		TObjectPtr<UEditableTextBox>* Targets[] = { &Field_ColorR, &Field_ColorG, &Field_ColorB };
		for (TObjectPtr<UEditableTextBox>* Target : Targets)
		{
			*Target = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass());
			SetBoxFontSize(*Target, LabelFontSize);
			if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(Row->AddChild(*Target)))
			{
				S->SetPadding(FMargin(0, 0, 4, 0));
				S->SetVerticalAlignment(VAlign_Center);
				S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			}
		}

		if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Box->AddChild(Row)))
		{
			VS->SetPadding(FMargin(0, 3));
		}
	}

	// 버튼 줄
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		Btn_Apply = MakeButton(WidgetTree, TEXT("Btn_Apply"), FText::FromString(TEXT("적용")));
		Btn_Save = MakeButton(WidgetTree, TEXT("Btn_Save"), FText::FromString(TEXT("저장")));
		Btn_Open = MakeButton(WidgetTree, TEXT("Btn_Open"), FText::FromString(TEXT("열기")));

		for (UButton* B : { Btn_Apply.Get(), Btn_Save.Get(), Btn_Open.Get() })
		{
			if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(Row->AddChild(B)))
			{
				S->SetPadding(FMargin(0, 0, 6, 0));
				S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			}
		}

		if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Box->AddChild(Row)))
		{
			VS->SetPadding(FMargin(0, 10, 0, 4));
		}
	}

	StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StatusText"));
	StatusText->SetText(FText::FromString(TEXT("저장하거나 연 파일이 다음 실행의 기본값이 됩니다.")));
	StatusText->SetAutoWrapText(true);   // 긴 경로 안내가 테두리를 벗어나지 않도록
	StatusText->SetFontSize(StatusFontSize);
	Box->AddChild(StatusText);
}

// ===== 초기화 =====

void ULightControlWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (Btn_Apply) { Btn_Apply->OnClicked.AddUniqueDynamic(this, &ULightControlWidget::HandleApply); }
	if (Btn_Save) { Btn_Save->OnClicked.AddUniqueDynamic(this, &ULightControlWidget::HandleSave); }
	if (Btn_Open) { Btn_Open->OnClicked.AddUniqueDynamic(this, &ULightControlWidget::HandleOpen); }

	if (Slider_Exposure) { Slider_Exposure->OnValueChanged.AddUniqueDynamic(this, &ULightControlWidget::HandleExposureSlider); }
	if (Slider_SunIntensity) { Slider_SunIntensity->OnValueChanged.AddUniqueDynamic(this, &ULightControlWidget::HandleSunIntensitySlider); }
	if (Slider_Altitude) { Slider_Altitude->OnValueChanged.AddUniqueDynamic(this, &ULightControlWidget::HandleAltitudeSlider); }
	if (Slider_Azimuth) { Slider_Azimuth->OnValueChanged.AddUniqueDynamic(this, &ULightControlWidget::HandleAzimuthSlider); }
	if (Slider_SkyIntensity) { Slider_SkyIntensity->OnValueChanged.AddUniqueDynamic(this, &ULightControlWidget::HandleSkyIntensitySlider); }

	// 현재 레벨 조명을 그대로 읽어와 시작한다(패널을 열었다고 화면이 바뀌면 안 된다).
	FLightSettings Cur;
	if (ALightControlManager* M = GetManager())
	{
		if (!M->CaptureCurrent(Cur))
		{
			Cur = M->GetLastApplied();
		}
	}
	SetFields(Cur);
}

ALightControlManager* ULightControlWidget::GetManager() const
{
	return ALightControlManager::GetOrSpawn(GetWorld());
}

void ULightControlWidget::Notify(const FString& Msg) const
{
	if (StatusText)
	{
		const_cast<ULightControlWidget*>(this)->StatusText->SetText(FText::FromString(Msg));
	}
	UE_LOG(LogTemp, Log, TEXT("[Light] %s"), *Msg);
}

// ===== 값 <-> 위젯 =====

void ULightControlWidget::SetFields(const FLightSettings& In)
{
	FLightSettings S = In;
	ULightControlLibrary::ClampSettings(S);

	TGuardValue<bool> Guard(bSyncing, true);

	if (Slider_Exposure) { Slider_Exposure->SetValue(S.ExposureEV100); }
	if (Slider_SunIntensity) { Slider_SunIntensity->SetValue(S.SunIntensity); }
	if (Slider_Altitude) { Slider_Altitude->SetValue(S.SunAltitudeDeg); }
	if (Slider_Azimuth) { Slider_Azimuth->SetValue(S.SunAzimuthDeg); }
	if (Slider_SkyIntensity) { Slider_SkyIntensity->SetValue(S.SkyIntensity); }

	if (Field_Exposure) { Field_Exposure->SetText(FText::FromString(Fmt(S.ExposureEV100))); }
	if (Field_SunIntensity) { Field_SunIntensity->SetText(FText::FromString(Fmt(S.SunIntensity))); }
	if (Field_Altitude) { Field_Altitude->SetText(FText::FromString(Fmt(S.SunAltitudeDeg))); }
	if (Field_Azimuth) { Field_Azimuth->SetText(FText::FromString(Fmt(S.SunAzimuthDeg))); }
	if (Field_SkyIntensity) { Field_SkyIntensity->SetText(FText::FromString(Fmt(S.SkyIntensity))); }

	if (Field_ColorR) { Field_ColorR->SetText(FText::FromString(Fmt(S.SunColor.R))); }
	if (Field_ColorG) { Field_ColorG->SetText(FText::FromString(Fmt(S.SunColor.G))); }
	if (Field_ColorB) { Field_ColorB->SetText(FText::FromString(Fmt(S.SunColor.B))); }
}

FLightSettings ULightControlWidget::GetFields() const
{
	FLightSettings S;
	S.ExposureEV100 = ParseOr(Field_Exposure, S.ExposureEV100);
	S.SunIntensity = ParseOr(Field_SunIntensity, S.SunIntensity);
	S.SunAltitudeDeg = ParseOr(Field_Altitude, S.SunAltitudeDeg);
	S.SunAzimuthDeg = ParseOr(Field_Azimuth, S.SunAzimuthDeg);
	S.SkyIntensity = ParseOr(Field_SkyIntensity, S.SkyIntensity);
	S.SunColor = FLinearColor(ParseOr(Field_ColorR, 1.f), ParseOr(Field_ColorG, 1.f), ParseOr(Field_ColorB, 1.f), 1.f);
	ULightControlLibrary::ClampSettings(S);
	return S;
}

// ===== 동작 =====

void ULightControlWidget::ApplyFromFields()
{
	ALightControlManager* M = GetManager();
	if (!M)
	{
		Notify(TEXT("조명 매니저를 만들 수 없습니다."));
		return;
	}
	const FLightSettings S = GetFields();
	M->ApplySettings(S);
	SetFields(S);   // 클램프 결과를 되쓴다
	Notify(TEXT("적용했습니다."));
}

void ULightControlWidget::SaveToFileDialog()
{
	FString Path;
	if (!PromptSaveFilePath(Path))
	{
		return;  // 사용자가 취소
	}

	const FLightSettings S = GetFields();
	if (!ULightControlLibrary::SaveToFile(Path, S))
	{
		Notify(FString::Printf(TEXT("저장 실패: %s"), *Path));
		return;
	}
	ULightControlLibrary::SetDefaultFile(Path);
	Notify(FString::Printf(TEXT("저장 완료(기본값으로 지정): %s"), *FPaths::GetCleanFilename(Path)));
}

void ULightControlWidget::OpenFromFileDialog()
{
	FString Path;
	if (!PromptOpenFilePath(Path))
	{
		return;  // 사용자가 취소
	}

	FLightSettings S;
	if (!ULightControlLibrary::LoadFromFile(Path, S))
	{
		Notify(FString::Printf(TEXT("열기 실패(형식 불일치): %s"), *FPaths::GetCleanFilename(Path)));
		return;
	}

	SetFields(S);
	if (ALightControlManager* M = GetManager())
	{
		M->ApplySettings(S);
	}
	ULightControlLibrary::SetDefaultFile(Path);
	Notify(FString::Printf(TEXT("불러와 적용(기본값으로 지정): %s"), *FPaths::GetCleanFilename(Path)));
}

void ULightControlWidget::HandleApply() { ApplyFromFields(); }
void ULightControlWidget::HandleSave() { SaveToFileDialog(); }
void ULightControlWidget::HandleOpen() { OpenFromFileDialog(); }

// 슬라이더는 끌면서 바로 결과를 보는 게 조명 조절의 핵심이라 즉시 적용한다.
#define PARK3D_SLIDER_HANDLER(FieldPtr)                       \
	if (bSyncing) { return; }                                 \
	if (FieldPtr) { FieldPtr->SetText(FText::FromString(Fmt(V))); } \
	ApplyFromFields();

void ULightControlWidget::HandleExposureSlider(float V) { PARK3D_SLIDER_HANDLER(Field_Exposure) }
void ULightControlWidget::HandleSunIntensitySlider(float V) { PARK3D_SLIDER_HANDLER(Field_SunIntensity) }
void ULightControlWidget::HandleAltitudeSlider(float V) { PARK3D_SLIDER_HANDLER(Field_Altitude) }
void ULightControlWidget::HandleAzimuthSlider(float V) { PARK3D_SLIDER_HANDLER(Field_Azimuth) }
void ULightControlWidget::HandleSkyIntensitySlider(float V) { PARK3D_SLIDER_HANDLER(Field_SkyIntensity) }

#undef PARK3D_SLIDER_HANDLER

// ===== 파일 다이얼로그 (기존 패널과 동일한 부모 핸들 처리) =====

bool ULightControlWidget::PromptOpenFilePath(FString& OutPath) const
{
#if PARK3D_USE_FILE_DIALOG
	IDesktopPlatform* DP = FDesktopPlatformModule::Get();
	if (!DP)
	{
		OutPath = ULightControlLibrary::GetSuggestedFilePath();
		return true;
	}

	// 부모 핸들이 없으면 네이티브 대화상자가 뜨지 않고 조용히 false 를 반환한다.
	const void* ParentHandle = FSlateApplication::IsInitialized()
		? FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr)
		: nullptr;

	TArray<FString> Files;
	if (DP->OpenFileDialog(ParentHandle, TEXT("조명 설정 열기"), ULightControlLibrary::GetLightDir(), TEXT(""),
		TEXT("Light JSON (*.json)|*.json|All Files (*.*)|*.*"), EFileDialogFlags::None, Files) && Files.Num() > 0)
	{
		OutPath = Files[0];
		return true;
	}
	return false;
#else
	OutPath = ULightControlLibrary::GetSuggestedFilePath();
	return true;
#endif
}

bool ULightControlWidget::PromptSaveFilePath(FString& OutPath) const
{
#if PARK3D_USE_FILE_DIALOG
	IDesktopPlatform* DP = FDesktopPlatformModule::Get();
	if (!DP)
	{
		OutPath = ULightControlLibrary::GetSuggestedFilePath();
		return true;
	}

	const void* ParentHandle = FSlateApplication::IsInitialized()
		? FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr)
		: nullptr;

	TArray<FString> Files;
	const FString Suggested = ULightControlLibrary::GetSuggestedFilePath();
	if (DP->SaveFileDialog(ParentHandle, TEXT("조명 설정 저장"), ULightControlLibrary::GetLightDir(),
		FPaths::GetCleanFilename(Suggested),
		TEXT("Light JSON (*.json)|*.json|All Files (*.*)|*.*"), EFileDialogFlags::None, Files) && Files.Num() > 0)
	{
		OutPath = Files[0];
		if (FPaths::GetExtension(OutPath).IsEmpty())
		{
			OutPath += TEXT(".json");
		}
		return true;
	}
	return false;
#else
	OutPath = ULightControlLibrary::GetSuggestedFilePath();
	return true;
#endif
}

// ===== 패널 드래그 (기존 패널 선례와 동일) =====

FReply ULightControlWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
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

FReply ULightControlWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
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

FReply ULightControlWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDraggingPanel && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDraggingPanel = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}
