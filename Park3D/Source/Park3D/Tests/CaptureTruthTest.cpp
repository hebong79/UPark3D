// Copyright Epic Games, Inc. All Rights Reserved.
// CaptureTruthTest : 캡처 정답(보드 #1468)의 투영·왜곡 표·시드 해석 순수 함수 검증. 렌더(깊이 패스)는 실RHI 실기로 확인한다.

#include "Misc/AutomationTest.h"
#include "../Rpc/CaptureTruth.h"
#include "../Rpc/RpcParamUtil.h"
#include "../CamLensDistortion.h"
#include "Dom/JsonObject.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCaptureTruthProjectionTest,
	"Park3D.Rpc.CaptureTruth.Projection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCaptureTruthProjectionTest::RunTest(const FString& Parameters)
{
	// 원점에서 +X 를 보는 1280×720, 수평 56.5° 카메라.
	CaptureTruth::FProjector Pj;
	Pj.W = 1280; Pj.H = 720;
	const double T = FMath::Tan(FMath::DegreesToRadians(56.5 * 0.5));
	Pj.F = 640.0 / T;

	FVector2D Px;
	double Depth = 0.0;
	TestTrue(TEXT("T-1 앞의 점은 투영된다"), Pj.Project(FVector(1000, 100, -50), Px, &Depth));
	TestEqual(TEXT("T-2 x = cx + F·(y/x)"), Px.X, 640.0 + Pj.F * 0.1, 1e-6);
	TestEqual(TEXT("T-3 y 아래가 + (아래쪽 점)"), Px.Y, 360.0 + Pj.F * 0.05, 1e-6);
	TestEqual(TEXT("T-4 깊이 = 전방 거리"), Depth, 1000.0, 1e-9);
	TestFalse(TEXT("T-5 뒤의 점은 거절"), Pj.Project(FVector(-10, 0, 0), Px));

	// 왜곡 카메라: 출력 픽셀 → 깊이 공간 픽셀 표가 같은 방향을 가리키는가(점 → 출력 픽셀 → 표 → 깊이 픽셀 광선).
	Pj.K1 = -0.3;
	const double DepthTan = T * 1.3;
	const int32 Dw = FMath::CeilToInt32(Pj.W * 1.3);
	const int32 Dh = FMath::RoundToInt32(static_cast<double>(Dw) * Pj.H / Pj.W);
	TArray<int32> Map;
	CaptureTruth::BuildDepthIndexMap(Pj, Dw, Dh, DepthTan, Map);
	TestEqual(TEXT("T-6 표 크기 = 출력 픽셀 수"), Map.Num(), Pj.W * Pj.H);

	const double Fd = (Dw * 0.5) / DepthTan;
	for (const FVector& P : { FVector(1000, 450, 200), FVector(1000, -500, -260), FVector(1000, 30, 10) })
	{
		TestTrue(TEXT("T-7 투영"), Pj.Project(P, Px));
		const int32 Q = FMath::FloorToInt32(Px.Y) * Pj.W + FMath::FloorToInt32(Px.X);
		const int32 R = Map.IsValidIndex(Q) ? Map[Q] : -1;
		TestTrue(TEXT("T-8 화면 안 픽셀은 깊이 공간 안"), R >= 0);
		if (R < 0) continue;
		// 깊이 픽셀 중심 광선이 가리키는 탄젠트와 점의 핀홀 탄젠트 차 — 출력 1픽셀(왜곡 확대 감안 2픽셀) 이내.
		const FVector2D RayTan(((R % Dw) + 0.5 - Dw * 0.5) / Fd, ((R / Dw) + 0.5 - Dh * 0.5) / Fd);
		const FVector2D PtTan(P.Y / P.X, -P.Z / P.X);
		TestTrue(FString::Printf(TEXT("T-9 같은 방향 (%.5f,%.5f) vs (%.5f,%.5f)"), RayTan.X, RayTan.Y, PtTan.X, PtTan.Y),
			(RayTan - PtTan).GetAbsMax() < 2.0 / Pj.F);
	}

	// 시드(보드 #1468): 준 값은 그대로, 생략·0 은 양수 하나를 새로 뽑는다.
	TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
	P->SetNumberField(TEXT("seed"), 7);
	TestEqual(TEXT("T-10 seed 7 그대로"), RpcParam::ResolveSeed(P), 7);
	P->SetNumberField(TEXT("seed"), 0);
	TestTrue(TEXT("T-11 seed 0 → 양수"), RpcParam::ResolveSeed(P) > 0);
	TestTrue(TEXT("T-12 생략 → 양수"), RpcParam::ResolveSeed(MakeShared<FJsonObject>()) > 0);
	return true;
}

#endif
