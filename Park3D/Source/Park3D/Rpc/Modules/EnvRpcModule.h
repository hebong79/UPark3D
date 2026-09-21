// Copyright Epic Games, Inc. All Rights Reserved.
// EnvRpcModule : env.* (13) 핸들러 — 레벨에 배치된 환경 액터(나무·건물·가로등…)를 찾고 숨기고, 에셋을 놓는다.
//
// env.list · env.hide 는 레벨 자체의 액터를 다룬다. Park3D 가 만든 오브젝트가 아니므로 조작은 "지우기"가 아니라
// "숨기기"다 — 레벨 에셋을 건드리지 않아 되돌릴 수 있고(재쿡 불필요), 가림률 실험에서 가리는 물체를 껐다 켜며
// 비교할 수 있다.
//
// 나머지 11개(assets/create/update/delete/clear/save/load/reloadAssets/hideMap/showMap/mapState)는 OmiPark3D
// (OmniversPark3D/OmiPark3D/src/omipark3d/rpc/modules/env.py)가 먼저 만든 확장을 되가져온 것이다.
//   - 프롭 = env.create 가 스폰한 AStaticMeshActor(Movable, 태그 `EnvProp`). update/delete/clear/save 는 이 태그가
//     있는 액터만 다룬다 — 레벨 액터는 절대 지우지 않는다.
//   - 에셋 = AssetRegistry 의 /Game/Environment 아래 StaticMesh. slug = 에셋 이름 소문자, category = 바로 아래 폴더.
//     쿡 빌드에서도 AssetRegistry.bin 으로 목록이 나온다(디스크 폴더를 훑지 않는다).
//   - 좌표는 계약 전체와 같은 언리얼 미터 규약(pos{x,z,y?} · MetersToUU=100 · car.create 와 같은 사상),
//     회전은 FRotator `rot{roll,pitch,yaw}` 도.
//   - hideMap/showMap/mapState 는 런타임 상태(파일에 남지 않는다). 숨긴 집합은 모듈이 들고 있어 showMap 이 그것만 되돌린다.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "../RpcModuleSupport.h"

class AActor;
class UStaticMesh;

class FEnvRpcModule : public FRpcModuleBase
{
public:
	explicit FEnvRpcModule(TFunction<UWorld*()> InWorldGetter) : FRpcModuleBase(MoveTemp(InWorldGetter)) {}

	virtual void Register(URpcDispatcher& Dispatcher) override;

	/** 배치 가능한 StaticMesh 에셋 한 항목(AssetRegistry 에서 읽음). */
	struct FEnvAssetEntry
	{
		FString Slug;        // 에셋 이름 소문자(같은 이름이 다른 폴더에 있으면 두 번째부터 `<category>_<이름>`)
		FString Name;        // 에셋 이름 원문(SM_TrashCan_01)
		FString ObjectPath;  // /Game/Environment/Props/SM_X.SM_X — LoadObject 에 그대로 쓴다
		FString Package;     // /Game/Environment/Props/SM_X — OmiPark3D 의 ueAsset 과 같은 표기
		FString Category;    // /Game/Environment 바로 아래 폴더 소문자(building · props …)
	};

	/** env.create 로 놓은 프롭 하나 — 액터와 계약 필드. 키(Name)는 액터의 실제 이름(AActor::GetName)이다. */
	struct FEnvPropRecord
	{
		FString Name;
		FString Asset;   // slug 또는 (경로로 만든 경우) 오브젝트 경로
		FString Label;
		FString Group;   // 파일에서 읽은 묶음 이름(env.delete group). create 는 비워 둔다
		TWeakObjectPtr<AActor> Actor;
	};

private:
	// ---- 에셋 목록 ----
	/** 목록을 (처음 한 번 또는 강제로) AssetRegistry 에서 채운다. */
	void EnsureAssetCache(bool bForceRescan);
	/** slug(대소문자 무시) 또는 오브젝트 경로 → 로드된 메시. 실패면 OutError(-32000) 설정 후 nullptr. OutAsset 은 기록용 식별자. */
	UStaticMesh* ResolveMesh(const FString& SlugOrPath, FString& OutAsset, FRpcError& OutError);

	bool bAssetsScanned = false;
	TMap<FString, FEnvAssetEntry> AssetsBySlug;

	// ---- 프롭 ----
	/** 다른 레벨로 넘어갔거나 파괴된 액터의 기록을 버린다(모듈은 GameInstance 수명이라 레벨보다 오래 산다). */
	void PruneProps(UWorld* World);
	/** 액터를 스폰하고 기록한다. Name 이 비었거나 겹치면 `<asset>_<번호>` 로 짓는다(OmiPark3D add_prop 과 같은 규약). */
	FEnvPropRecord* SpawnProp(UWorld* World, UStaticMesh* Mesh, const FString& Asset, const FString& Name, const FString& Label,
		const FString& Group, const FVector& PosMeters, const FRotator& Rot, const FVector& Scale);
	/** 이름으로 프롭을 찾는다. 없으면 OutError 설정 후 nullptr. */
	FEnvPropRecord* FindProp(const FString& Name, FRpcError& OutError);
	/** 프롭 액터를 파괴하고 기록을 지운다. 있었으면 true. */
	bool RemoveProp(const FString& Name);
	FString NewPropName(const FString& Asset);

	TMap<FString, FEnvPropRecord> Props;
	int32 PropSerial = 0;

	// ---- 맵 오브젝트 숨김 ----
	/** hideMap 이 실제로 숨긴 액터들 — showMap 은 정확히 이것만 되돌린다(env.hide 로 따로 숨긴 것은 건드리지 않는다). */
	TArray<TWeakObjectPtr<AActor>> MapHidden;
	/** {mapCount, hiddenCount, keptCount, hidden} 을 Root 에 채운다. */
	void FillMapState(UWorld* World, const TSharedPtr<FJsonObject>& Root);
	/** hidden=true 면 맵 오브젝트를 숨기고, false 면 MapHidden 을 되돌린다. 반환: 이번에 바뀐 수. */
	int32 SetMapHidden(UWorld* World, bool bHidden);
};
