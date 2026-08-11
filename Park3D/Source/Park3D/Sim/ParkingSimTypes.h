// Copyright Epic Games, Inc. All Rights Reserved.
// ParkingSimTypes : 주차 진입 시뮬레이션의 상태/기록 데이터 모델.
// 좌표는 전부 UE 월드 미터(XY 평면, Z=높이는 다루지 않는다) 이며 각도는 UE yaw(도)다.

#pragma once

#include "CoreMinimal.h"
#include "ParkingSimTypes.generated.h"

/** 시뮬레이션 진행 상태. 사용자 요구의 "이동/정지/주차" 3상태 + 대기/리플레이. */
UENUM(BlueprintType)
enum class EParkSimState : uint8
{
	Idle     UMETA(DisplayName = "대기"),
	Moving   UMETA(DisplayName = "이동"),
	Stopped  UMETA(DisplayName = "정지"),
	Parked   UMETA(DisplayName = "주차"),
	Replay   UMETA(DisplayName = "리플레이"),
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
	UPROPERTY() FString role;         // 입구 / 통로 / 진입점 / 주차면
};

/** 주행 1회분 기록 = 로그 파일의 JSON 루트이자 리플레이 소스. */
USTRUCT()
struct FParkSimRecord
{
	GENERATED_BODY()

	UPROPERTY() FString startedAt;                 // yyyy-MM-dd HH:mm:ss
	UPROPERTY() int32 presetId = 0;                // 목표 주차면의 프리셋 idx
	UPROPERTY() int32 slotIndex = 0;               // 프리셋 내 면 번호(1-based)
	UPROPERTY() int32 seed = 0;                    // 0=비결정
	UPROPERTY() FString carId;
	UPROPERTY() float entranceX = 0.f;
	UPROPERTY() float entranceY = 0.f;
	UPROPERTY() float durationSec = 0.f;
	UPROPERTY() float distanceM = 0.f;
	UPROPERTY() FString result;                    // 주차완료 / 중단 / 시간초과
	UPROPERTY() TArray<FParkSimWaypoint> waypoints;
	UPROPERTY() TArray<FString> events;            // 사람이 읽는 이벤트 로그 줄
	UPROPERTY() TArray<FParkSimFrame> frames;
};
