// Copyright Epic Games, Inc. All Rights Reserved.
// CameraViewerWidgetTest : 뷰어 표시 크기 영속화(저장/복원) JSON 라운드트립 유닛테스트.
//  TP-VIEWSIZE: SaveSizeConfigToPath → LoadSizeConfigFromPath 라운드트립,
//               파일 없음 시 false(기본값 폴백), 임시 파일 정리.
// PIE 불필요 — 위젯 인스턴스 없이 static 함수만 검증(에디터 컨텍스트).

#include "Misc/AutomationTest.h"
#include "../CameraViewerWidget.h"
#include "Components/Image.h"
#include "Engine/TextureRenderTarget2D.h"
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

// TP-VIEWRT: 상시 표시 뷰어의 렌더타겟 유무별 이미지 가시성.
//  RT 없음(카메라 0대) → Collapsed 로 접어 빈 사각형/외곽 프레임이 남지 않게 한다.
//  RT 있음            → Visible + 브러시 리소스가 해당 RT.
// 위젯 인스턴스는 NewObject 로 만들고 BindWidget 대상(Img_View)만 직접 주입한다(WBP·PIE 불필요).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraViewerRenderTargetVisibilityTest,
	"Park3D.CameraViewer.RenderTargetVisibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCameraViewerRenderTargetVisibilityTest::RunTest(const FString& Parameters)
{
	UCameraViewerWidget* Viewer = NewObject<UCameraViewerWidget>();
	if (!Viewer)
	{
		AddError(TEXT("뷰어 위젯 생성 실패"));
		return false;
	}
	Viewer->Img_View = NewObject<UImage>(Viewer);
	if (!Viewer->Img_View)
	{
		AddError(TEXT("Img_View 생성 실패"));
		return false;
	}
	Viewer->Img_View->SetVisibility(ESlateVisibility::Visible);

	// 1) RT 없음 → 접힘.
	Viewer->SetRenderTarget(nullptr);
	TestEqual(TEXT("RT 없으면 Img_View Collapsed"),
		(int32)Viewer->Img_View->GetVisibility(), (int32)ESlateVisibility::Collapsed);

	// 2) RT 있음 → 표시 + 브러시 리소스 일치.
	UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(Viewer);
	RT->InitAutoFormat(256, 144);
	Viewer->SetRenderTarget(RT);
	TestEqual(TEXT("RT 있으면 Img_View Visible"),
		(int32)Viewer->Img_View->GetVisibility(), (int32)ESlateVisibility::Visible);
	TestTrue(TEXT("브러시 리소스가 지정한 RT"),
		Viewer->Img_View->GetBrush().GetResourceObject() == RT);

	// 3) 다시 RT 없음 → 재차 접힘(카메라 전량 삭제 경로).
	Viewer->SetRenderTarget(nullptr);
	TestEqual(TEXT("RT 해제되면 다시 Collapsed"),
		(int32)Viewer->Img_View->GetVisibility(), (int32)ESlateVisibility::Collapsed);

	return true;
}

// TP-DEFSIZE: 최초 실행 기본 표시 크기를 화면 가로의 목표 비율로 보정.
//  슬롯 폭 → 화면 폭 변환 계수를 계산으로 알아내려는 시도는 두 번 다 빗나갔다
//  (뷰포트 픽셀/DPI 환산 → 26%, 루트 로컬 폭 × 비율 → 49%).
//  그래서 실측한 현재 화면 비율에서 목표 비율로 곱해 보정한다(렌더 폭은 슬롯 폭에 선형).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraViewerDefaultSizeTest,
	"Park3D.CameraViewer.DefaultSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCameraViewerDefaultSizeTest::RunTest(const FString& Parameters)
{
	// 1) 실측 재현 — 폭 480 일 때 화면비 0.4914 였다면, 0.4 가 되도록 축소한다.
	{
		const float W = UCameraViewerWidget::ComputeCorrectedViewWidth(480.f, 0.4914f, 0.4f, 160.f, 2000.f);
		TestEqual(TEXT("보정 폭"), W, 480.f * (0.4f / 0.4914f), 0.01f);
		// 선형이므로 보정 후 화면비는 정확히 목표가 된다(화면비/폭 = 일정).
		const float NewRatio = 0.4914f * (W / 480.f);
		TestEqual(TEXT("보정 후 화면비 = 0.4"), NewRatio, 0.4f, 1e-4f);
	}

	// 2) 반대 방향 — 너무 작으면(0.26) 키운다.
	{
		const float W = UCameraViewerWidget::ComputeCorrectedViewWidth(478.f, 0.26f, 0.4f, 160.f, 2000.f);
		TestTrue(TEXT("작을 때는 커진다"), W > 478.f);
		TestEqual(TEXT("보정 후 화면비 = 0.4"), 0.26f * (W / 478.f), 0.4f, 1e-4f);
	}

	// 3) 이미 목표면 그대로.
	TestEqual(TEXT("이미 목표면 불변"),
		UCameraViewerWidget::ComputeCorrectedViewWidth(600.f, 0.4f, 0.4f, 160.f, 2000.f), 600.f, 0.01f);

	// 4) 상/하한 클램프.
	TestEqual(TEXT("상한 클램프"),
		UCameraViewerWidget::ComputeCorrectedViewWidth(1000.f, 0.1f, 0.4f, 160.f, 1280.f), 1280.f, 0.01f);
	TestEqual(TEXT("하한 클램프"),
		UCameraViewerWidget::ComputeCorrectedViewWidth(200.f, 0.9f, 0.4f, 160.f, 1280.f), 160.f, 0.01f);

	// 5) 측정 불가(0 이하) → 현재 폭 유지(다음 틱 재시도).
	TestEqual(TEXT("화면비 0 → 유지"),
		UCameraViewerWidget::ComputeCorrectedViewWidth(480.f, 0.f, 0.4f, 160.f, 1280.f), 480.f, 0.01f);
	TestEqual(TEXT("현재폭 0 → 유지"),
		UCameraViewerWidget::ComputeCorrectedViewWidth(0.f, 0.4f, 0.4f, 160.f, 1280.f), 0.f, 0.01f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
