// Copyright Epic Games, Inc. All Rights Reserved.
// PTZCameraCaptureStateTest : 캡처 컴포넌트의 렌더링 스테이트 유지 규약 검증.
//  TP-PERSIST: 비선택 카메라(bCaptureEveryFrame=false)도 bAlwaysPersistRenderingState 가 true 여서
//              엔진이 FSceneViewState 를 파괴하지 않는다.
//  회귀 대상 — /stream(비선택 카메라 캡처)만 Lumen GI/리플렉션 기여를 잃어 그늘이 절반 밝기 +
//              갈색으로 나오던 결함(Docs/Bug/20260806_163723_Park3D_스트림영상_간접광누락_진단.md).
//              bAlwaysPersistRenderingState 가 false 로 되돌아가면 그 증상이 그대로 재발한다.
//  에디터 월드에 카메라를 스폰해 확인하고 즉시 정리(PIE 불필요).

#include "Misc/AutomationTest.h"
#include "../PTZCameraActor.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPTZCameraCapturePersistStateTest,
	"Park3D.CameraControl.CapturePersistRenderState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPTZCameraCapturePersistStateTest::RunTest(const FString& Parameters)
{
	UWorld* World = (GEngine && GEngine->GetWorldContexts().Num() > 0) ? GWorld : nullptr;
	if (!World)
	{
		AddWarning(TEXT("에디터 월드 없음 — 캡처 스테이트 테스트 건너뜀."));
		return true;
	}

	APTZCameraActor* Cam = World->SpawnActor<APTZCameraActor>();
	if (!TestNotNull(TEXT("카메라 스폰"), Cam) || !TestNotNull(TEXT("캡처 컴포넌트"), Cam->Capture))
	{
		if (Cam) { Cam->Destroy(); }
		return false;
	}

	// 1) 생성 직후: 매 프레임 캡처는 꺼져 있고(성능), 렌더링 스테이트는 유지된다(간접광).
	TestFalse(TEXT("생성 직후 bCaptureEveryFrame=false"), Cam->Capture->bCaptureEveryFrame);
	TestTrue(TEXT("생성 직후 bAlwaysPersistRenderingState=true"), Cam->Capture->bAlwaysPersistRenderingState);

	// 2) 선택 → 매 프레임 캡처 on. 유지 플래그는 그대로.
	Cam->SetCaptureEnabled(true);
	TestTrue(TEXT("선택 시 bCaptureEveryFrame=true"), Cam->Capture->bCaptureEveryFrame);
	TestTrue(TEXT("선택 시에도 bAlwaysPersistRenderingState=true"), Cam->Capture->bAlwaysPersistRenderingState);

	// 3) 선택 해제 → 매 프레임 캡처 off. 유지 플래그가 여기서 꺼지면 /stream 이 다시 어두워진다.
	Cam->SetCaptureEnabled(false);
	TestFalse(TEXT("비선택 시 bCaptureEveryFrame=false"), Cam->Capture->bCaptureEveryFrame);
	TestTrue(TEXT("비선택 시에도 bAlwaysPersistRenderingState=true"), Cam->Capture->bAlwaysPersistRenderingState);

	// 4) 캡처 소스는 톤매핑된 LDR — 스트림/RPC 가 공유하는 픽셀 규약.
	TestEqual(TEXT("CaptureSource=SCS_FinalColorLDR"),
		static_cast<int32>(Cam->Capture->CaptureSource),
		static_cast<int32>(ESceneCaptureSource::SCS_FinalColorLDR));

	Cam->Destroy();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
