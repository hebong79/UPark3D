// Copyright Epic Games, Inc. All Rights Reserved.
// CameraViewerWidgetTest : 뷰어 표시 크기 영속화(저장/복원) JSON 라운드트립 유닛테스트.
//  TP-VIEWSIZE: SaveSizeConfigToPath → LoadSizeConfigFromPath 라운드트립,
//               파일 없음 시 false(기본값 폴백), 임시 파일 정리.
// PIE 불필요 — 위젯 인스턴스 없이 static 함수만 검증(에디터 컨텍스트).

#include "Misc/AutomationTest.h"
#include "../CameraViewerWidget.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraViewerSizeRoundTripTest,
	"Park3D.CameraViewer.SizeRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCameraViewerSizeRoundTripTest::RunTest(const FString& Parameters)
{
	const FString TempPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Test_ViewerSize_RoundTrip.json"));
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	PF.DeleteFile(*TempPath); // 사전 정리

	// 1) 파일 없음 → Load 실패(호출부가 기본값 사용).
	FVector2D Loaded(0.f, 0.f);
	TestFalse(TEXT("파일 없으면 Load 실패"), UCameraViewerWidget::LoadSizeConfigFromPath(TempPath, Loaded));

	// 2) 저장 → 로드 라운드트립.
	const FVector2D Saved(640.f, 360.f);
	TestTrue(TEXT("크기 저장 성공"), UCameraViewerWidget::SaveSizeConfigToPath(TempPath, Saved));
	TestTrue(TEXT("저장 파일 존재"), PF.FileExists(*TempPath));

	FVector2D Back(0.f, 0.f);
	TestTrue(TEXT("크기 로드 성공"), UCameraViewerWidget::LoadSizeConfigFromPath(TempPath, Back));
	TestEqual(TEXT("width 라운드트립"), (float)Back.X, (float)Saved.X, 1e-3f);
	TestEqual(TEXT("height 라운드트립"), (float)Back.Y, (float)Saved.Y, 1e-3f);

	// 3) 정리.
	PF.DeleteFile(*TempPath);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
