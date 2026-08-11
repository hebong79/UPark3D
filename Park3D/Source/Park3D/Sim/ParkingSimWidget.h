// Copyright Epic Games, Inc. All Rights Reserved.
// ParkingSimWidget : 주차 시뮬레이션 조작용 상시 HUD(시작/정지/리플레이 + 상태 표시).
//
// 조명 패널과 같은 이유로 WBP 없이 C++ 위젯 트리로 만든다(헤드리스에서 WBP 를 만들 수 없다).
// 단축키(F9/F10)는 APark3DGameMode 가 같은 진입점(AParkingSimManager)에 바인딩한다.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ParkingSimWidget.generated.h"

class AParkingSimManager;
class UButton;
class UTextBlock;

UCLASS()
class PARK3D_API UParkingSimWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 매니저를 찾거나 스폰해 시뮬레이션을 시작한다(단축키 경로와 동일). */
	UFUNCTION(BlueprintCallable, Category = "Sim") void HandleStart();
	UFUNCTION(BlueprintCallable, Category = "Sim") void HandleStop();
	UFUNCTION(BlueprintCallable, Category = "Sim") void HandleReplay();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void BuildUI();
	AParkingSimManager* GetManager() const;
	void SetStatus(const FString& Text);

	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MessageText = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton> Btn_Start = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton> Btn_Stop = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton> Btn_Replay = nullptr;
};
