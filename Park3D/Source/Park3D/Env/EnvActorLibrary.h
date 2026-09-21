// Copyright Epic Games, Inc. All Rights Reserved.
// EnvActorLibrary : 레벨에 배치된 환경 액터(나무·건물·가로등…)를 가리는 공용 로직.
//
// 두 경로가 이것을 함께 쓴다 — env.* RPC(런타임 조작)와 시작 시 config 의 hide_actors 적용.
// 한쪽만 충돌을 끄거나 한쪽만 Park3D 소유 액터를 걸러 내면 상태가 갈라지므로 구현을 하나로 둔다.

#pragma once

#include "CoreMinimal.h"

class AActor;
class UWorld;

namespace Park3DEnv
{
	/**
	 * 목록·숨김 대상이 될 수 있는 액터인가.
	 * 보이는 지오메트리가 있어야 "가리는 물체"로서 의미가 있고(컨트롤러·게임모드 제외),
	 * Park3D 가 소유한 액터(차량·PTZ 카메라·주차면·바닥)는 제 도메인 메서드가 담당하므로 뺀다.
	 */
	PARK3D_API bool IsEnvActor(const AActor* Actor);

	/**
	 * 이름(AActor::GetName)이 일치하는 액터를 숨기거나 되돌린다.
	 * 숨길 때 충돌도 함께 끈다 — 안 그러면 보이지 않는 벽이 남아 클릭 피킹과 바닥 트레이스가 막힌다.
	 * 반환: 실제로 바뀐 액터들(호출부가 로그·응답에 그대로 쓴다).
	 */
	PARK3D_API TArray<AActor*> SetHiddenByNames(UWorld* World, const TSet<FString>& Names, bool bHidden);

	/**
	 * 액터 하나를 숨기거나 되돌린다(충돌도 함께 끄고 켠다). SetHiddenByNames 와 env.hideMap 이 같은 함수를 쓴다 —
	 * 한쪽만 충돌을 끄면 보이지 않는 벽이 남아 피킹·바닥 트레이스가 막히는 상태가 갈라진다.
	 */
	PARK3D_API void SetActorHidden(AActor* Actor, bool bHidden);

	/**
	 * env.hideMap 이 남겨 두는 "바닥" 액터인가 — 도로·지면·바닥·랜드스케이프·아스팔트(이름/라벨/클래스에 Road·Ground·
	 * Floor·Landscape·Asphalt, 대소문자 무시)와 주차면(BP_ParkingSlot*). OmiPark3D 의 MAP_KEEP_CATEGORIES(road·ground·
	 * floor) + bay_* 에 대응한다. 그 외 환경 액터(건물·소품·표지·보도·나무 …)는 맵 오브젝트로 친다.
	 */
	PARK3D_API bool IsMapKeepActor(const AActor* Actor);
}
