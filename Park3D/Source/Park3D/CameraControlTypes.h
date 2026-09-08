// Copyright Epic Games, Inc. All Rights Reserved.
// CameraControlTypes : PTZ 카메라 컨트롤 데이터 모델.
// Unity CameraControl 포팅 (CSaveInitCampPos.SCameraPosList / SCameraPos / SCamDir, CMyUtil.SVector3 / SPtz).
// JSON 스키마는 Unity CSaveInitCampPos.cs 의 SCameraPosList(2단 중첩 datas)와 100% 호환되어야 한다.
// 모든 직렬화 멤버는 소문자 시작 → FJsonObjectConverter 직렬화 키를 Unity(cam_id/preset_id/target_pos/p/t/z)와 일치.

#pragma once

#include "CoreMinimal.h"
#include "CameraControlTypes.generated.h"

/**
 * === Unity SVector3 (JSON pos/rot) — 소문자 키 강제 ===
 * FVector 직접 사용 시 UE 직렬화가 대문자 X/Y/Z가 되어 Unity 비호환(좌표 손실).
 * 멤버명을 소문자 시작 x/y/z 로 두어 FJsonObjectConverter 직렬화 키를 Unity와 일치시킨다.
 * 좌표계는 Unity(왼손, Y-up, m): x=right, y=up(높이), z=forward. (ParkingCarTypes.FCarVec3 와 동일 관례)
 */
USTRUCT(BlueprintType)
struct FCamVec3
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") float x = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") float y = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") float z = 0.f;
};

/**
 * === Unity SPtz (JSON ptzmin/ptzmax) — 키 p/t/z ===
 * Pan/Tilt/Zoom(배율). 기본값은 설계 §4.1 규약(p=0, t=0, z=1). 실 파일에는 항상 저장되므로 인메모리 기본은 보조.
 */
USTRUCT(BlueprintType)
struct FCamPtz
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") float p = 0.f;   // Pan
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") float t = 0.f;   // Tilt
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") float z = 1.f;   // Zoom(배율)
};

/**
 * === Unity SCamDir (카메라 뷰 방향 프리셋 1개) ===
 * 멤버명을 소문자 시작으로 두어 JSON 키(idx/sname/cam_id/preset_id/pos/rot/pan/tilt/zoom/ptzmin/ptzmax)가 Unity와 일치.
 * 내부 pos는 Unreal 미터 좌표다. isUnreal 없는 legacy JSON만 로드 경계에서 Unity 좌표로 변환한다.
 * 기본값은 설계 §4.1 규약 준수(idx=0, cam_id=1, preset_id=1, pan=0, tilt=0, zoom=1).
 */
USTRUCT(BlueprintType)
struct FCamDir
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") int32    idx = 0;          // 프리셋 순번(0=미설정)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") FString  sname;            // "Preset N"
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") int32    cam_id = 1;       // 카메라 id(1부터)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") int32    preset_id = 1;    // 프리셋 id(1부터)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") FCamVec3 pos;              // Unity 좌표(y=높이), m
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") FCamVec3 rot;              // Euler(x=tilt, y=pan, z=0)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") float    pan = 0.f;        // = rot.y
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") float    tilt = 0.f;       // = rot.x
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") float    zoom = 1.f;       // 줌 배율(1~36), FOV 아님
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") FCamPtz  ptzmin;           // 슬라이더 min(pan/tilt/zoom)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") FCamPtz  ptzmax;           // 슬라이더 max(pan/tilt/zoom)

	/**
	 * 시작 슬롯 — 이 프리셋의 기준 면(start_face)이 바닥에서 받을 번호. 0=미지정.
	 * start_face 가 함께 있으면 그 면의 바닥 번호가 **다시 매겨진다**(AParkingPresetManager::SetNumberAnchors).
	 * 뒤에 오는 면까지 이어 매길지는 auto_renumber 가 정한다(기본은 기준 면 한 장만).
	 * Unity SCamDir 에는 없는 키다. Unity 쪽 파서는 모르는 키를 무시하므로 파일 호환은 유지되고,
	 * 이 값이 없는 옛 파일은 0(미지정)으로 읽힌다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") int32    start_slot = 0;
	/**
	 * 기준 면의 키(FParkingSlotNumberInfo::FaceKey — "level:<액터>#<인스턴스>" / "preset:<idx>#<slot>"). 빈 문자열=없음.
	 * 순번이 아니라 키로 두는 이유: 순번은 프리셋을 만들거나 지우면 밀려 엉뚱한 면을 가리킨다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") FString  start_face;
	/**
	 * 기준 면부터 이어 매길 면의 **개수**. N 이면 수동/자동 어느 모드든 기준 면부터 N 장을 강제로 매긴다(번호가 겹쳐도 막지 않는다).
	 * 0 이면 auto_renumber 가 범위를 정한다. 예) start_slot=1, start_count=7 → 기준 면부터 7개 면이 1~7, 그 뒤는 원래 순번.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") int32    start_count = 0;
	/**
	 * start_count 가 0 일 때의 범위. 거짓(기본, 수동)이면 **기준 면 한 장만** start_slot 번이 되고,
	 * 참(자동)이면 묶음 끝까지 +1 씩 이어 매긴다. 이 값이 없는 옛 파일은 거짓=수동으로 읽힌다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") bool     auto_renumber = false;
};

/** === Unity SCameraPos (카메라 1대의 프리셋 리스트) === 내부 datas 키 소문자. */
USTRUCT(BlueprintType)
struct FCameraPos
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") float           target_pos = 0.f; // 타겟 위치
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") TArray<FCamDir> datas;            // 프리셋 리스트
};

/**
 * === Unity SCameraPosList (JSON 루트 = 카메라 배열) ===
 * 루트 키 datas 소문자. 2단 중첩 datas: { "datas":[ { "target_pos":.., "datas":[ SCamDir.. ] } ] }.
 */
USTRUCT(BlueprintType)
struct FCameraPosList
{
	GENERATED_BODY()

	/** true이면 모든 FCamDir.pos가 Unreal 미터 좌표. 누락/false는 Unity legacy 형식. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") bool isUnreal = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") TArray<FCameraPos> datas;
};
