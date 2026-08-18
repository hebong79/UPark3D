// Copyright Epic Games, Inc. All Rights Reserved.
// LightControlWidget : "조명 설정" 패널. 노출·태양(광량/색/고도/방위)·하늘빛 6항목을 조절하고
// 적용/저장/열기를 제공한다. 저장·열기한 파일은 다음 실행의 기본값이 된다.
//
// 이 패널은 예외적으로 WBP 없이 C++ 에서 위젯 트리를 구성한다.
// 사유: UE5.8 파이썬에 WidgetBlueprint.WidgetTree 가 노출되지 않아 에디터 없이는 WBP 를 만들 수 없다
//       (설계서 lightpanel §6). 나중에 이 클래스를 부모로 하는 WBP 를 만들면 디자이너 재배치가 가능하다.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LightControlTypes.h"
#include "LightControlWidget.generated.h"

class ALightControlManager;
class UBorder;
class UButton;
class UEditableTextBox;
class USlider;
class UTextBlock;
class UVerticalBox;

UCLASS()
class PARK3D_API ULightControlWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 입력란 값을 읽어 레벨 조명에 적용한다(파일은 건드리지 않는다). */
	UFUNCTION(BlueprintCallable, Category = "Light") void ApplyFromFields();

	/** 파일 다이얼로그로 저장하고, 그 파일을 다음 실행 기본값으로 지정한다. */
	UFUNCTION(BlueprintCallable, Category = "Light") void SaveToFileDialog();

	/** 파일 다이얼로그로 불러와 적용하고, 그 파일을 다음 실행 기본값으로 지정한다. */
	UFUNCTION(BlueprintCallable, Category = "Light") void OpenFromFileDialog();

	/** 설정값을 입력란·슬라이더에 반영한다. */
	UFUNCTION(BlueprintCallable, Category = "Light") void SetFields(const FLightSettings& S);

	/** 입력란·슬라이더에서 설정값을 읽는다(클램프 적용). */
	UFUNCTION(BlueprintCallable, Category = "Light") FLightSettings GetFields() const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;

	// 패널 드래그(기존 패널 선례와 동일)
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	UFUNCTION() void HandleApply();
	UFUNCTION() void HandleSave();
	UFUNCTION() void HandleOpen();

	// 슬라이더는 항목마다 별도 핸들러가 필요하다(델리게이트가 값만 전달하고 발신자를 알려주지 않음).
	UFUNCTION() void HandleExposureSlider(float V);
	UFUNCTION() void HandleSunIntensitySlider(float V);
	UFUNCTION() void HandleAltitudeSlider(float V);
	UFUNCTION() void HandleAzimuthSlider(float V);
	UFUNCTION() void HandleSkyIntensitySlider(float V);

private:
	/** 위젯 트리를 C++ 로 구성한다(최초 1회). */
	void BuildUI();

	/** 라벨 + 슬라이더 + 수치입력 한 줄을 만들어 Parent 에 붙인다. */
	void AddSliderRow(UVerticalBox* Parent, const FText& Label, float Min, float Max,
		TObjectPtr<USlider>& OutSlider, TObjectPtr<UEditableTextBox>& OutField);

	ALightControlManager* GetManager() const;
	void Notify(const FString& Msg) const;

	/** 다이얼로그 경로 선택(기존 패널과 동일한 부모 핸들 처리). */
	bool PromptOpenFilePath(FString& OutPath) const;
	bool PromptSaveFilePath(FString& OutPath) const;

	/**
	 * 마지막으로 열거나 저장한 파일의 전체 경로. 저장 대화상자의 기본 파일명·폴더로 쓴다 —
	 * 열어서 고친 파일을 저장할 때 고정 기본명(LightSettings.json)이 뜨면 매번 다시 골라야 한다.
	 */
	FString CurFilePath;

	// ---- 구성 요소 ----
	UPROPERTY(Transient) TObjectPtr<UBorder> RootBorder = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText = nullptr;

	UPROPERTY(Transient) TObjectPtr<USlider> Slider_Exposure = nullptr;
	UPROPERTY(Transient) TObjectPtr<USlider> Slider_SunIntensity = nullptr;
	UPROPERTY(Transient) TObjectPtr<USlider> Slider_Altitude = nullptr;
	UPROPERTY(Transient) TObjectPtr<USlider> Slider_Azimuth = nullptr;
	UPROPERTY(Transient) TObjectPtr<USlider> Slider_SkyIntensity = nullptr;

	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> Field_Exposure = nullptr;
	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> Field_SunIntensity = nullptr;
	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> Field_Altitude = nullptr;
	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> Field_Azimuth = nullptr;
	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> Field_SkyIntensity = nullptr;

	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> Field_ColorR = nullptr;
	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> Field_ColorG = nullptr;
	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> Field_ColorB = nullptr;

	UPROPERTY(Transient) TObjectPtr<UButton> Btn_Apply = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton> Btn_Save = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton> Btn_Open = nullptr;

	/** 슬라이더가 입력란을 갱신하는 동안 되먹임을 막는다. */
	bool bSyncing = false;

	// 드래그 상태
	bool bDraggingPanel = false;
	FVector2D DragStartLocal = FVector2D::ZeroVector;
	FVector2D DragStartTranslation = FVector2D::ZeroVector;
	FVector2D PanelTranslation = FVector2D::ZeroVector;
};
