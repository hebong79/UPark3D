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
};

/**
 * === 바닥 번호 지정 1건(UE 전용 — Unity SCameraPosList 에는 없다) ===
 * "이 면(face)을 slot 번으로 한다". 수동(auto_renumber=false, 기본)이면 기준 면부터 count 장(0=한 장)만 바뀌고
 * 뒤 면은 손대지 않는다(번호가 겹쳐도 막지 않는다). 자동이면 기준 면부터 묶음 끝까지 +1 씩 이어 매기고 count 는 무시한다.
 * face 는 FParkingSlotNumberInfo::FaceKey("level:<액터>#<인스턴스>" / "preset:<idx>#<slot>") — 순번은 프리셋을
 * 만들거나 지우면 밀려 엉뚱한 면을 가리키므로 키로 둔다.
 */
USTRUCT(BlueprintType)
struct FCamSlotNumber
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") FString face;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") int32   slot = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") int32   count = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") bool    auto_renumber = false;
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

	/**
	 * 바닥 번호 지정 목록 — 파일(주차장) 전체에 **하나**다. 프리셋 소속이 아니다.
	 * 바닥 번호는 카메라·프리셋과 무관하게 한 벌뿐이라, 프리셋마다 기준점 하나씩 들고 있으면 새 면을 지정할 때마다
	 * 앞서 지정한 면이 원래 번호로 되돌아간다(2026-09-08 신고 "자동이 아닌데 범위 밖 번호가 바뀐다"의 정체).
	 * 지정한 face 마다 한 항목이 쌓이고, 같은 face 를 다시 지정하면 그 항목만 바뀐다. 파일에 같이 저장된다.
	 * 이 키가 없는 옛 파일·Unity 파일은 빈 목록으로 읽히고, Unity 파서는 모르는 키를 무시하므로 파일 호환은 유지된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") TArray<FCamSlotNumber> slot_numbers;
};
