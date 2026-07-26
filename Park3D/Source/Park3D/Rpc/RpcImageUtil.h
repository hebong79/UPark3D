// Copyright Epic Games, Inc. All Rights Reserved.
// RpcImageUtil : 렌더타깃 픽셀(FColor/BGRA) → JPEG/PNG 인코딩 + base64. RHI 비의존 순수 함수.
// cam.captureJPG/PNG 가 사용. 인코딩만 분리해 헤드리스(-nullrhi)에서도 유닛테스트 가능(설계 §3).

#pragma once

#include "CoreMinimal.h"

namespace RpcImage
{
	/**
	 * FColor(BGRA) 픽셀 배열 → 압축 이미지 바이트.
	 * @param Pixels  W*H FColor 배열(부족하면 실패).
	 * @param bPng    true=PNG(무손실, Quality 무시), false=JPEG(Quality 1~100).
	 * @return 성공 여부. 성공 시 OutBytes 에 인코딩 결과.
	 */
	PARK3D_API bool EncodeColors(const TArray<FColor>& Pixels, int32 W, int32 H, bool bPng, int32 Quality, TArray<uint8>& OutBytes);

	/** 바이트 → base64 문자열. */
	PARK3D_API FString ToBase64(const TArray<uint8>& Bytes);
}
