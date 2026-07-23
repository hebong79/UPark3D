// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParkingGeometryLibrary.h"

FSingleParkingResult UParkingGeometryLibrary::GetSingleParkingDimensions(float AngleDeg, float Width, float Length, float CenterOffsetX)
{
	const float Rad = FMath::DegreesToRadians(AngleDeg);
	const float CosA = FMath::Abs(FMath::Cos(Rad));
	const float SinA = FMath::Abs(FMath::Sin(Rad));

	// 회전된 사각형의 bounding box
	const float Bw = Width * CosA + Length * SinA;   // 전체 수평 폭
	const float Bh = Width * SinA + Length * CosA;   // 전체 수직 높이

	const float HalfBW = Bw * 0.5f;
	const float B = HalfBW - CenterOffsetX;          // 왼쪽 거리
	const float C = HalfBW + CenterOffsetX;          // 오른쪽 거리

	FSingleParkingResult R;
	R.BoxWidth = B + C;                              // == Bw
	R.LeftSlopeW = FMath::Max(0.f, B);
	R.RightSlopeW = FMath::Max(0.f, C);
	R.BoxHeight = Bh;
	return R;
}

FMultiParkingResult UParkingGeometryLibrary::GetMultiParkingDimensions(int32 Count, float AngleDeg, float Width, float Length, float AdditionalSpacing)
{
	FMultiParkingResult R;
	if (Count <= 0)
	{
		return R;
	}

	const FSingleParkingResult Single = GetSingleParkingDimensions(AngleDeg, Width, Length, 0.f);

	const float Rad = FMath::DegreesToRadians(AngleDeg);
	const float CosJ = FMath::Abs(FMath::Cos(Rad));

	// h : 인접 주차면 중심 간 수평 간격 = width / cos(j) (사선 배치 보정)
	const float H = (CosJ > KINDA_SMALL_NUMBER ? Width / CosJ : Width) + AdditionalSpacing;

	R.OneBoxW = Single.BoxWidth;
	R.SlopHoriDist = Single.BoxWidth * 0.5f;                 // 시작선→첫 중심
	R.StepW = H;
	R.TotWidth = Single.BoxWidth + (Count - 1) * H;          // 전체 배열 수평 길이
	R.OneH = Single.BoxHeight;                               // 배열 수직 높이
	return R;
}

float UParkingGeometryLibrary::CalculateRotatedWidth(float Width, float Height, float AngleDeg)
{
	const float Rad = FMath::DegreesToRadians(AngleDeg);
	const float CosA = FMath::Cos(Rad);
	// |cos|≈0 (±90°) 이면 보정 불가 → 원본 폭 유지
	return FMath::IsNearlyZero(CosA) ? Width : (Width / CosA);
}

TArray<FParkingSpaceAssignment> UParkingGeometryLibrary::CalculateParkingSpaceAssignments(const TArray<FParkingPreset>& Presets)
{
	TArray<FParkingSpaceAssignment> Out;
	if (Presets.Num() == 0)
	{
		return Out;
	}

	// 1순위 CameraIdx, 2순위 PresetIdx 오름차순 정렬
	TArray<FParkingPreset> Sorted = Presets;
	Sorted.Sort([](const FParkingPreset& A, const FParkingPreset& B)
	{
		if (A.CameraIdx != B.CameraIdx)
		{
			return A.CameraIdx < B.CameraIdx;
		}
		return A.PresetIdx < B.PresetIdx;
	});

	// 1부터 순차 할당
	int32 Cur = 1;
	for (const FParkingPreset& P : Sorted)
	{
		FParkingSpaceAssignment A;
		A.CamIdx = P.CameraIdx;
		A.PresetIdx = P.PresetIdx;
		A.StartFaceNum = Cur;
		A.FaceCount = P.FaceCount;
		Out.Add(A);
		Cur += P.FaceCount;
	}
	return Out;
}
