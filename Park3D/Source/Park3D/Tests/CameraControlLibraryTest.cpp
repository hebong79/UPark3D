// Copyright Epic Games, Inc. All Rights Reserved.
// CameraControlLibraryTest : UCameraControlLibrary 순수함수 + JSON 입출력 유닛테스트.
// 설계 §10.1 테스트 포인트:
//  TP-COORD(좌표 라운드트립+축 스케일), TP-FOV(줌↔FOV, clamp, 라운드트립),
//  TP-SLIDER(슬라이더 매핑, Min==Max 방어), TP-JSON(라운드트립 + §12-C 보정 + Unity 2단 중첩 픽스처),
//  TP-ANGLE(수직/수평각 부호, 수평거리0→±90), TP-LINE(직교점 좌표, 좌우각 부호).
// PIE 불필요 — 자동화 프레임워크에서 에디터 컨텍스트로 실행.

#include "Misc/AutomationTest.h"
#include "../CameraControlLibrary.h"
#include "../CameraControlTypes.h"
#include "../CameraControlManager.h"
#include "../PTZCameraActor.h"
#include "Components/SceneCaptureComponent2D.h"   // TP-FOV: SetZoom 후 Capture->FOVAngle 절대값 단정
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

// ===== TP-COORD 좌표 라운드트립 + 축 스케일 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraControlCoordTest,
	"Park3D.CameraControl.Coord",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCameraControlCoordTest::RunTest(const FString& Parameters)
{
	const float Tolerance = 1e-3f;
	const FCamVec3 Inputs[] = {
		{0.f, 0.f, 0.f},
		{1.f, 0.f, 2.f},
		{-5.5f, 3.25f, -10.125f},
		{12.68f, -0.05f, 21.16f},
		{100.f, 50.f, -75.f},
	};

	for (const FCamVec3& V : Inputs)
	{
		const FVector UE = UCameraControlLibrary::UnityPosToUE(V, 100.f);
		const FCamVec3 Back = UCameraControlLibrary::UEToUnityPos(UE, 100.f);
		TestEqual(TEXT("x 라운드트립"), Back.x, V.x, Tolerance);
		TestEqual(TEXT("y 라운드트립"), Back.y, V.y, Tolerance);
		TestEqual(TEXT("z 라운드트립"), Back.z, V.z, Tolerance);
	}

	// 물리 방향 보존: Unity(1,0,2) -> UE(200, 100, 0) (z→X, x→Y, y→Z).
	const FVector Scaled = UCameraControlLibrary::UnityPosToUE({1.f, 0.f, 2.f}, 100.f);
	TestEqual(TEXT("스케일 X"), Scaled.X, 200.0, 1e-3);
	TestEqual(TEXT("스케일 Y"), Scaled.Y, 100.0, 1e-3);
	TestEqual(TEXT("스케일 Z"), Scaled.Z, 0.0, 1e-3);

	// 높이(Unity y) → UE Z 확인.
	const FVector Height = UCameraControlLibrary::UnityPosToUE({0.f, 5.f, 0.f}, 100.f);
	TestEqual(TEXT("높이 → UE Z"), Height.Z, 500.0, 1e-3);

	return true;
}

// ===== TP-FOV 줌 ↔ FOV =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraControlFovTest,
	"Park3D.CameraControl.Fov",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCameraControlFovTest::RunTest(const FString& Parameters)
{
	// 탄젠트 광학 모델(HNR-2036LA 규격 정정 설계 §6.1): hfov = 2·atan(tan(56.5°/2)/z).
	// 기대값은 구현식을 베끼지 않도록 독립 계산한 소수 5자리 리터럴(절삭 오차 ≤5e-6) — 허용치 1e-3 유지.
	TestEqual(TEXT("zoom1→56.500"), UCameraControlLibrary::ZoomToHFov(1.f), 56.50000f, 1e-3f);
	TestEqual(TEXT("zoom2→30.076"), UCameraControlLibrary::ZoomToHFov(2.f), 30.07595f, 1e-3f);
	TestEqual(TEXT("zoom3→20.309"), UCameraControlLibrary::ZoomToHFov(3.f), 20.30875f, 1e-3f);
	TestEqual(TEXT("zoom4→15.301"), UCameraControlLibrary::ZoomToHFov(4.f), 15.30147f, 1e-3f);
	TestEqual(TEXT("zoom5→12.267"), UCameraControlLibrary::ZoomToHFov(5.f), 12.26737f, 1e-3f);
	TestEqual(TEXT("zoom8→7.685"), UCameraControlLibrary::ZoomToHFov(8.f), 7.68499f, 1e-3f);
	TestEqual(TEXT("zoom16→3.847"), UCameraControlLibrary::ZoomToHFov(16.f), 3.84682f, 1e-3f);
	TestEqual(TEXT("zoom20→3.078"), UCameraControlLibrary::ZoomToHFov(20.f), 3.07787f, 1e-3f);
	TestEqual(TEXT("zoom36→1.710"), UCameraControlLibrary::ZoomToHFov(36.f), 1.71021f, 1e-3f);

	// clamp(설계 §6.2 — 기존 계약 전부 보존): zoom<1(0 포함) → 1 선클램프 → 56.5, zoom>36 → 36 → ≈1.710.
	TestEqual(TEXT("zoom0→56.5(선클램프)"), UCameraControlLibrary::ZoomToHFov(0.f), 56.50000f, 1e-3f);
	TestEqual(TEXT("zoom-5→56.5(선클램프)"), UCameraControlLibrary::ZoomToHFov(-5.f), 56.50000f, 1e-3f);
	TestEqual(TEXT("zoom0.999→56.5(하한 경계)"), UCameraControlLibrary::ZoomToHFov(0.999f), 56.50000f, 1e-3f);
	TestEqual(TEXT("zoom100→36클램프"), UCameraControlLibrary::ZoomToHFov(100.f), 1.71021f, 1e-3f);
	TestEqual(TEXT("MaxZoom0.5→1보정"), UCameraControlLibrary::ZoomToHFov(10.f, 0.5f), 56.50000f, 1e-3f);

	// 역함수: HFovToZoom (설계 §6.3).
	TestEqual(TEXT("hfov56.5→1"), UCameraControlLibrary::HFovToZoom(56.50000f), 1.f, 1e-3f);
	TestEqual(TEXT("hfov30.076→2"), UCameraControlLibrary::HFovToZoom(30.07595f), 2.f, 1e-3f);
	TestEqual(TEXT("hfov20.309→3"), UCameraControlLibrary::HFovToZoom(20.30875f), 3.f, 1e-3f);
	TestEqual(TEXT("hfov15.301→4"), UCameraControlLibrary::HFovToZoom(15.30147f), 4.f, 1e-3f);
	TestEqual(TEXT("hfov12.267→5"), UCameraControlLibrary::HFovToZoom(12.26737f), 5.f, 1e-3f);
	TestEqual(TEXT("hfov7.685→8"), UCameraControlLibrary::HFovToZoom(7.68499f), 8.f, 1e-3f);
	TestEqual(TEXT("hfov3.847→16"), UCameraControlLibrary::HFovToZoom(3.84682f), 16.f, 1e-3f);
	TestEqual(TEXT("hfov3.078→20"), UCameraControlLibrary::HFovToZoom(3.07787f), 20.f, 1e-3f);
	TestEqual(TEXT("hfov1.710→36"), UCameraControlLibrary::HFovToZoom(1.71021f), 36.f, 1e-3f);

	// 정의역 방어(규격 정정 설계 §3.2 W1~W3, 영향도 G-5): 탄젠트 특이점에서 0나눗셈·부호반전·발산이 없어야 한다.
	TestEqual(TEXT("hfov0→36(조기반환)"), UCameraControlLibrary::HFovToZoom(0.f), 36.f, 1e-3f);
	TestEqual(TEXT("hfov-1→36(음수)"), UCameraControlLibrary::HFovToZoom(-1.f), 36.f, 1e-3f);
	TestEqual(TEXT("hfov0.0001→36(상한 클램프)"), UCameraControlLibrary::HFovToZoom(0.0001f), 36.f, 1e-3f);
	TestEqual(TEXT("hfov90→1(기본보다 광각)"), UCameraControlLibrary::HFovToZoom(90.f), 1.f, 1e-3f);
	TestEqual(TEXT("hfov180→1(발산 방어 W3)"), UCameraControlLibrary::HFovToZoom(180.f), 1.f, 1e-3f);
	TestEqual(TEXT("hfov200→1(부호반전 방어 W2)"), UCameraControlLibrary::HFovToZoom(200.f), 1.f, 1e-3f);
	TestEqual(TEXT("hfov360→1(0나눗셈 방어 W1)"), UCameraControlLibrary::HFovToZoom(360.f), 1.f, 1e-3f);
	TestEqual(TEXT("MaxZoom0.5→1보정(역방향)"), UCameraControlLibrary::HFovToZoom(30.f, 0.5f), 1.f, 1e-3f);

	// DefaultHFov 정의역 이탈(W5 — EditAnywhere UPROPERTY 라 0·200 설정이 가능하다).
	const float DegenerateHFov = UCameraControlLibrary::ZoomToHFov(1.f, 36.f, 0.f);
	TestTrue(TEXT("DefaultHFov0→(0,1) 유한 양수 화각(음수·NaN 없음)"), DegenerateHFov > 0.f && DegenerateHFov < 1.f);
	const float WideHFov = UCameraControlLibrary::ZoomToHFov(1.f, 36.f, 200.f);
	TestTrue(TEXT("DefaultHFov200→(0,180) 유한 화각(발산·부호반전 없음)"), WideHFov > 0.f && WideHFov < 180.f);
	TestEqual(TEXT("DefaultHFov0→역방향 1"), UCameraControlLibrary::HFovToZoom(30.f, 36.f, 0.f), 1.f, 1e-3f);
	TestEqual(TEXT("DefaultHFov200→역방향 36"), UCameraControlLibrary::HFovToZoom(30.f, 36.f, 200.f), 36.f, 1e-3f);

	// 라운드트립: HFovToZoom(ZoomToHFov(z)) ≈ z (z ∈ [1,36]). 전 구간 실측 최악 오차 5.65e-6.
	const float Zooms[] = {1.f, 2.f, 5.f, 10.f, 20.f, 36.f};
	for (float Z : Zooms)
	{
		const float RT = UCameraControlLibrary::HFovToZoom(UCameraControlLibrary::ZoomToHFov(Z));
		TestEqual(TEXT("줌 라운드트립"), RT, Z, 1e-3f);
	}

	// CDO 일치(설계 §6.4 N1, 영향도 G-1): 56.5 는 PTZCameraActor.h UPROPERTY + 라이브러리 기본 인자 2곳에
	// 중복 존재한다. 한쪽만 고치면 "액터 경유는 56.5, 라이브러리 직접 호출은 58" 로 모델이 분열되므로 봉인한다.
	const APTZCameraActor* CamCdo = GetDefault<APTZCameraActor>();
	if (TestNotNull(TEXT("APTZCameraActor CDO"), CamCdo))
	{
		TestEqual(TEXT("CDO DefaultHFov == 라이브러리 기본 인자"),
			CamCdo->DefaultHFov, UCameraControlLibrary::ZoomToHFov(1.f), 1e-4f);
		TestEqual(TEXT("CDO MaxZoom == 36"), CamCdo->MaxZoom, 36.f, 1e-4f);
	}

	// 16:9 수직 화각 = 2·atan(tan(H/2)·9/16) → HNR-2036LA 사양서 V 33.63° 대조(설계 §6.4 N2).
	const float VFovDeg = FMath::RadiansToDegrees(2.f * FMath::Atan(
		FMath::Tan(FMath::DegreesToRadians(UCameraControlLibrary::ZoomToHFov(1.f) * 0.5f)) * 9.f / 16.f));
	TestEqual(TEXT("16:9 수직 화각 → 사양서 33.63"), VFovDeg, 33.63406f, 0.01f);

	// 액터 경유 절대값(영향도 G-12): SetZoom → Capture->FOVAngle 의 절대값을 검증한다.
	// RpcServerTest 의 setPTZ/getPTZ 왕복은 정/역 상쇄로 어떤 모델에서도 통과하므로 모델 회귀를 못 잡는다.
	UWorld* World = (GEngine && GEngine->GetWorldContexts().Num() > 0) ? GWorld : nullptr;
	if (!World)
	{
		AddWarning(TEXT("에디터 월드 없음 — SetZoom→FOVAngle 절대값 검증 건너뜀."));
		return true;
	}

	APTZCameraActor* Camera = World->SpawnActor<APTZCameraActor>();
	if (TestNotNull(TEXT("PTZ Camera 스폰"), Camera))
	{
		Camera->SetZoom(2.f);
		TestEqual(TEXT("SetZoom(2)→FOVAngle 30.076(절대값)"), Camera->Capture->FOVAngle, 30.07595f, 1e-3f);
		TestEqual(TEXT("SetZoom(2)→GetZoom 2.0"), Camera->GetZoom(), 2.f, 1e-3f);
		Camera->SetZoom(1.f);
		TestEqual(TEXT("SetZoom(1)→FOVAngle 56.500(절대값)"), Camera->Capture->FOVAngle, 56.50000f, 1e-3f);
		Camera->SetZoom(36.f);
		TestEqual(TEXT("SetZoom(36)→FOVAngle 1.710(절대값)"), Camera->Capture->FOVAngle, 1.71021f, 1e-3f);
		Camera->Destroy();
	}

	return true;
}

// ===== TP-SLIDER 슬라이더 매핑 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraControlSliderTest,
	"Park3D.CameraControl.Slider",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCameraControlSliderTest::RunTest(const FString& Parameters)
{
	// SliderToValue(0/1/0.5) = min/max/중앙.
	TestEqual(TEXT("slider0→min"), UCameraControlLibrary::SliderToValue(0.f, 10.f, 20.f), 10.f, 1e-3f);
	TestEqual(TEXT("slider1→max"), UCameraControlLibrary::SliderToValue(1.f, 10.f, 20.f), 20.f, 1e-3f);
	TestEqual(TEXT("slider0.5→중앙"), UCameraControlLibrary::SliderToValue(0.5f, 10.f, 20.f), 15.f, 1e-3f);

	// 음수 범위 포함.
	TestEqual(TEXT("음수범위 중앙"), UCameraControlLibrary::SliderToValue(0.5f, -180.f, 180.f), 0.f, 1e-3f);

	// ValueToSlider 역변환.
	TestEqual(TEXT("value15→0.5"), UCameraControlLibrary::ValueToSlider(15.f, 10.f, 20.f), 0.5f, 1e-3f);
	TestEqual(TEXT("value10→0"), UCameraControlLibrary::ValueToSlider(10.f, 10.f, 20.f), 0.f, 1e-3f);
	TestEqual(TEXT("value20→1"), UCameraControlLibrary::ValueToSlider(20.f, 10.f, 20.f), 1.f, 1e-3f);

	// Min==Max 0나눗셈 방어 → 0.
	TestEqual(TEXT("Min==Max 방어"), UCameraControlLibrary::ValueToSlider(15.f, 10.f, 10.f), 0.f, 1e-3f);

	// 슬라이더 라운드트립.
	const float Vals[] = {10.f, 12.5f, 15.f, 18.3f, 20.f};
	for (float V : Vals)
	{
		const float S = UCameraControlLibrary::ValueToSlider(V, 10.f, 20.f);
		const float Back = UCameraControlLibrary::SliderToValue(S, 10.f, 20.f);
		TestEqual(TEXT("슬라이더 라운드트립"), Back, V, 1e-3f);
	}

	return true;
}

// ===== TP-ROT PTZ 회전 부호 (§11 가정 검증) =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraControlRotTest,
	"Park3D.CameraControl.Rot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCameraControlRotTest::RunTest(const FString& Parameters)
{
	// Yaw=Pan, Pitch=-Tilt, Roll=0. (FRotator 멤버는 double → double 리터럴로 비교, 오버로드 모호성 방지)
	const FRotator R = UCameraControlLibrary::PanTiltToRotator(45.f, 20.f);
	TestEqual(TEXT("Yaw=Pan"), R.Yaw, 45.0, 1e-3);
	TestEqual(TEXT("Pitch=-Tilt"), R.Pitch, -20.0, 1e-3);
	TestEqual(TEXT("Roll=0"), R.Roll, 0.0, 1e-3);

	// 역변환 라운드트립.
	float Pan = 0.f, Tilt = 0.f;
	UCameraControlLibrary::RotatorToPanTilt(R, Pan, Tilt);
	TestEqual(TEXT("Pan 라운드트립"), Pan, 45.f, 1e-3f);
	TestEqual(TEXT("Tilt 라운드트립"), Tilt, 20.f, 1e-3f);

	return true;
}

// ===== TP-ANGLE 수직/수평각 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraControlAngleTest,
	"Park3D.CameraControl.Angle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCameraControlAngleTest::RunTest(const FString& Parameters)
{
	float Vert = 0.f, Horz = 0.f;

	// 카메라(0,0,10) 높이 10, 기준점(10,0,0) 정면. 타겟 우측(+Y): (10,5,0).
	UCameraControlLibrary::VertHorzAngleToTarget(
		FVector(0, 0, 10), FVector(10, 5, 0), FVector(10, 0, 0), Vert, Horz);
	// 수평거리 = sqrt(10^2+5^2)=11.180, 높이차=10 → 수직각 atan2(10,11.18)=41.81°(내림 +).
	TestEqual(TEXT("수직각 내림(+)"), Vert, FMath::RadiansToDegrees(FMath::Atan2(10.f, FMath::Sqrt(125.f))), 1e-2f);
	TestTrue(TEXT("수직각 양수(내림)"), Vert > 0.f);
	// 수평각: 우측(+Y) → (+). 크기 = acos(10/11.18)=26.57°.
	TestTrue(TEXT("수평각 우측(+)"), Horz > 0.f);
	TestEqual(TEXT("수평각 크기"), Horz, 26.565f, 1e-2f);

	// 타겟 좌측(-Y): (10,-5,0) → 수평각 (-).
	UCameraControlLibrary::VertHorzAngleToTarget(
		FVector(0, 0, 10), FVector(10, -5, 0), FVector(10, 0, 0), Vert, Horz);
	TestTrue(TEXT("수평각 좌측(-)"), Horz < 0.f);
	TestEqual(TEXT("수평각 좌측 크기"), Horz, -26.565f, 1e-2f);

	// 올려다봄: 카메라(0,0,0), 타겟(10,0,10) → 높이차 -10 → 수직각 음수(올림 -).
	UCameraControlLibrary::VertHorzAngleToTarget(
		FVector(0, 0, 0), FVector(10, 0, 10), FVector(10, 0, 0), Vert, Horz);
	TestTrue(TEXT("수직각 올림(-)"), Vert < 0.f);

	// 수평거리≈0 폴백: 타겟이 카메라 바로 아래 → +90(카메라가 위).
	UCameraControlLibrary::VertHorzAngleToTarget(
		FVector(0, 0, 10), FVector(0, 0, 0), FVector(10, 0, 0), Vert, Horz);
	TestEqual(TEXT("수평거리0 카메라위 →+90"), Vert, 90.f, 1e-3f);

	// 수평거리≈0 폴백: 타겟이 카메라 바로 위 → -90.
	UCameraControlLibrary::VertHorzAngleToTarget(
		FVector(0, 0, 0), FVector(0, 0, 10), FVector(10, 0, 0), Vert, Horz);
	TestEqual(TEXT("수평거리0 카메라아래 →-90"), Vert, -90.f, 1e-3f);

	// 거리 함수.
	TestEqual(TEXT("DistanceXZ 높이무시"),
		UCameraControlLibrary::DistanceXZ(FVector(0, 0, 100), FVector(3, 4, 0)), 5.f, 1e-3f);
	TestEqual(TEXT("Distance3D"),
		UCameraControlLibrary::Distance3D(FVector(0, 0, 0), FVector(0, 0, 10)), 10.f, 1e-3f);
	// CPCamDistDlg 표시 단위: UE cm 결과를 m 텍스트로 변환한다.
	TestEqual(TEXT("거리 UI 250cm→2.5m"), UCameraControlLibrary::WorldCentimetersToMeters(250.f), 2.5f, 1e-3f);
	TestEqual(TEXT("거리 UI 잘못된 계수 방어"), UCameraControlLibrary::WorldCentimetersToMeters(250.f, 0.f), 2.5f, 1e-3f);

	return true;
}

// ===== TP-LINE 타겟라인 좌우각 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraControlLineTest,
	"Park3D.CameraControl.Line",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCameraControlLineTest::RunTest(const FString& Parameters)
{
	FVector RefPt = FVector::ZeroVector;
	float StartDeg = 0.f, EndDeg = 0.f;

	// 카메라(0,0,5), 라인 S(10,-5,0) ~ E(10,5,0) (UE Y축 평행, X=10).
	// 직교점(수선의 발) = (10,0,0). 시작점 좌측(-Y) → (-), 끝점 우측(+Y) → (+).
	UCameraControlLibrary::TargetLineAngles(
		FVector(0, 0, 5), FVector(10, -5, 0), FVector(10, 5, 0), RefPt, StartDeg, EndDeg);

	// FVector 멤버는 double → double 리터럴로 비교(오버로드 모호성 방지).
	TestEqual(TEXT("직교점 X"), RefPt.X, 10.0, 1e-3);
	TestEqual(TEXT("직교점 Y"), RefPt.Y, 0.0, 1e-3);
	TestEqual(TEXT("직교점 Z(높이제거)"), RefPt.Z, 0.0, 1e-3);
	TestTrue(TEXT("시작점 좌측(-)"), StartDeg < 0.f);
	TestTrue(TEXT("끝점 우측(+)"), EndDeg > 0.f);
	// 크기 = atan2(5,10) = 26.565°.
	TestEqual(TEXT("시작각 크기"), StartDeg, -26.565f, 1e-2f);
	TestEqual(TEXT("끝각 크기"), EndDeg, 26.565f, 1e-2f);

	// 라인 길이≈0 폴백: 직교점=시작점, 각도 0.
	UCameraControlLibrary::TargetLineAngles(
		FVector(0, 0, 5), FVector(10, 3, 0), FVector(10, 3, 0), RefPt, StartDeg, EndDeg);
	TestEqual(TEXT("길이0 직교점=시작 X"), RefPt.X, 10.0, 1e-3);
	TestEqual(TEXT("길이0 직교점=시작 Y"), RefPt.Y, 3.0, 1e-3);
	TestEqual(TEXT("길이0 시작각 0"), StartDeg, 0.f, 1e-3f);
	TestEqual(TEXT("길이0 끝각 0"), EndDeg, 0.f, 1e-3f);

	return true;
}

// ===== TP-JSON 라운드트립 + §12-C 보정 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraControlJsonRoundTripTest,
	"Park3D.CameraControl.JsonRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCameraControlJsonRoundTripTest::RunTest(const FString& Parameters)
{
	// 2대 카메라, 카메라별 프리셋(2단 중첩 datas) 구성.
	FCameraPosList Src;
	{
		FCameraPos Cam0;
		Cam0.target_pos = 0.f;
		{
			FCamDir D; D.idx = 1; D.sname = TEXT("Preset 1"); D.cam_id = 1; D.preset_id = 1;
			D.pos = {12.68f, 5.f, 21.16f}; D.rot = {15.f, 45.f, 0.f};
			D.pan = 45.f; D.tilt = 15.f; D.zoom = 4.f;
			D.ptzmin = {-180.f, -90.f, 1.f}; D.ptzmax = {180.f, 90.f, 36.f};
			Cam0.datas.Add(D);
		}
		{
			FCamDir D; D.idx = 2; D.sname = TEXT("Preset 2"); D.cam_id = 1; D.preset_id = 2;
			D.pos = {-3.5f, 8.f, 9.9f}; D.rot = {-10.f, 271.13f, 0.f};
			D.pan = 271.13f; D.tilt = -10.f; D.zoom = 12.f;
			D.ptzmin = {-180.f, -90.f, 1.f}; D.ptzmax = {180.f, 90.f, 36.f};
			Cam0.datas.Add(D);
		}
		Src.datas.Add(Cam0);

		FCameraPos Cam1;
		Cam1.target_pos = 2.5f;
		{
			FCamDir D; D.idx = 1; D.sname = TEXT("Preset 1"); D.cam_id = 2; D.preset_id = 1;
			D.pos = {30.f, 6.f, -5.f}; D.rot = {20.f, 90.f, 0.f};
			D.pan = 90.f; D.tilt = 20.f; D.zoom = 1.f;
			D.ptzmin = {-180.f, -90.f, 1.f}; D.ptzmax = {180.f, 90.f, 36.f};
			Cam1.datas.Add(D);
		}
		Src.datas.Add(Cam1);
	}

	const FString TempPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Test_CamPos_RoundTrip.json"));
	TestTrue(TEXT("저장 성공"), UCameraControlLibrary::SaveToJson(TempPath, Src));

	FCameraPosList Loaded;
	TestTrue(TEXT("로드 성공"), UCameraControlLibrary::LoadFromJson(TempPath, Loaded));
	TestTrue(TEXT("새 UE 파일은 플래그 유지"), Loaded.isUnreal);
	TestEqual(TEXT("카메라 수 동일"), Loaded.datas.Num(), Src.datas.Num());

	if (Loaded.datas.Num() == Src.datas.Num())
	{
		for (int32 c = 0; c < Src.datas.Num(); ++c)
		{
			const FCameraPos& S = Src.datas[c];
			const FCameraPos& L = Loaded.datas[c];
			TestEqual(TEXT("target_pos"), L.target_pos, S.target_pos, 1e-3f);
			TestEqual(TEXT("프리셋 수 동일"), L.datas.Num(), S.datas.Num());
			if (L.datas.Num() != S.datas.Num()) continue;

			for (int32 i = 0; i < S.datas.Num(); ++i)
			{
				const FCamDir& SD = S.datas[i];
				const FCamDir& LD = L.datas[i];
				TestEqual(TEXT("idx"), LD.idx, SD.idx);
				TestEqual(TEXT("sname"), LD.sname, SD.sname);
				TestEqual(TEXT("cam_id"), LD.cam_id, SD.cam_id);
				TestEqual(TEXT("preset_id"), LD.preset_id, SD.preset_id);
				TestEqual(TEXT("pos.x"), LD.pos.x, SD.pos.x, 1e-3f);
				TestEqual(TEXT("pos.y"), LD.pos.y, SD.pos.y, 1e-3f);
				TestEqual(TEXT("pos.z"), LD.pos.z, SD.pos.z, 1e-3f);
				TestEqual(TEXT("rot.x"), LD.rot.x, SD.rot.x, 1e-3f);
				TestEqual(TEXT("rot.y"), LD.rot.y, SD.rot.y, 1e-3f);
				TestEqual(TEXT("zoom"), LD.zoom, SD.zoom, 1e-3f);
				TestEqual(TEXT("ptzmin.p"), LD.ptzmin.p, SD.ptzmin.p, 1e-3f);
				TestEqual(TEXT("ptzmax.z"), LD.ptzmax.z, SD.ptzmax.z, 1e-3f);
				// 로드 후 pan=rot.y, tilt=rot.x 동기화.
				TestEqual(TEXT("pan=rot.y"), LD.pan, SD.rot.y, 1e-3f);
				TestEqual(TEXT("tilt=rot.x"), LD.tilt, SD.rot.x, 1e-3f);
			}
		}
	}

	// 직렬화 키가 소문자 Unity 호환인지 검증(2단 중첩 datas / target_pos / cam_id / preset_id / ptzmin / p-t-z).
	FString RawJson;
	FFileHelper::LoadFileToString(RawJson, *TempPath);
	TestTrue(TEXT("키 datas"), RawJson.Contains(TEXT("\"datas\""), ESearchCase::CaseSensitive));
	TestTrue(TEXT("키 target_pos"), RawJson.Contains(TEXT("\"target_pos\""), ESearchCase::CaseSensitive));
	TestTrue(TEXT("키 cam_id"), RawJson.Contains(TEXT("\"cam_id\""), ESearchCase::CaseSensitive));
	TestTrue(TEXT("키 preset_id"), RawJson.Contains(TEXT("\"preset_id\""), ESearchCase::CaseSensitive));
	TestTrue(TEXT("키 ptzmin"), RawJson.Contains(TEXT("\"ptzmin\""), ESearchCase::CaseSensitive));
	TestTrue(TEXT("키 ptzmax"), RawJson.Contains(TEXT("\"ptzmax\""), ESearchCase::CaseSensitive));
	TestTrue(TEXT("UE 플래그 true 저장"), RawJson.Contains(TEXT("\"isUnreal\":true"), ESearchCase::CaseSensitive)
		|| RawJson.Contains(TEXT("\"isUnreal\": true"), ESearchCase::CaseSensitive));
	// SPtz 소문자 p/t/z 키.
	TestTrue(TEXT("키 p"), RawJson.Contains(TEXT("\"p\""), ESearchCase::CaseSensitive));
	TestFalse(TEXT("대문자 X 키 없음"), RawJson.Contains(TEXT("\"X\""), ESearchCase::CaseSensitive));

	FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*TempPath);
	return true;
}

// ===== TP-JSON 보정 + Unity 2단 중첩 픽스처 로드 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraControlJsonFixtureTest,
	"Park3D.CameraControl.JsonFixture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCameraControlJsonFixtureTest::RunTest(const FString& Parameters)
{
	// Unity CSaveInitCampPos.SaveToJson 산출 구조(2단 중첩 datas)를 모사한 픽스처.
	// 보정 대상: ptzmax.z=360→36, preset_id=0→1, zoom=0→1, rot(x=15,y=45)→pan=45/tilt=15.
	const FString Fixture = TEXT(
		"{\"datas\":["
		"{\"target_pos\":0.0,\"datas\":["
		"{\"idx\":1,\"sname\":\"Preset 1\",\"cam_id\":1,\"preset_id\":0,"
		"\"pos\":{\"x\":12.68,\"y\":5.0,\"z\":21.16},"
		"\"rot\":{\"x\":15.0,\"y\":45.0,\"z\":0.0},"
		"\"pan\":0.0,\"tilt\":0.0,\"zoom\":0.0,"
		"\"ptzmin\":{\"p\":-180.0,\"t\":-90.0,\"z\":1.0},"
		"\"ptzmax\":{\"p\":180.0,\"t\":90.0,\"z\":360.0}"
		"}]}"
		"]}");

	const FString TempPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Test_CamPos_Fixture.json"));
	TestTrue(TEXT("픽스처 쓰기"), FFileHelper::SaveStringToFile(Fixture, *TempPath));

	FCameraPosList Loaded;
	TestTrue(TEXT("픽스처 로드"), UCameraControlLibrary::LoadFromJson(TempPath, Loaded));
	TestTrue(TEXT("legacy 픽스처는 내부 UE로 정규화"), Loaded.isUnreal);
	TestEqual(TEXT("카메라 1대"), Loaded.datas.Num(), 1);

	if (Loaded.datas.Num() == 1 && Loaded.datas[0].datas.Num() == 1)
	{
		const FCamDir& D = Loaded.datas[0].datas[0];
		// 스키마 파싱 확인.
		TestEqual(TEXT("sname"), D.sname, FString(TEXT("Preset 1")));
		TestEqual(TEXT("Unity z→UE x"), D.pos.x, 21.16f, 1e-2f);
		TestEqual(TEXT("Unity x→UE y"), D.pos.y, 12.68f, 1e-2f);
		TestEqual(TEXT("Unity y→UE z"), D.pos.z, 5.f, 1e-3f);
		TestEqual(TEXT("ptzmin.p"), D.ptzmin.p, -180.f, 1e-3f);
		// §12-C 보정.
		TestEqual(TEXT("preset_id 0→1"), D.preset_id, 1);
		TestEqual(TEXT("ptzmax.z 360→36"), D.ptzmax.z, 36.f, 1e-3f);
		TestEqual(TEXT("zoom 0→1"), D.zoom, 1.f, 1e-3f);
		// rot↔pan/tilt 동기화.
		TestEqual(TEXT("pan=rot.y=45"), D.pan, 45.f, 1e-3f);
		TestEqual(TEXT("tilt=rot.x=15"), D.tilt, 15.f, 1e-3f);
	}
	else
	{
		AddError(TEXT("픽스처 2단 중첩 datas 파싱 실패"));
	}

	FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*TempPath);
	return true;
}

// ===== TP-WORLD 내부 Unreal 미터 → Manager/PTZ 실제 월드 위치 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraControlManagerWorldApplyTest,
	"Park3D.CameraControl.ManagerWorldApply",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCameraControlManagerWorldApplyTest::RunTest(const FString& Parameters)
{
	UWorld* World = (GEngine && GEngine->GetWorldContexts().Num() > 0) ? GWorld : nullptr;
	if (!World)
	{
		AddWarning(TEXT("에디터 월드 없음 — Camera Manager 월드 적용 검증 건너뜀."));
		return true;
	}

	ACameraControlManager* Manager = World->SpawnActor<ACameraControlManager>();
	if (!TestNotNull(TEXT("Camera Manager 스폰"), Manager))
	{
		return false;
	}
	APTZCameraActor* Camera = Manager->AddCamera(TEXT("Automation"));
	if (!TestNotNull(TEXT("PTZ Camera 스폰"), Camera))
	{
		Manager->Destroy();
		return false;
	}

	// Legacy Unity (12.68,5,21.16)를 이미 정규화한 내부 UE 미터 (21.16,12.68,5).
	FCamDir Dir;
	Dir.pos = {21.16f, 12.68f, 5.f};
	Dir.pan = 45.f;
	Dir.tilt = 15.f;
	Dir.zoom = 4.f;
	Manager->ApplyDir(0, Dir);

	TestTrue(TEXT("내부 UE m -> PTZ 월드 cm 축/높이 적용"),
		Camera->GetActorLocation().Equals(FVector(2116.f, 1268.f, 500.f), 1e-2f));

	Camera->Destroy();
	Manager->Destroy();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
