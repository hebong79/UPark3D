// Copyright Epic Games, Inc. All Rights Reserved.
// PlateLayout : 번호판 종류별 글자 칸 배치(mm) — OmiPark3D plategen.layout() 이식.
//
// 고시 별표(2의1 · 2의2 · 18 · 1)의 mm 치수를 그대로 쓴다. 한 줄 판은 세로 중앙 85(83) mm 칸에 좌→우,
// 두 줄 판은 윗줄(앞자리+한글)·아랫줄(일련번호). 지역판은 지역명 칸이 따로 붙는다(사업용 520×110 은 좌측 세로 2자,
// 구형 335×170 은 윗줄 앞).
// 여기는 순수 계산이라 Automation 테스트로 검증한다(칸이 판을 벗어나지 않고, 겹치지 않고, 글자 수가 맞는지).

#pragma once

#include "CoreMinimal.h"

struct FPlateKindDef;

/** 글자 한 칸. 좌표는 판 좌상단 기준 mm(u 오른쪽, v 아래). */
struct FPlateCell
{
	TCHAR Ch = 0;
	double X0 = 0.0, Y0 = 0.0, X1 = 0.0, Y1 = 0.0;
	/** 잉크 높이(mm). 숫자 = 칸 높이 × 0.88, 한글 = × 0.68 (별표 2의2: 74.8/85, 57/85). */
	double InkH = 0.0;
	bool bDigit = false;

	double Width() const { return X1 - X0; }
	double Height() const { return Y1 - Y0; }
};

namespace PlateLayout
{
	/**
	 * 종류 + 표시 번호 조각 → 칸 목록. `Shown` 은 PlateKinds::DisplayNumber 가 준 정규 번호여야 한다(자릿수·지역·한글이
	 * 종류에 맞춰져 있다). 문법이 안 맞으면 빈 배열.
	 */
	TArray<FPlateCell> Cells(const FPlateKindDef& Kind, const FString& Shown);
}
