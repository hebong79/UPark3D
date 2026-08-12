// Copyright Epic Games, Inc. All Rights Reserved.
// ParkingSimTypes : 주차 진입 시뮬레이션의 상태/기록 데이터 모델.
// 좌표는 전부 UE 월드 미터(XY 평면, Z=높이는 다루지 않는다) 이며 각도는 UE yaw(도)다.

#pragma once

#include "CoreMinimal.h"
#include "ParkingSimTypes.generated.h"

/** 시뮬레이션 진행 상태. 사용자 요구의 "이동/정지/주차" 3상태 + 대기/리플레이/출차. */
UENUM(BlueprintType)
enum class EParkSimState : uint8
{
	Idle     UMETA(DisplayName = "대기"),
	Moving   UMETA(DisplayName = "이동"),
	Stopped  UMETA(DisplayName = "정지"),
	Parked   UMETA(DisplayName = "주차"),
	Replay   UMETA(DisplayName = "리플레이"),
	Exited   UMETA(DisplayName = "출차"),
};

/**
 * 주행 방향.
 *  - Enter : 입구 → 주차면 (입차). 끝나면 차량이 면에 남는다.
 *  - Exit  : 주차면 → 출구 (출차). 출구에 닿으면 차량을 제거한다.
 * 입구와 출구는 같은 지점(주차면 전체의 가장 우측 바깥 중앙)이다.
 */
UENUM(BlueprintType)
enum class EParkSimDir : uint8
{
	Enter UMETA(DisplayName = "입차"),
	Exit  UMETA(DisplayName = "출차"),
};

/**
 * 주차 진입 방향.
 *  - Front : 전진으로 그대로 밀고 들어간다. 차 앞(코)이 주차면 안쪽을 향한다.
 *  - Rear  : 진입점에서 통로 쪽으로 돌아선 뒤 후진해 들어간다. 차 앞이 통로 쪽을 향한다.
 *  - Random: 둘 중 하나를 무작위(seed 반영, 50:50)로 고른다.
 */
UENUM(BlueprintType)
enum class EParkSimParkMode : uint8
{
	Random UMETA(DisplayName = "랜덤"),
	Front  UMETA(DisplayName = "전면주차"),
	Rear   UMETA(DisplayName = "후면주차"),
};

/** 기록 프레임 1개(리플레이의 원본 데이터). 키 이름이 곧 JSON 키가 되도록 소문자로 둔다. */
USTRUCT()
struct FParkSimFrame
{
	GENERATED_BODY()

	UPROPERTY() float t = 0.f;        // 시작으로부터의 경과(초)
	UPROPERTY() float x = 0.f;        // UE X(m)
	UPROPERTY() float y = 0.f;        // UE Y(m)
	UPROPERTY() float yaw = 0.f;      // 진행 방향(UE yaw, 도)
	UPROPERTY() float speed = 0.f;    // m/s
	UPROPERTY() FString state;        // 이동/정지/주차
};

/** 경로 경유지 1개(로그·응답용). */
USTRUCT()
struct FParkSimWaypoint
{
	GENERATED_BODY()

	UPROPERTY() float x = 0.f;
	UPROPERTY() float y = 0.f;
	UPROPERTY() FString role;         // 입구 / 통로 / 진입점 / 주차면 / 출구
};

/** 주행 1회분 기록 = 로그 파일의 JSON 루트이자 리플레이 소스. */
USTRUCT()
struct FParkSimRecord
{
	GENERATED_BODY()

	UPROPERTY() FString startedAt;                 // yyyy-MM-dd HH:mm:ss
	UPROPERTY() FString simMode;                   // 입차 / 출차
	UPROPERTY() int32 presetId = 0;                // 목표(입차) 또는 출발(출차) 주차면의 프리셋 idx
	UPROPERTY() int32 slotIndex = 0;               // 프리셋 내 면 번호(1-based)
	UPROPERTY() int32 seed = 0;                    // 0=비결정
	UPROPERTY() FString parkMode;                  // 전면주차 / 후면주차 (요청이 랜덤이어도 실제 뽑힌 값)
	                                               // 출차에서는 "출발 시점의 주차 자세"를 뜻한다.
	UPROPERTY() FString carId;
	UPROPERTY() float entranceX = 0.f;
	UPROPERTY() float entranceY = 0.f;
	UPROPERTY() float durationSec = 0.f;
	UPROPERTY() float distanceM = 0.f;
	UPROPERTY() FString result;                    // 주차완료 / 출차완료 / 중단 / 시간초과
	UPROPERTY() TArray<FParkSimWaypoint> waypoints;
	UPROPERTY() TArray<FString> events;            // 사람이 읽는 이벤트 로그 줄
	UPROPERTY() TArray<FParkSimFrame> frames;
};
