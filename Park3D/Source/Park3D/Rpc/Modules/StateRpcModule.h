// Copyright Epic Games, Inc. All Rights Reserved.
// StateRpcModule : 채팅 작업의 되돌리기·일괄 처리(보드 #1100).
//   state.snapshot / state.restore / state.drop / state.list — 파일 없이 메모리에 장면을 떠 두고 되돌린다(최근 20개).
//   car.setAll · car.createMany · car.setVisible · preset.setAll — 컬렉션을 한 호출로 바꾼다.
// 언리얼은 RPC 를 게임 스레드의 한 HTTP 콜백 안에서 처리하므로, 한 호출 안의 여러 변경은 같은 프레임에 들어간다
// (중간 상태가 스트림에 그려지지 않는다) — "한 번만 다시 그린다" 는 요구가 구조적으로 성립한다.

#pragma once

#include "CoreMinimal.h"
#include "../RpcModuleSupport.h"
#include "../../ParkingCarTypes.h"
#include "../../ParkingPresetTypes.h"
#include "../../ParkingPresetManager.h"

class FStateRpcModule : public FRpcModuleBase
{
public:
	explicit FStateRpcModule(TFunction<UWorld*()> InWorldGetter) : FRpcModuleBase(MoveTemp(InWorldGetter)) {}

	virtual void Register(URpcDispatcher& Dispatcher) override;

	enum EScope : uint8 { ScopeCars = 1, ScopePresets = 2, ScopeCameras = 4, ScopeView = 8, ScopeAll = 15 };

	/** 장면을 떠서 토큰을 돌려준다(실패 시 빈 문자열 + E). system.batch atomic 도 쓴다. */
	FString TakeSnapshot(uint8 Scopes, FRpcError& E);
	/** 토큰의 장면으로 되돌린다. OutSummary = {restored:{cars,presets,cameras,view}}. */
	bool RestoreSnapshot(const FString& Token, TSharedPtr<FJsonObject>& OutSummary, FRpcError& E);
	bool DropSnapshot(const FString& Token) { return Snapshots.RemoveAll([&Token](const FSnapshot& S) { return S.Token == Token; }) > 0; }

private:
	struct FCarSnap
	{
		FCarPos Pos;
		bool bHidden = false;
		FString Plate;
		FString PlateKind;
		bool bPainted = false;
		FLinearColor Color = FLinearColor::White;
	};
	struct FCamSnap
	{
		FVector Loc = FVector::ZeroVector;
		float Pan = 0.f, Tilt = 0.f, Zoom = 1.f;
	};
	struct FSnapshot
	{
		FString Token;
		uint8 Scopes = 0;
		FDateTime Created;
		TArray<FCarSnap> Cars;
		TArray<FParkingPreset> Presets;
		TArray<FSlotNumberAnchor> Anchors;
		TArray<FCamSnap> Cams;
		FVector ViewLoc = FVector::ZeroVector;
		FRotator ViewRot = FRotator::ZeroRotator;
	};

	TArray<FSnapshot> Snapshots;   // 오래된 것부터. 20 개를 넘으면 앞에서 버린다.
	int32 NextToken = 1;
};
