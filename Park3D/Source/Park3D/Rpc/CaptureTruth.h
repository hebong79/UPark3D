// Copyright Epic Games, Inc. All Rights Reserved.
// CaptureTruth : 캡처 1장에 대한 이미지 정답(보드 #1468 — VPD/LPD/LPR 평가).
// cam.captureJPG/PNG {withTruth} 가 방금 찍은 그림과 같은 시점·같은 투영으로 차량·번호판의 2D 상자와 보이는 비율을 낸다.
//
// 방법(머티리얼 에셋 없음): 전용 캡처 컴포넌트로 장면 깊이를 한 장 찍고(가림 포함), 화면에 걸리는 차량마다
// 그 차만 그린 깊이를 한 장씩 더 찍는다(가림 없음 = amodal). 두 깊이가 같은 픽셀이 "보이는 픽셀"이다.
// 번호판은 판 평면을 광선으로 해석해 같은 비교를 한다(렌더 추가 없음). 왜곡 카메라는 출력 픽셀마다 왜곡 전 방향으로
// 깊이 공간 픽셀을 찾아 같은 와프를 따른다. 정답은 RPC 응답에만 실린다 — 스트림·뷰어는 이 렌더를 보지 않는다.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class APTZCameraActor;

namespace CaptureTruth
{
	/**
	 * 출력 이미지(W×H, 원점 왼쪽 위·x 오른쪽·y 아래) 투영 모델. 카메라 축 Fwd/Right/Up(월드), 위치 C(cm).
	 * 핀홀 탄젠트 → 렌즈 왜곡(CamLensDistortion) → 픽셀. 렌더·정답·slotPolygon 이 같은 모델이다.
	 */
	struct FProjector
	{
		FVector C = FVector::ZeroVector, Fwd = FVector::ForwardVector, Right = FVector::RightVector, Up = FVector::UpVector;
		int32 W = 0, H = 0;
		double F = 1.0;      // 출력 초점거리(px) = (W/2)/tan(hfov/2)
		double K1 = 0.0, K2 = 0.0;

		/** 월드 점 → 출력 픽셀(연속 좌표, 픽셀 가장자리 기준). 카메라 뒤(전방 거리 1 cm 이하)면 false. */
		PARK3D_API bool Project(const FVector& P, FVector2D& OutPx, double* OutDepth = nullptr) const;
	};

	/**
	 * 출력 픽셀 → 깊이 공간 픽셀 인덱스 표(왜곡 카메라 전용). 출력 픽셀 중심의 왜곡 전 방향이 깊이 렌더의 어느 픽셀에 떨어지는가.
	 * 깊이 공간 밖이면 -1. 핀홀이면 쓰지 않는다(같은 격자).
	 */
	PARK3D_API void BuildDepthIndexMap(const FProjector& Out, int32 Dw, int32 Dh, double DepthTan, TArray<int32>& OutMap);

	/**
	 * Cam 의 출력 W×H 그림에 대한 정답을 만든다(그림은 호출자가 방금 찍었다 — 같은 게임 프레임, 같은 자세).
	 * @param OutMask  bWithMask 면 W*H 바이트(0 = 배경, n = truth.cars 의 index n). 아니면 비운다.
	 */
	PARK3D_API bool Build(APTZCameraActor* Cam, int32 CamId, int32 W, int32 H, bool bWithMask,
		TSharedPtr<FJsonObject>& OutTruth, TArray<uint8>& OutMask, FString& OutError);
}
