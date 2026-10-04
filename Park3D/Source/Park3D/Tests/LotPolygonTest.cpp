// Copyright Epic Games, Inc. All Rights Reserved.
// LotPolygonTest : 바닥 다각형(preview.show polygons·lot.set, 보드 #1168) — 정리·귀 자르기·JSON 왕복.

#include "Misc/AutomationTest.h"
#include "../Rpc/RpcOverlayActor.h"
#include "Dom/JsonObject.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** 삼각형 넓이 합(cm²) — 채움이 다각형 넓이와 같으면 겹침·구멍이 없다. */
	double LotTriArea(const FRpcPreviewPolygon& P)
	{
		double Sum = 0.0;
		for (int32 t = 0; t + 2 < P.Triangles.Num(); t += 3)
		{
			const FVector2D A = P.Points[P.Triangles[t]], B = P.Points[P.Triangles[t + 1]], C = P.Points[P.Triangles[t + 2]];
			Sum += FMath::Abs((B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X)) * 0.5;
		}
		return Sum;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLotPolygonTriangulateTest,
	"Park3D.Rpc.Lot.Triangulate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLotPolygonTriangulateTest::RunTest(const FString& Parameters)
{
	// 1) 20×12 m 사각형, 닫는 점 포함·중복 점 포함 → 4점, 삼각형 2개, 넓이 240 m².
	{
		FRpcPreviewPolygon P;
		P.Points = { {0, 0}, {2000, 0}, {2000, 0}, {2000, 1200}, {0, 1200}, {0, 0} };
		TestTrue(TEXT("사각형 Prepare"), P.Prepare());
		TestEqual(TEXT("사각형 점 정리"), P.Points.Num(), 4);
		TestEqual(TEXT("사각형 삼각형 수"), P.Triangles.Num() / 3, 2);
		TestTrue(TEXT("사각형 넓이"), FMath::IsNearlyEqual(LotTriArea(P), 2000.0 * 1200.0, 1.0));
	}
	// 2) 시계 방향 L 자(오목) — 넓이 = 20×12 − 10×6 = 180 m².
	{
		FRpcPreviewPolygon P;
		P.Points = { {0, 0}, {0, 1200}, {1000, 1200}, {1000, 600}, {2000, 600}, {2000, 0} };
		TestTrue(TEXT("L자 Prepare"), P.Prepare());
		TestEqual(TEXT("L자 삼각형 수"), P.Triangles.Num() / 3, 4);
		TestTrue(TEXT("L자 넓이"), FMath::IsNearlyEqual(LotTriArea(P), 1800000.0, 1.0));
	}
	// 3) 자기교차(나비넥타이) — 정리는 되지만 채움은 포기.
	{
		FRpcPreviewPolygon P;
		P.Points = { {0, 0}, {1000, 1000}, {1000, 0}, {0, 1000} };
		TestTrue(TEXT("나비 Prepare"), P.Prepare());
		TestTrue(TEXT("나비 채움 넓이가 다각형보다 크지 않음"), LotTriArea(P) <= 1000.0 * 1000.0 + 1.0);
	}
	// 4) 점 2개 → 실패.
	{
		FRpcPreviewPolygon P;
		P.Points = { {0, 0}, {100, 0}, {0, 0} };
		TestFalse(TEXT("점 부족"), P.Prepare());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLotPolygonJsonTest,
	"Park3D.Rpc.Lot.JsonRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLotPolygonJsonTest::RunTest(const FString& Parameters)
{
	FRpcPreviewPolygon A;
	A.Points = { {0, 0}, {2000, 0}, {2000, 1200}, {0, 1200} };
	A.ZCm = 12.f;
	A.Opacity = 0.4f;
	A.bDashed = true;
	A.LineWidthCm = 20.f;
	A.Color = FLinearColor(FColor::FromHex(TEXT("#ff9f1c")));
	A.Prepare();

	const TSharedPtr<FJsonObject> J = ARpcOverlayActor::PolygonToJson(A);
	TestEqual(TEXT("color hex"), J->GetStringField(TEXT("color")).ToLower(), FString(TEXT("#ff9f1c")));
	TestTrue(TEXT("x 는 m"), FMath::IsNearlyEqual(J->GetArrayField(TEXT("points"))[1]->AsObject()->GetNumberField(TEXT("x")), 20.0));

	FRpcPreviewPolygon B;
	TestTrue(TEXT("FromJson"), ARpcOverlayActor::PolygonFromJson(J, B));
	TestEqual(TEXT("점 수"), B.Points.Num(), 4);
	TestTrue(TEXT("z"), FMath::IsNearlyEqual(B.ZCm, 12.f, 0.01f));
	TestTrue(TEXT("opacity"), FMath::IsNearlyEqual(B.Opacity, 0.4f, 0.001f));
	TestTrue(TEXT("dashed"), B.bDashed);
	TestTrue(TEXT("lineWidth"), FMath::IsNearlyEqual(B.LineWidthCm, 20.f, 0.01f));
	TestEqual(TEXT("color"), B.Color.ToFColor(true), A.Color.ToFColor(true));
	TestEqual(TEXT("삼각형"), B.Triangles.Num(), 6);
	return true;
}

#endif
