// Copyright Epic Games, Inc. All Rights Reserved.
// CamLensDistortion : PTZ 카메라 렌즈 방사 왜곡 모델(보드 #1297, OpenCV pinhole radial).
// 탄젠트 좌표 x = X/Z, y = Y/Z, r² = x² + y², x_d = x·(1 + k1·r² + k2·r⁴) (y 도 같다).
// 픽셀 u = fx·x_d + cx, fx = fy = (W/2)/tan(hfov/2) — hfov 는 줌이 반영된 현재 수평 화각. k 는 줌과 무관.
// 렌더(APTZCameraActor)·화면 투영(scenario slotPolygon)이 이 함수들만 쓴다(같은 모델 보장).

#pragma once

#include "CoreMinimal.h"

namespace CamLens
{
	/** k1·k2 허용 범위(계약: [-1, 1]). */
	constexpr double KLimit = 1.0;

	/** 왜곡 없음(핀홀)인가. */
	inline bool IsPinhole(double K1, double K2) { return K1 == 0.0 && K2 == 0.0; }

	/** r_d = r·(1 + k1 r² + k2 r⁴). r 은 왜곡 전 탄젠트 반경. */
	PARK3D_API double DistortRadius(double R, double K1, double K2);

	/** d r_d / d r = 1 + 3 k1 r² + 5 k2 r⁴ — 0 이하가 되면 그 반경에서 그림이 접힌다. */
	PARK3D_API double DistortSlope(double R, double K1, double K2);

	/**
	 * 단조 구간의 끝: DistortSlope 가 처음 0 이하가 되는 반경(없으면 RCap).
	 * 이 반경까지는 r → r_d 가 증가 함수라 역변환이 하나로 정해진다.
	 */
	PARK3D_API double MonotonicLimit(double K1, double K2, double RCap = 8.0);

	/**
	 * 화면 모서리(왜곡 후 탄젠트 반경 CornerRd)에 닿기 전에 그림이 접히면 거절한다(계약).
	 * OutWhy 는 사람이 읽을 사유(영어 — 외부 에이전트가 읽는다).
	 */
	PARK3D_API bool Validate(double K1, double K2, double CornerRd, FString& OutWhy);

	/** 화면 모서리의 왜곡 후 탄젠트 반경: tan(hfov/2)·sqrt(1 + (H/W)²). */
	PARK3D_API double CornerTan(double HFovDeg, int32 W, int32 H);

	/**
	 * 역변환: 왜곡 후 반경 Rd → 왜곡 전 반경(단조 구간 이분법).
	 * Rd 가 단조 구간 끝의 값보다 크면(cam.setFOV 로 화각을 넓혀 모서리가 접힘 영역에 든 경우) 끝값으로 자르고 false.
	 */
	PARK3D_API bool UndistortRadius(double Rd, double K1, double K2, double& OutR);

	/** 왜곡 전 탄젠트 좌표 → 왜곡 후. */
	PARK3D_API FVector2D DistortTan(const FVector2D& Xy, double K1, double K2);

	/** 왜곡 후 탄젠트 좌표 → 왜곡 전(UndistortRadius 기반). */
	PARK3D_API FVector2D UndistortTan(const FVector2D& Xd, double K1, double K2);
}
