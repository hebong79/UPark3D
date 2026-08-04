// Copyright Epic Games, Inc. All Rights Reserved.
// CameraControlManagerTest : 부팅 시 기본 카메라 보장 경로 검증.
//  TP-BOOTCAM: GetOrSpawn 이 기존 매니저를 재사용(중복 스폰 없음),
//              EnsureDefaultCamera 가 카메라 0대 → 1대 + 선택 + 유효 RT 를 만들고,
//              재호출해도 대수가 늘지 않는다(멱등).
//  회귀 대상 — 컨트롤 패널을 열기 전에는 매니저/카메라/RT 가 없어 뷰어가 흰 사각형만 그리던 결함.
//  에디터 월드에 매니저를 스폰해 확인하고 즉시 정리(PIE 불필요).

#include "Misc/AutomationTest.h"
#include "../CameraControlManager.h"
#include "../PTZCameraActor.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraManagerEnsureDefaultCameraTest,
	"Park3D.CameraControl.EnsureDefaultCamera",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCameraManagerEnsureDefaultCameraTest::RunTest(const FString& Parameters)
{
	UWorld* World = (GEngine && GEngine->GetWorldContexts().Num() > 0) ? GWorld : nullptr;
	if (!World)
	{
		AddWarning(TEXT("에디터 월드 없음 — 매니저 테스트 건너뜀."));
		return true;
	}

	ACameraControlManager* Mgr = World->SpawnActor<ACameraControlManager>();
	if (!TestNotNull(TEXT("매니저 스폰"), Mgr))
	{
		return false;
	}

	// 1) 스폰 직후에는 카메라도 RT 도 없다(= 결함 당시의 부팅 상태).
	TestEqual(TEXT("초기 카메라 0대"), Mgr->GetCameraCount(), 0);
	TestNull(TEXT("초기 RT 없음"), Mgr->GetSelectedRenderTarget());

	// 2) 보장 호출 → 1대 생성 + 선택 + 유효 RT.
	Mgr->EnsureDefaultCamera();
	TestEqual(TEXT("보장 후 카메라 1대"), Mgr->GetCameraCount(), 1);
	TestEqual(TEXT("선택 인덱스 0"), Mgr->SelectedIndex, 0);
	UTextureRenderTarget2D* RT = Mgr->GetSelectedRenderTarget();
	TestNotNull(TEXT("보장 후 선택 RT 존재"), RT);
	if (RT)
	{
		TestTrue(TEXT("RT 크기 > 0"), RT->SizeX > 0 && RT->SizeY > 0);
	}

	// 3) 기본 자세 반영 — 높이 5m = Z 500uu(MetersToUU 기본 100).
	APTZCameraActor* Cam = Mgr->GetCamera(0);
	if (TestNotNull(TEXT("카메라 액터"), Cam))
	{
		TestEqual(TEXT("기본 높이 500uu"), (float)Cam->GetActorLocation().Z, 500.f, 1e-2f);
	}

	// 4) 멱등 — 재호출해도 대수가 늘지 않는다(GameMode BeginPlay 와 패널 오픈이 겹쳐도 안전).
	Mgr->EnsureDefaultCamera();
	TestEqual(TEXT("재호출 후에도 1대"), Mgr->GetCameraCount(), 1);

	// 5) GetOrSpawn 은 기존 매니저를 재사용한다(중복 스폰 금지).
	//    월드에 다른 매니저가 남아 있을 수 있으므로 동일 인스턴스가 아니라 "대수가 안 늘어남"으로 검증한다.
	auto CountManagers = [World]()
	{
		int32 N = 0;
		for (TActorIterator<ACameraControlManager> It(World); It; ++It) { ++N; }
		return N;
	};
	const int32 Before = CountManagers();
	TestNotNull(TEXT("GetOrSpawn 결과"), ACameraControlManager::GetOrSpawn(World));
	TestEqual(TEXT("GetOrSpawn 이 새로 스폰하지 않음"), CountManagers(), Before);

	// 정리 — 카메라 액터는 매니저가 소유하지 않으므로 직접 파괴한 뒤 매니저를 지운다.
	if (Cam)
	{
		Cam->Destroy();
	}
	Mgr->Destroy();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
