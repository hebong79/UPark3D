// Copyright Epic Games, Inc. All Rights Reserved.
// CameraControlOpticsTest : 설정 파일(camera.hfov_wide)이 카메라 풀에 적용되는 경로 검증.
// 화각 설정화 설계 §8.2(검증) / §8.3(적용) / §8.4(비대칭 격리):
//  ManagerOptics    — AddCamera 주입 + 기존 카메라 '재적용'(GetZoom→필드→SetZoom)으로 배율이 보존되는가.
//  HFovValidation   — (0,180) 밖은 무시 + 경고 + '직전 유효값' 유지인가.
//  OpticsIsolation  — 매니저 경유만 config 를 따르고 CDO·라이브러리 기본 인자는 불변인가.
// 에디터 월드에 매니저를 스폰해 확인하고 즉시 정리(PIE 불필요).

#include "Misc/AutomationTest.h"
#include "../CameraControlManager.h"
#include "../CameraControlLibrary.h"
#include "../PTZCameraActor.h"
#include "Components/SceneCaptureComponent2D.h"   // SetZoom 후 Capture->FOVAngle 절대값 단정
#include "Engine/Engine.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** 테스트가 스폰한 카메라·매니저 정리(카메라는 매니저가 소유하지 않으므로 직접 파괴). */
	void DestroyOptics(ACameraControlManager* Mgr, const TArray<APTZCameraActor*>& Cams)
	{
		for (APTZCameraActor* Cam : Cams)
		{
			if (Cam)
			{
				Cam->Destroy();
			}
		}
		if (Mgr)
		{
			Mgr->Destroy();
		}
	}
}

// ===== A 적용 — 주입과 재적용 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraManagerOpticsTest,
	"Park3D.CameraControl.ManagerOptics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCameraManagerOpticsTest::RunTest(const FString& Parameters)
{
	UWorld* World = (GEngine && GEngine->GetWorldContexts().Num() > 0) ? GWorld : nullptr;
	if (!World)
	{
		AddWarning(TEXT("에디터 월드 없음 — 매니저 광학 규격 테스트 건너뜀."));
		return true;
	}

	// --- A-5 카메라 0대 상태에서도 안전한가(GetOrSpawn 이 매니저를 갓 만든 상황) ---
	{
		ACameraControlManager* Empty = World->SpawnActor<ACameraControlManager>();
		if (TestNotNull(TEXT("빈 매니저 스폰"), Empty))
		{
			TestEqual(TEXT("A-5 초기 카메라 0대"), Empty->GetCameraCount(), 0);
			Empty->SetCameraDefaultHFov(45.f);   // 루프가 비어 있을 뿐 크래시 없음
			TestEqual(TEXT("A-5 카메라 0대여도 필드는 갱신"), Empty->CameraDefaultHFov, 45.f, 1e-4f);

			APTZCameraActor* Late = Empty->AddCamera(TEXT("Optics Late"));
			if (TestNotNull(TEXT("이후 카메라 스폰"), Late))
			{
				TestEqual(TEXT("A-5 이후 AddCamera 에 반영"), Late->DefaultHFov, 45.f, 1e-4f);
			}
			DestroyOptics(Empty, {Late});
		}
	}

	ACameraControlManager* Mgr = World->SpawnActor<ACameraControlManager>();
	if (!TestNotNull(TEXT("매니저 스폰"), Mgr))
	{
		return false;
	}

	// --- A-7 매니저 기본값이 액터 CDO 와 같은가 ---
	// 여기가 어긋나면 56.5 상수가 '4번째'로 복제된 것이다(라이브러리 기본 인자 2 + 액터 CDO 1).
	const APTZCameraActor* CamCdo = GetDefault<APTZCameraActor>();
	if (TestNotNull(TEXT("APTZCameraActor CDO"), CamCdo))
	{
		TestEqual(TEXT("A-7 매니저 기본 화각 == 액터 CDO"), Mgr->CameraDefaultHFov, CamCdo->DefaultHFov, 1e-4f);
		TestEqual(TEXT("A-7 매니저 기본 배율 == 액터 CDO"), Mgr->CameraMaxZoom, CamCdo->MaxZoom, 1e-4f);
	}

	// --- 전제: 코드 기본값(56.5/36)으로 태어난 카메라 2대를 서로 다른 배율에 둔다 ---
	APTZCameraActor* CamA = Mgr->AddCamera(TEXT("Optics A"));
	APTZCameraActor* CamB = Mgr->AddCamera(TEXT("Optics B"));
	if (!TestNotNull(TEXT("카메라 A"), CamA) || !TestNotNull(TEXT("카메라 B"), CamB))
	{
		DestroyOptics(Mgr, {CamA, CamB});
		return false;
	}

	TestEqual(TEXT("전제: 기본 화각 56.5 주입"), CamA->DefaultHFov, 56.5f, 1e-4f);
	TestEqual(TEXT("전제: 기본 배율 36 주입"), CamA->MaxZoom, 36.f, 1e-4f);
	CamA->SetZoom(2.f);
	CamB->SetZoom(4.f);
	TestEqual(TEXT("전제: zoom2 → FOVAngle 30.076(56.5 기준)"), CamA->Capture->FOVAngle, 30.07595f, 1e-3f);

	// --- A-2 재적용 계약의 핵심: 화각만 바뀌고 '배율은 보존'된다 ---
	Mgr->SetCameraDefaultHFov(45.f);
	TestEqual(TEXT("A-2 매니저 화각 갱신"), Mgr->CameraDefaultHFov, 45.f, 1e-4f);
	TestEqual(TEXT("A-2 카메라 필드 갱신"), CamA->DefaultHFov, 45.f, 1e-4f);
	// 기대값은 ZoomToHFov(2, 36, 45) = 2·atan(tan(22.5°)/2) 의 실제 계산값이다.
	// (설계서 §5.3/§8.3 본문의 "23.409" 는 오기 — 같은 식으로 기존 봉인값 30.07595/1.71021 이 재현됨을 확인했다.)
	TestEqual(TEXT("A-2 FOVAngle 이 새 기준으로 재계산됨"), CamA->Capture->FOVAngle, 23.40184f, 1e-3f);
	TestEqual(TEXT("A-2 배율 2.0 보존"), CamA->GetZoom(), 2.f, 1e-3f);

	// --- A-3 순서 오류 회귀 방지 ---
	// 필드만 바꾸고 SetZoom 재적용을 빠뜨리면 FOVAngle 이 30.076 그대로 남는다(= 화면이 안 바뀐다).
	// GetZoom 을 필드 대입 '뒤에' 읽으면 배율이 1.541 로 오염돼 위 A-2 배율 단정이 깨진다.
	TestTrue(TEXT("A-3 옛 FOVAngle 이 그대로 남지 않음"),
		FMath::Abs(CamA->Capture->FOVAngle - 30.07595f) > 1e-2f);

	// --- A-6 카메라 2대가 모두 갱신되고 각자의 배율이 보존된다 ---
	TestEqual(TEXT("A-6 B 필드 갱신"), CamB->DefaultHFov, 45.f, 1e-4f);
	TestEqual(TEXT("A-6 B FOVAngle(zoom4, 45 기준)"), CamB->Capture->FOVAngle, 11.82420f, 1e-3f);
	TestEqual(TEXT("A-6 B 배율 4.0 보존"), CamB->GetZoom(), 4.f, 1e-3f);

	// --- A-1 이후 스폰되는 카메라에 주입된다(SyncCamerasToData 로 늘어나는 2..N 대 대비) ---
	APTZCameraActor* CamC = Mgr->AddCamera(TEXT("Optics C"));
	if (TestNotNull(TEXT("카메라 C"), CamC))
	{
		TestEqual(TEXT("A-1 신규 카메라에 화각 주입"), CamC->DefaultHFov, 45.f, 1e-4f);
		CamC->SetZoom(1.f);
		TestEqual(TEXT("A-1 zoom1 → FOVAngle 45.000"), CamC->Capture->FOVAngle, 45.f, 1e-3f);
	}

	// --- A-4 배율·화각 두 주입이 서로를 덮지 않는다 ---
	Mgr->SetCameraMaxZoom(30.f);
	Mgr->SetCameraDefaultHFov(45.f);
	APTZCameraActor* CamD = Mgr->AddCamera(TEXT("Optics D"));
	if (TestNotNull(TEXT("카메라 D"), CamD))
	{
		TestEqual(TEXT("A-4 신규 카메라 MaxZoom 30"), CamD->MaxZoom, 30.f, 1e-4f);
		TestEqual(TEXT("A-4 신규 카메라 DefaultHFov 45"), CamD->DefaultHFov, 45.f, 1e-4f);
	}

	// --- A-1b 스폰 직후 FOVAngle 이 실제로 갱신되는가 (SetZoom 을 부르지 않는다) ---
	// 위 A-1 은 FOVAngle 을 읽기 전에 스스로 SetZoom(1.f) 을 불러 이 구멍을 비껴갔다(사후 영향도 F-2).
	// APTZCameraActor 생성자가 CDO 기본값(56.5)으로 Capture->FOVAngle 을 굳히므로, AddCamera 가
	// 필드만 대입하면 DefaultHFov=70 인데 FOVAngle=56.5 인 스테일 상태가 된다.
	// 즉 AddCamera 에 재적용이 없으면(수정 전) 아래 두 단정은 56.5 / 1.303 으로 반드시 실패한다.
	// SetZoom 이 뒤따르지 않는 유일한 호출부가 RPC cam.create 이므로, 이 경로의 유일한 방어선이다.
	Mgr->SetCameraDefaultHFov(70.f);
	APTZCameraActor* CamE = Mgr->AddCamera(TEXT("Optics E"));
	if (TestNotNull(TEXT("카메라 E"), CamE))
	{
		TestEqual(TEXT("A-1b 스폰 직후 FOVAngle 70(SetZoom 호출 없이)"), CamE->Capture->FOVAngle, 70.f, 1e-3f);
		TestEqual(TEXT("A-1b 스폰 직후 배율 1.0"), CamE->GetZoom(), 1.f, 1e-3f);
	}

	DestroyOptics(Mgr, {CamA, CamB, CamC, CamD, CamE});
	return true;
}

// ===== V 검증 — (0,180) 밖 거부 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraManagerHFovValidationTest,
	"Park3D.CameraControl.HFovValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCameraManagerHFovValidationTest::RunTest(const FString& Parameters)
{
	UWorld* World = (GEngine && GEngine->GetWorldContexts().Num() > 0) ? GWorld : nullptr;
	if (!World)
	{
		AddWarning(TEXT("에디터 월드 없음 — 화각 검증 테스트 건너뜀."));
		return true;
	}

	// 거부 경로는 의도적으로 Warning 을 남긴다 — 조용히 무시하면 "설정했는데 안 먹는다"가 되므로
	// 경고가 나오지 않는 쪽이 오히려 결함이다. 발생 횟수 0 = "최소 1회 발생하되 횟수는 안 따짐".
	AddExpectedMessagePlain(TEXT("[CameraControl] hfov_wide"), ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains, 0);

	ACameraControlManager* Mgr = World->SpawnActor<ACameraControlManager>();
	if (!TestNotNull(TEXT("매니저 스폰"), Mgr))
	{
		return false;
	}

	APTZCameraActor* Cam = Mgr->AddCamera(TEXT("Validation"));
	if (!TestNotNull(TEXT("카메라 스폰"), Cam))
	{
		DestroyOptics(Mgr, {Cam});
		return false;
	}
	Cam->SetZoom(1.f);

	const float Base = Mgr->CameraDefaultHFov;   // 코드 기본값(56.5)

	// V-1/V-2 0·음수 — 필드도 화면도 불변.
	Mgr->SetCameraDefaultHFov(0.f);
	TestEqual(TEXT("V-1 0 은 거부(필드 불변)"), Mgr->CameraDefaultHFov, Base, 1e-4f);
	TestEqual(TEXT("V-1 FOVAngle 불변"), Cam->Capture->FOVAngle, Base, 1e-3f);
	Mgr->SetCameraDefaultHFov(-1.f);
	TestEqual(TEXT("V-2 음수는 거부"), Mgr->CameraDefaultHFov, Base, 1e-4f);

	// V-3 상한은 배타 — 180 도 거부한다.
	Mgr->SetCameraDefaultHFov(180.f);
	TestEqual(TEXT("V-3 180 거부(배타)"), Mgr->CameraDefaultHFov, Base, 1e-4f);
	Mgr->SetCameraDefaultHFov(200.f);
	TestEqual(TEXT("V-3 200 거부"), Mgr->CameraDefaultHFov, Base, 1e-4f);
	Mgr->SetCameraDefaultHFov(360.f);
	TestEqual(TEXT("V-3 360 거부"), Mgr->CameraDefaultHFov, Base, 1e-4f);
	TestEqual(TEXT("V-3 거부 뒤에도 FOVAngle 불변"), Cam->Capture->FOVAngle, Base, 1e-3f);

	// V-6 "기존값 유지"의 의미는 CDO 기본값이 아니라 '직전 유효값'이다.
	Mgr->SetCameraDefaultHFov(45.f);
	TestEqual(TEXT("V-6 정상값 수용"), Mgr->CameraDefaultHFov, 45.f, 1e-4f);
	Mgr->SetCameraDefaultHFov(200.f);
	TestEqual(TEXT("V-6 거부 후 직전 유효값 45 유지(56.5 로 되돌아가지 않음)"), Mgr->CameraDefaultHFov, 45.f, 1e-4f);
	TestEqual(TEXT("V-6 FOVAngle 도 45 유지"), Cam->Capture->FOVAngle, 45.f, 1e-3f);

	// V-4 경계 안쪽은 실제로 통과한다.
	Mgr->SetCameraDefaultHFov(179.9f);
	TestEqual(TEXT("V-4 179.9 수용"), Mgr->CameraDefaultHFov, 179.9f, 1e-4f);
	TestEqual(TEXT("V-4 zoom1 → FOVAngle 179.9"), Cam->Capture->FOVAngle, 179.9f, 1e-3f);

	// V-5 극단값이지만 유한하고 크래시가 없다(라이브러리 하한 클램프 0.001° 와 맞닿는 값).
	Mgr->SetCameraDefaultHFov(0.001f);
	TestEqual(TEXT("V-5 0.001 수용(범위 안)"), Mgr->CameraDefaultHFov, 0.001f, 1e-6f);
	TestTrue(TEXT("V-5 FOVAngle 이 유한 양수"),
		Cam->Capture->FOVAngle > 0.f && Cam->Capture->FOVAngle < 180.f);

	// V-7 계층 분리 — config 가 거부해도 라이브러리는 계속 방어한다(둘은 다른 일을 한다).
	const float Defended = UCameraControlLibrary::ZoomToHFov(1.f, 36.f, 200.f);
	TestTrue(TEXT("V-7 라이브러리는 200 도 유한 화각으로 방어"), Defended > 0.f && Defended < 180.f);

	DestroyOptics(Mgr, {Cam});
	return true;
}

// ===== X 비대칭 격리 — 매니저 경유만 config 를 따른다 =====
// 설계 §6.2 표의 실증. 여기서 56.5 를 리터럴로 적지 않는 이유는, 이 테스트가 검증하려는 것이
// "값이 56.5 인가"가 아니라 "매니저 경유 경로와 CDO/라이브러리 경로가 실제로 갈라지는가"이기 때문이다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraOpticsIsolationTest,
	"Park3D.CameraControl.OpticsIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCameraOpticsIsolationTest::RunTest(const FString& Parameters)
{
	UWorld* World = (GEngine && GEngine->GetWorldContexts().Num() > 0) ? GWorld : nullptr;
	if (!World)
	{
		AddWarning(TEXT("에디터 월드 없음 — 비대칭 격리 테스트 건너뜀."));
		return true;
	}

	const float CodeDefault = UCameraControlLibrary::ZoomToHFov(1.f);   // 라이브러리 기본 인자 = 코드 기본값

	ACameraControlManager* Mgr = World->SpawnActor<ACameraControlManager>();
	if (!TestNotNull(TEXT("매니저 스폰"), Mgr))
	{
		return false;
	}

	// config 가 코드 기본값과 다른 화각을 지정한 상태를 만든다.
	Mgr->SetCameraDefaultHFov(45.f);
	TestTrue(TEXT("전제: config 화각이 코드 기본값과 다르다"), !FMath::IsNearlyEqual(45.f, CodeDefault, 1e-3f));

	// X-2 매니저 경유 카메라만 새 기준을 쓴다.
	APTZCameraActor* Managed = Mgr->AddCamera(TEXT("Isolation Managed"));
	if (TestNotNull(TEXT("매니저 경유 카메라"), Managed))
	{
		Managed->SetZoom(1.f);
		TestEqual(TEXT("X-2 매니저 경유는 config 화각(45)"), Managed->Capture->FOVAngle, 45.f, 1e-3f);
	}

	// X-1 CDO 는 오염되지 않는다(GetMutableDefault 주입 금지 — 사전 영향도 GR-5).
	// 오염되면 Park3D.CameraControl.Fov 의 CDO 단정이 'PIE 를 돌린 세션에서만' 실패하는
	// 재현 불안정 결함이 된다.
	const APTZCameraActor* CamCdo = GetDefault<APTZCameraActor>();
	if (TestNotNull(TEXT("APTZCameraActor CDO"), CamCdo))
	{
		TestEqual(TEXT("X-1 CDO 는 코드 기본값 유지"), CamCdo->DefaultHFov, CodeDefault, 1e-4f);
	}

	// X-1 라이브러리 기본 인자도 불변(무인자 호출은 컴파일 상수를 따른다 — 설계 §6.1).
	TestEqual(TEXT("X-1 라이브러리 무인자 호출은 코드 기본값"),
		UCameraControlLibrary::ZoomToHFov(1.f), CodeDefault, 1e-4f);

	// X-2 매니저를 거치지 않고 직접 스폰한 액터는 CDO 기본값 그대로다
	// (기존 Park3D.CameraControl.Fov 의 절대값 단정이 config 와 무관하게 재현되는 근거).
	APTZCameraActor* Direct = World->SpawnActor<APTZCameraActor>();
	if (TestNotNull(TEXT("직접 스폰 카메라"), Direct))
	{
		TestEqual(TEXT("X-2 직접 스폰은 코드 기본값"), Direct->DefaultHFov, CodeDefault, 1e-4f);
		Direct->SetZoom(1.f);
		TestEqual(TEXT("X-2 직접 스폰 FOVAngle 도 코드 기본값"), Direct->Capture->FOVAngle, CodeDefault, 1e-3f);
		Direct->Destroy();
	}

	DestroyOptics(Mgr, {Managed});
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
