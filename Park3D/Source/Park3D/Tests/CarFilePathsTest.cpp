// Copyright Epic Games, Inc. All Rights Reserved.
// CarFilePathsTest : car.deleteFile 의 자리 검문(CarFilePaths) 유닛테스트.
//
// 이 검문이 삭제 RPC 의 전부다 — 통과하면 파일이 실제로 사라지므로, 「어디까지 허용하는가」를
// 실제 프로젝트 폴더와 무관하게(가짜 루트 주입) 못 박아 둔다. PIE 불필요.

#include "Misc/AutomationTest.h"
#include "../Rpc/Modules/CarFilePaths.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

#if WITH_DEV_AUTOMATION_TESTS

// ===== 자리 검문: 허용 폴더 안의 .json 만 통과한다 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarFilePathsGuardTest,
	"Park3D.CarFile.DeleteGuard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCarFilePathsGuardTest::RunTest(const FString& Parameters)
{
	// 가짜 루트 두 개 — 실제 Saved/CarData·Save/3D/CarPos 와 같은 모양이면 충분하다.
	const FString RootA = CarFilePaths::Normalize(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CarData")));
	const FString RootB = CarFilePaths::Normalize(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Other"), TEXT("CarPos")));
	const TArray<FString> Roots = { RootA, RootB };

	FString Path, Reason;

	// 1) 허용 폴더 안의 .json — 통과.
	TestTrue(TEXT("CarData 안의 배치 파일은 지울 수 있다"),
		CarFilePaths::CanDelete(RootA / TEXT("touragent-scene-20260826-213748.json"), Roots, Path, Reason));
	TestEqual(TEXT("통과 경로는 정규화된 절대 경로"), Path, RootA / TEXT("touragent-scene-20260826-213748.json"));

	// 두 번째 루트도 같은 자격이다.
	TestTrue(TEXT("CarPos 안의 배치 파일도 지울 수 있다"),
		CarFilePaths::CanDelete(RootB / TEXT("CarPos.json"), Roots, Path, Reason));

	// 2) ★ `..` 로 폴더를 빠져나가는 경로 — 거절. 접기를 안 하면 StartsWith 로는 통과해 버린다.
	TestFalse(TEXT("`..` 로 상위 폴더를 겨냥하면 거절"),
		CarFilePaths::CanDelete(RootA / TEXT("../../Config/DefaultGame.json"), Roots, Path, Reason));
	TestTrue(TEXT("거절 사유가 비어 있지 않다"), !Reason.IsEmpty());
	TestFalse(TEXT("접힌 경로가 허용 폴더 안이 아니다"), Path.StartsWith(RootA + TEXT("/")));

	// 3) 허용 폴더 **밖**의 .json — 거절.
	TestFalse(TEXT("다른 폴더의 .json 은 거절"),
		CarFilePaths::CanDelete(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Config"), TEXT("config_pmaker.json")), Roots, Path, Reason));

	// 4) 확장자가 .json 이 아니면 거절 — 안에 있어도 안 된다(exe·ini·로그 보호).
	TestFalse(TEXT(".json 이 아니면 거절"),
		CarFilePaths::CanDelete(RootA / TEXT("Park3D.exe"), Roots, Path, Reason));

	// 5) 폴더 이름이 접두사로 겹치는 이웃 폴더 — 거절(CarData vs CarDataBackup).
	TestFalse(TEXT("이름이 겹치는 이웃 폴더는 다른 폴더다"),
		CarFilePaths::CanDelete(RootA + TEXT("Backup/x.json"), Roots, Path, Reason));

	// 6) 폴더 자기 자신은 파일이 아니다.
	TestFalse(TEXT("폴더 경로 자체는 거절"), CarFilePaths::CanDelete(RootA, Roots, Path, Reason));

	// 7) 윈도우 역슬래시로 와도 같은 판정이어야 한다(부르는 쪽이 그렇게 보낸다).
	FString Backslash = RootA / TEXT("touragent-scene-20260826-213748.json");
	Backslash.ReplaceInline(TEXT("/"), TEXT("\\"));
	TestTrue(TEXT("역슬래시 경로도 같은 파일로 본다"),
		CarFilePaths::CanDelete(Backslash, Roots, Path, Reason));

	// 8) 대소문자가 달라도 같은 폴더로 본다(윈도우 파일시스템 규약).
	TestTrue(TEXT("대소문자 차이는 같은 폴더"),
		CarFilePaths::CanDelete(RootA.ToUpper() / TEXT("Some-Scene.JSON"), Roots, Path, Reason));

	return true;
}

// ===== 실제 파일: 만들고 → 검문 통과 → 지우고 → 다시 부르면 「없었다」 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarFilePathsDeleteRoundTripTest,
	"Park3D.CarFile.DeleteRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCarFilePathsDeleteRoundTripTest::RunTest(const FString& Parameters)
{
	const FString Dir = CarFilePaths::Normalize(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CarData")));
	const FString File = Dir / TEXT("Test_car_deleteFile.json");
	const TArray<FString> Roots = { Dir };

	TestTrue(TEXT("시험용 배치 파일 생성"), FFileHelper::SaveStringToFile(TEXT(R"({"datas":[]})"), *File));

	FString Path, Reason;
	TestTrue(TEXT("검문 통과"), CarFilePaths::CanDelete(File, Roots, Path, Reason));

	// car.deleteFile 핸들러가 하는 것과 같은 두 걸음.
	TestTrue(TEXT("삭제 전에는 있다"), IFileManager::Get().FileExists(*Path));
	TestTrue(TEXT("삭제 성공"), IFileManager::Get().Delete(*Path, false, true, true));
	TestFalse(TEXT("삭제 후에는 없다"), IFileManager::Get().FileExists(*Path));

	// 멱등: 두 번째 호출은 「없었다」이지 실패가 아니다 — 핸들러는 existed=false 로 ok 를 돌려준다.
	TestTrue(TEXT("두 번째도 검문은 통과한다"), CarFilePaths::CanDelete(File, Roots, Path, Reason));
	TestFalse(TEXT("이미 없다"), IFileManager::Get().FileExists(*Path));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
