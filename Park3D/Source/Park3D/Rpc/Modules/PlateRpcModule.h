// Copyright Epic Games, Inc. All Rights Reserved.
// PlateRpcModule : plate.* (3) 핸들러 — 번호판 번호·종류 추첨(plate.random)과 텍스처 굽기(plate.bake).
// OmiPark3D 확장(rpc/modules/plate.py · world/plates.py)의 언리얼 이식이다. 차량에 붙이는 쪽은 car.setPlate.
//
// 종류 표·번호 문법·표시 정규화는 `Plate/PlateKinds` 에 있다(2026-09-21 차량 액터가 종류별 판을 그리게 되면서 RPC 밖으로
// 올렸다). 여기 `PlateRpc` 네임스페이스는 그 이름을 그대로 쓰는 호출부(CarRpcModule·테스트)를 위한 별칭 + RPC 파라미터 해석이다.
// 종류별 판은 `MI_Plate_<key>` 인스턴스가 있을 때만 그려지고(`rendered`), 없으면 옛 판(normal_film 그림)으로 떨어진다.

#pragma once

#include "CoreMinimal.h"
#include "../RpcModuleSupport.h"
#include "../../Plate/PlateKinds.h"

class UPlateGlyphAtlasSubsystem;

namespace PlateRpc
{
	using PlateKinds::Kinds;
	using PlateKinds::FindKind;
	using PlateKinds::DefaultKind;
	using PlateKinds::AutoKind;
	using PlateKinds::RandomKind;
	using PlateKinds::MakeStream;
	using PlateKinds::PickRandomKind;
	using PlateKinds::ParsePlate;
	using PlateKinds::DisplayNumber;
	using PlateKinds::DisplayText;

	/** kind 파라미터 → 표의 key | "auto" | "random" | ""(없음 → Default). 모르는 값은 -32000. */
	bool KindParam(const TSharedPtr<FJsonObject>& P, const FString& Key, const FString& Default, FString& OutKind, FRpcError& OutError);

	/** plate 파라미터 → 정규 번호. 없으면 빈 문자열(성공), 문법이 틀리면 -32000. */
	bool PlateParam(const TSharedPtr<FJsonObject>& P, const FString& Key, FString& OutCanonical, FRpcError& OutError);

	/** plate.kinds / car.plateKinds 공용 응답. */
	TSharedPtr<FJsonObject> KindsPayload(UWorld* World);

	/**
	 * 번호 SDF 아틀라스. 게임 인스턴스가 있으면 그 서브시스템, 없으면(에디터 자동화 테스트·커맨드릿) 파일만 읽는
	 * 독립 인스턴스를 하나 만들어 돌려 쓴다 — 아틀라스 로드는 게임 인스턴스에 의존하지 않는다.
	 */
	UPlateGlyphAtlasSubsystem* ResolveAtlas(UWorld* World);
}

class FPlateRpcModule : public FRpcModuleBase
{
public:
	explicit FPlateRpcModule(TFunction<UWorld*()> InWorldGetter) : FRpcModuleBase(MoveTemp(InWorldGetter)) {}

	virtual void Register(URpcDispatcher& Dispatcher) override;
};
