// Copyright Epic Games, Inc. All Rights Reserved.
// LevelSelectWidget : 화면 오른쪽 위 "주차장 선택" 패널. config_pmaker.json 의 levels[] 를 콤보에 올리고,
// 고르면 그 항목의 레벨로 이동한다(UGameplayStatics::OpenLevel).
//
// 조명 패널·시뮬레이션 HUD 와 같은 이유로 WBP 없이 C++ 위젯 트리로 만든다 — Content/ 는 git 밖이라 WBP 는
// 커밋에 남지 않고, 이 세션엔 에디터 MCP 도 붙지 않았다. 새 클래스라 쿠킹된 WBP 의 베이스가 아니므로
// UPROPERTY 를 두어도 "Bad export index" 함정(CarPlacementWidget 2026-08-12)과 무관하다.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Config/Park3DAppConfig.h"
#include "Types/SlateEnums.h"
#include "LevelSelectWidget.generated.h"

class UBorder;
class UComboBoxString;
class UTextBlock;

UCLASS()
class PARK3D_API ULevelSelectWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * 콤보를 InOptions 의 이름으로 채우고, CurrentLevelPath(NormalizeLevelPath 형식)와 같은 레벨의 항목을
	 * 선택 상태로 둔다(코드 선택은 이동을 일으키지 않는다). 뷰포트에 올린 뒤 부를 것 — 그래야 콤보가 살아 있다.
	 * @return 항목이 하나라도 있으면 true.
	 */
	bool Populate(const TArray<FPark3DLevelOption>& InOptions, const FString& CurrentLevelPath);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;

	/** 콤보 선택 변경. 사용자가 고른 것(OnMouseClick/OnKeyPress)만 이동이고, 코드 선택(Direct)은 무시한다. */
	UFUNCTION() void HandleSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	/** 콤보 항목/본문 위젯 — 흰 드롭다운 위에 검은 글자(카메라 패널 콤보와 같은 규약). */
	UFUNCTION() UWidget* HandleGenerateItem(FString Item);

private:
	void BuildUI();

	/** 마지막 Populate 로 받은 목록. 콤보 문자열 → 레벨 경로를 여기서 찾는다. */
	TArray<FPark3DLevelOption> Options;

	UPROPERTY(Transient) TObjectPtr<UBorder> RootBorder = nullptr;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> Combo_Level = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText = nullptr;
};
