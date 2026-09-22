// Copyright Epic Games, Inc. All Rights Reserved.
// LevelSlotLibrary : 레벨 BP_ParkingSlot 의 ISM_Slot 인스턴스(레벨 주차면)를 런타임에 고치고 파일로 남기는 공용 함수.
//
// 레벨 면은 .umap 에 박힌 에셋이라 에디터 없이는 못 고치고 Content/ 는 git 에도 없다. 그래서 인스턴스 변환을
// 런타임에 바꾸고(bay.update/delete/create), 그 결과를 스냅샷 파일(Save/3D/Bay/<이름>.json)로 저장해
// 기동 시 config `slot_file` 로 다시 적용한다 — hide_actors 와 같은 "레벨 에셋을 안 고치고 런타임에 덮는" 규약.
//
// 파일 형식(kind=levelSlots): actors[] 마다 name(BP_ParkingSlot 액터 이름)과 slots[](월드 변환).
//   { "kind":"levelSlots", "level":"/Game/Levels/LV_Park_03",
//     "actors":[ { "name":"BP_ParkingSlot_C_1", "slots":[ { "pos":{x,y,z}(m), "yaw":72.09, "scale":{x,y,z} } ] } ] }
// scale 은 인스턴스 스케일 그대로다(ISM_Slot 은 엔진 Plane 100cm 라 스케일 = 판 외측 크기 m).
// 적용은 액터 단위 **교체**다 — 파일에 있는 액터는 인스턴스를 전부 지우고 파일대로 다시 넣고, 파일에 없는 액터는 그대로 둔다.

#pragma once

#include "CoreMinimal.h"

class AActor;
class UWorld;
class UInstancedStaticMeshComponent;
class FJsonObject;

namespace Park3DLevelSlots
{
	/** 레벨 면 인스턴스 하나(월드 공간, cm). */
	struct FSlotInstance
	{
		FString ActorName;
		FTransform Transform;
	};

	/** ISM_Slot 인가(ISM_Stopper·ISM_Space 제외) — ParkingPresetManager·CarPlacementManager·BayRpcModule 과 같은 규약. */
	bool IsSlotComponent(const UInstancedStaticMeshComponent* Comp);

	/** 레벨 BP_ParkingSlot 액터를 이름 끝 번호순으로(preset.numbers 의 레벨 면 순서). */
	void CollectSlotActors(UWorld* World, TArray<AActor*>& Out);

	/** 액터의 ISM_Slot 컴포넌트. 없으면 nullptr. */
	UInstancedStaticMeshComponent* FindSlotComponent(AActor* Actor);

	/** 월드의 레벨 면 전부를 액터순 → 인스턴스순으로 스냅샷. Override 에 있는 키("level:<액터>#<인스턴스>")는 그 변환을 대신 쓴다(숨긴 면의 원래 변환). */
	void Snapshot(UWorld* World, const TMap<FString, FTransform>& Override, TArray<FSlotInstance>& Out);
	/** 액터 목록을 직접 주는 판(테스트용 — 월드 판이 CollectSlotActors 뒤에 이것을 부른다). */
	void Snapshot(const TArray<AActor*>& SlotActors, const TMap<FString, FTransform>& Override, TArray<FSlotInstance>& Out);

	/**
	 * 스냅샷을 월드에 적용 — 파일에 나온 액터마다 인스턴스를 비우고 목록대로 다시 넣는다.
	 * @return 넣은 인스턴스 수. 월드에 없는 액터 이름은 OutMissingActors 에 담고 건너뛴다.
	 */
	int32 Apply(UWorld* World, const TArray<FSlotInstance>& In, TArray<FString>& OutMissingActors);
	int32 Apply(const TArray<AActor*>& SlotActors, const TArray<FSlotInstance>& In, TArray<FString>& OutMissingActors);

	/** 스냅샷 → JSON 루트(kind=levelSlots). LevelPath 는 기록용. */
	TSharedRef<FJsonObject> ToJson(const FString& LevelPath, const TArray<FSlotInstance>& In);

	/** JSON 루트 → 스냅샷. kind 가 levelSlots 가 아니거나 actors 가 없으면 false. */
	bool FromJson(const TSharedPtr<FJsonObject>& Root, TArray<FSlotInstance>& Out, FString& OutLevelPath);

	bool SaveFile(const FString& Path, const FString& LevelPath, const TArray<FSlotInstance>& In);
	bool LoadFile(const FString& Path, TArray<FSlotInstance>& Out, FString& OutLevelPath);
}
