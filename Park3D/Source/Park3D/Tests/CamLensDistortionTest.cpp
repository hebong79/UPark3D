// Copyright Epic Games, Inc. All Rights Reserved.
// CamLensDistortionTest : 렌즈 방사 왜곡 모델(CamLensDistortion, 보드 #1297) 순수 함수 검증. 월드 불필요.

#include "Misc/AutomationTest.h"
#include "../CamLensDistortion.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCamLensDistortionModelTest,
	"Park3D.CameraControl.LensDistortion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCamLensDistortionModelTest::RunTest(const FString& Parameters)
{
	// 56.5° 16:9 모서리: tan(28.25°)·sqrt(1 + (9/16)²) ≈ 0.6165
	const double Corner = CamLens::CornerTan(56.5, 1280, 720);
	TestEqual(TEXT("D-1 모서리 탄젠트 반경"), Corner, 0.61648, 1e-4);

	FString Why;
	TestTrue(TEXT("D-2 핀홀 허용"), CamLens::Validate(0.0, 0.0, Corner, Why));
	TestTrue(TEXT("D-3 k1=-0.3 허용(접힘 r=1.054 → r_d 0.703 > 모서리)"), CamLens::Validate(-0.3, 0.0, Corner, Why));
	TestFalse(TEXT("D-4 k1=-0.5 거절(r_d 최대 0.544 < 모서리)"), CamLens::Validate(-0.5, 0.0, Corner, Why));
	TestTrue(TEXT("D-4b 거절 사유에 fold"), Why.Contains(TEXT("folds")));
	TestFalse(TEXT("D-5 |k|>1 거절"), CamLens::Validate(0.0, 1.5, Corner, Why));
	TestTrue(TEXT("D-6 pincushion k1=+0.3 허용"), CamLens::Validate(0.3, 0.0, Corner, Why));

	// 접힘 반경: 1 + 3k1 r² = 0 → r = sqrt(1/(3·0.3))
	TestEqual(TEXT("D-7 단조 구간 끝"), CamLens::MonotonicLimit(-0.3, 0.0), FMath::Sqrt(1.0 / 0.9), 1e-6);

	// 왕복: 왜곡 → 역변환이 원래 좌표
	for (const double K1 : { -0.3, -0.1, 0.2 })
	{
		const FVector2D X(0.41, -0.22);
		const FVector2D Xd = CamLens::DistortTan(X, K1, 0.05);
		const FVector2D Back = CamLens::UndistortTan(Xd, K1, 0.05);
		TestTrue(FString::Printf(TEXT("D-8 왕복 k1=%g"), K1), Back.Equals(X, 1e-9));
	}

	// 중심 불변, barrel 은 바깥이 안쪽으로
	TestTrue(TEXT("D-9 중심 불변"), CamLens::DistortTan(FVector2D::ZeroVector, -0.3, 0.0).IsNearlyZero());
	TestTrue(TEXT("D-10 barrel 은 반경을 줄인다"), CamLens::DistortRadius(0.5, -0.3, 0.0) < 0.5);
	return true;
}

#endif
