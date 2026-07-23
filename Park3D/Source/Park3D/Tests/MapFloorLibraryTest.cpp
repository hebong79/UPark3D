// Copyright Epic Games, Inc. All Rights Reserved.
// MapFloorLibraryTest : UMapFloorLibrary 순수함수 유닛테스트.
// 설계 §9.1 테스트 포인트 TP-1(클램프), TP-2(크기→스케일), TP-3(축 매핑 — 비정방형),
//  TP-4(텍스트 파싱), TP-5(단위 사슬).
// PIE 불필요 — 자동화 프레임워크에서 에디터 컨텍스트로 실행.

#include "Misc/AutomationTest.h"
#include "../Map/MapFloorLibrary.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// ===== TP-1 클램프 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMapFloorClampSizeTest,
	"Park3D.MapFloor.ClampSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMapFloorClampSizeTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("범위 내 통과"), UMapFloorLibrary::ClampSizeMeters(160.f), 160.f, 1e-3f);
	TestEqual(TEXT("최소 미만 → 10"), UMapFloorLibrary::ClampSizeMeters(5.f), 10.f, 1e-3f);
	TestEqual(TEXT("최대 초과 → 500"), UMapFloorLibrary::ClampSizeMeters(5000.f), 500.f, 1e-3f);
	TestEqual(TEXT("음수 → 10"), UMapFloorLibrary::ClampSizeMeters(-20.f), 10.f, 1e-3f);
	TestEqual(TEXT("0 → 10(퇴화 메시 방지)"), UMapFloorLibrary::ClampSizeMeters(0.f), 10.f, 1e-3f);
	TestEqual(TEXT("경계 10 유지"), UMapFloorLibrary::ClampSizeMeters(10.f), 10.f, 1e-3f);
	TestEqual(TEXT("경계 500 유지"), UMapFloorLibrary::ClampSizeMeters(500.f), 500.f, 1e-3f);
	// 상한 500 은 Landscape 평탄 구간(반경 250m)의 실측 한계다. 이 값을 올리려면 지형을 먼저 평탄화해야 한다.
	TestEqual(TEXT("상한 상수 = 500"), UMapFloorLibrary::MaxSizeM, 500.f, 1e-3f);

	// NaN/Inf 는 FMath::Clamp 로 걸러지지 않으므로 기본값(160) 폴백이어야 한다.
	const float NaNValue = std::numeric_limits<float>::quiet_NaN();
	const float InfValue = std::numeric_limits<float>::infinity();
	TestEqual(TEXT("NaN → 기본값 160"), UMapFloorLibrary::ClampSizeMeters(NaNValue), 160.f, 1e-3f);
	TestEqual(TEXT("+Inf → 기본값 160"), UMapFloorLibrary::ClampSizeMeters(InfValue), 160.f, 1e-3f);
	TestEqual(TEXT("-Inf → 기본값 160"), UMapFloorLibrary::ClampSizeMeters(-InfValue), 160.f, 1e-3f);

	return true;
}

// ===== TP-2 크기 → 평면 스케일 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMapFloorSizeToScaleTest,
	"Park3D.MapFloor.SizeToScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMapFloorSizeToScaleTest::RunTest(const FString& Parameters)
{
	// /Engine/BasicShapes/Plane = 100uu 정사각 → 스케일은 미터값과 같아진다.
	const FVector Default = UMapFloorLibrary::MapSizeToPlaneScale(160.f, 160.f, 100.f, 100.f);
	TestEqual(TEXT("기본 스케일 X"), Default.X, 160.0, 1e-3);
	TestEqual(TEXT("기본 스케일 Y"), Default.Y, 160.0, 1e-3);
	TestEqual(TEXT("기본 스케일 Z=1"), Default.Z, 1.0, 1e-6);

	const FVector Extreme = UMapFloorLibrary::MapSizeToPlaneScale(500.f, 10.f, 100.f, 100.f);
	TestEqual(TEXT("최대/최소 스케일 X"), Extreme.X, 500.0, 1e-3);
	TestEqual(TEXT("최대/최소 스케일 Y"), Extreme.Y, 10.0, 1e-3);
	TestEqual(TEXT("평면이므로 Z 는 항상 1"), Extreme.Z, 1.0, 1e-6);

	return true;
}

// ===== TP-3 축 매핑 (비정방형 — X/Y 뒤바뀜 회귀 검출) =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMapFloorAxisMappingTest,
	"Park3D.MapFloor.AxisMapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMapFloorAxisMappingTest::RunTest(const FString& Parameters)
{
	// 정방형(160×160)만 테스트하면 축 뒤바뀜 버그가 드러나지 않는다(사전 영향분석 M-3).
	// 규약: UI "가로(X)"=Unity X → UE X, UI "세로(Z)"=Unity Z → UE Y.
	const FVector S1 = UMapFloorLibrary::MapSizeToPlaneScale(300.f, 100.f, 100.f, 100.f);
	TestEqual(TEXT("가로 300 → UE Scale.X"), S1.X, 300.0, 1e-3);
	TestEqual(TEXT("세로 100 → UE Scale.Y"), S1.Y, 100.0, 1e-3);
	TestEqual(TEXT("높이 스케일 불변"), S1.Z, 1.0, 1e-6);

	// 영향분석이 지정한 비정방형 케이스(120×200) — 가로 < 세로 인 반대 방향도 검증.
	const FVector S2 = UMapFloorLibrary::MapSizeToPlaneScale(120.f, 200.f, 100.f, 100.f);
	TestEqual(TEXT("가로 120 → UE Scale.X"), S2.X, 120.0, 1e-3);
	TestEqual(TEXT("세로 200 → UE Scale.Y"), S2.Y, 200.0, 1e-3);
	TestTrue(TEXT("X 와 Y 가 서로 뒤바뀌지 않았다"), S2.X < S2.Y);

	return true;
}

// ===== TP-4 텍스트 파싱 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMapFloorParseSizeTest,
	"Park3D.MapFloor.ParseSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMapFloorParseSizeTest::RunTest(const FString& Parameters)
{
	float V = 0.f;

	TestTrue(TEXT("\"160\" 파싱 성공"), UMapFloorLibrary::ParseSizeMeters(TEXT("160"), V));
	TestEqual(TEXT("\"160\" 값"), V, 160.f, 1e-3f);

	TestTrue(TEXT("\"160.5\" 파싱 성공"), UMapFloorLibrary::ParseSizeMeters(TEXT("160.5"), V));
	TestEqual(TEXT("\"160.5\" 값"), V, 160.5f, 1e-3f);

	TestTrue(TEXT("\"-5\" 파싱 성공(부호 허용, 클램프는 별도)"), UMapFloorLibrary::ParseSizeMeters(TEXT("-5"), V));
	TestEqual(TEXT("\"-5\" 값"), V, -5.f, 1e-3f);

	// 실패 케이스: OutMeters 를 건드리지 않아야 한다(호출부가 기존 값을 유지할 수 있도록).
	const float Sentinel = 777.f;
	V = Sentinel;
	TestFalse(TEXT("빈 문자열 실패"), UMapFloorLibrary::ParseSizeMeters(TEXT(""), V));
	TestEqual(TEXT("빈 문자열 시 출력 미변경"), V, Sentinel, 1e-3f);

	V = Sentinel;
	TestFalse(TEXT("\"abc\" 실패"), UMapFloorLibrary::ParseSizeMeters(TEXT("abc"), V));
	TestEqual(TEXT("\"abc\" 시 출력 미변경"), V, Sentinel, 1e-3f);

	V = Sentinel;
	TestFalse(TEXT("\"12abc\" 실패(Atof 단독으로는 못 잡는 케이스)"), UMapFloorLibrary::ParseSizeMeters(TEXT("12abc"), V));
	TestEqual(TEXT("\"12abc\" 시 출력 미변경"), V, Sentinel, 1e-3f);

	return true;
}

// ===== TP-5 단위 사슬 (m → uu) =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMapFloorUnitChainTest,
	"Park3D.MapFloor.UnitChain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMapFloorUnitChainTest::RunTest(const FString& Parameters)
{
	// 1m = 100uu 규약. 스케일 × 베이스메시(100uu) = 실제 폭(uu) 이어야 한다.
	const float PlaneBaseUU = 100.f;
	const FVector S = UMapFloorLibrary::MapSizeToPlaneScale(160.f, 160.f, 100.f, PlaneBaseUU);

	TestEqual(TEXT("실제 폭(uu) = 16000"), S.X * PlaneBaseUU, 16000.0, 1e-2);
	TestEqual(TEXT("실제 깊이(uu) = 16000"), S.Y * PlaneBaseUU, 16000.0, 1e-2);

	// 비정방형에서도 단위 사슬이 성립.
	const FVector S2 = UMapFloorLibrary::MapSizeToPlaneScale(120.f, 200.f, 100.f, PlaneBaseUU);
	TestEqual(TEXT("120m → 12000uu"), S2.X * PlaneBaseUU, 12000.0, 1e-2);
	TestEqual(TEXT("200m → 20000uu"), S2.Y * PlaneBaseUU, 20000.0, 1e-2);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
