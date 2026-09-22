// Copyright Epic Games, Inc. All Rights Reserved.
// LevelSlotLibraryTest : 레벨 주차면(ISM_Slot) 스냅샷 ↔ 적용 ↔ JSON 왕복.
// 레벨 BP_ParkingSlot 클래스는 C++ 로 못 만드므로 ISM 을 단 평범한 액터를 스폰해 액터 목록판(Snapshot/Apply 오버로드)으로 검증한다.

#include "Misc/AutomationTest.h"
#include "../Env/LevelSlotLibrary.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** ISM_Slot(엔진 Plane) 하나를 단 액터. 이름은 스폰 이름 그대로(레벨 이름 규칙과 무관). */
	AActor* LsTestSpawnSlotActor(UWorld* World, const TCHAR* Name, const TArray<FTransform>& Instances)
	{
		FActorSpawnParameters SP;
		SP.Name = FName(Name);
		SP.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
		AActor* A = World->SpawnActor<AActor>(SP);
		if (!A) return nullptr;
		UInstancedStaticMeshComponent* Comp = NewObject<UInstancedStaticMeshComponent>(A, TEXT("ISM_Slot"));
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")));
		A->SetRootComponent(Comp);
		Comp->RegisterComponent();
		for (const FTransform& T : Instances) { Comp->AddInstance(T, /*bWorldSpace=*/true); }
		return A;
	}

	FTransform LsTestT(double X, double Y, double Yaw, double SX, double SY)
	{
		return FTransform(FRotator(0.f, Yaw, 0.f), FVector(X, Y, 1.1), FVector(SX, SY, 1.0));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLevelSlotLibraryRoundTripTest,
	"Park3D.Env.LevelSlots.SnapshotApplyRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLevelSlotLibraryRoundTripTest::RunTest(const FString& Parameters)
{
	UWorld* World = (GEngine && GEngine->GetWorldContexts().Num() > 0) ? GWorld : nullptr;
	if (!World)
	{
		AddWarning(TEXT("에디터 월드 없음 — 레벨 주차면 테스트 건너뜀."));
		return true;
	}

	AActor* A1 = LsTestSpawnSlotActor(World, TEXT("LsTest_Slot_1"), { LsTestT(0, 0, 72.09, 6.14, 2.64), LsTestT(184.47, 570.94, 72.09, 6.14, 2.64) });
	AActor* A2 = LsTestSpawnSlotActor(World, TEXT("LsTest_Slot_2"), { LsTestT(1000, 0, 0, 2.64, 5.14) });
	if (!A1 || !A2) { AddError(TEXT("테스트 액터 스폰 실패")); return false; }
	TArray<AActor*> Actors = { A1, A2 };

	TestNotNull(TEXT("ISM_Slot 을 찾는다"), Park3DLevelSlots::FindSlotComponent(A1));

	// 1) 스냅샷: 액터순 → 인스턴스순 3개. Override 키는 그 변환을 대신 쓴다(숨긴 면).
	TArray<Park3DLevelSlots::FSlotInstance> Snap;
	TMap<FString, FTransform> Override;
	Override.Add(TEXT("level:LsTest_Slot_2#0"), LsTestT(2000, 0, 0, 2.64, 5.14));
	Park3DLevelSlots::Snapshot(Actors, Override, Snap);
	TestEqual(TEXT("스냅샷 3개"), Snap.Num(), 3);
	if (Snap.Num() == 3)
	{
		TestEqual(TEXT("[0] 액터"), Snap[0].ActorName, FString(TEXT("LsTest_Slot_1")));
		TestTrue(TEXT("[1] 위치"), Snap[1].Transform.GetLocation().Equals(FVector(184.47, 570.94, 1.1), 0.01));
		TestTrue(TEXT("[2] Override 변환"), FMath::IsNearlyEqual(Snap[2].Transform.GetLocation().X, 2000.0, 0.01));
	}

	// 2) JSON 왕복: m 단위 pos·yaw·scale 보존.
	TArray<Park3DLevelSlots::FSlotInstance> Back;
	FString Level;
	TestTrue(TEXT("FromJson"), Park3DLevelSlots::FromJson(Park3DLevelSlots::ToJson(TEXT("/Game/Levels/LV_Test"), Snap), Back, Level));
	TestEqual(TEXT("level 기록"), Level, FString(TEXT("/Game/Levels/LV_Test")));
	TestEqual(TEXT("왕복 3개"), Back.Num(), 3);
	if (Back.Num() == 3)
	{
		TestTrue(TEXT("pos 보존"), Back[1].Transform.GetLocation().Equals(FVector(184.47, 570.94, 1.1), 0.01));
		TestTrue(TEXT("yaw 보존"), FMath::IsNearlyEqual(Back[1].Transform.Rotator().Yaw, 72.09, 0.01));
		TestTrue(TEXT("scale 보존"), Back[1].Transform.GetScale3D().Equals(FVector(6.14, 2.64, 1.0), 0.001));
	}
	{
		TSharedPtr<FJsonObject> Wrong = MakeShared<FJsonObject>();
		Wrong->SetStringField(TEXT("kind"), TEXT("envProps"));
		TArray<Park3DLevelSlots::FSlotInstance> None; FString L;
		TestFalse(TEXT("다른 kind 거부"), Park3DLevelSlots::FromJson(Wrong, None, L));
	}

	// 3) 적용: 액터 1 을 인스턴스 1개(새 크기)로 교체, 없는 액터는 missing, 액터 2 는 그대로.
	TArray<Park3DLevelSlots::FSlotInstance> New;
	New.Add({ TEXT("LsTest_Slot_1"), LsTestT(-300, 0, 72.09, 6.0, 2.4) });
	New.Add({ TEXT("LsTest_Missing"), LsTestT(0, 0, 0, 1, 1) });
	TArray<FString> Missing;
	TestEqual(TEXT("적용 1개"), Park3DLevelSlots::Apply(Actors, New, Missing), 1);
	TestEqual(TEXT("missing 1"), Missing.Num(), 1);
	UInstancedStaticMeshComponent* C1 = Park3DLevelSlots::FindSlotComponent(A1);
	TestEqual(TEXT("액터 1 인스턴스 1개"), C1 ? C1->GetInstanceCount() : -1, 1);
	FTransform T;
	if (C1 && C1->GetInstanceTransform(0, T, true))
	{
		TestTrue(TEXT("새 위치"), FMath::IsNearlyEqual(T.GetLocation().X, -300.0, 0.01));
		TestTrue(TEXT("새 스케일"), T.GetScale3D().Equals(FVector(6.0, 2.4, 1.0), 0.001));
	}
	UInstancedStaticMeshComponent* C2 = Park3DLevelSlots::FindSlotComponent(A2);
	TestEqual(TEXT("액터 2 는 그대로"), C2 ? C2->GetInstanceCount() : -1, 1);

	// 4) 파일 왕복.
	const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("_AutomationTest_LevelSlots.json"));
	IFileManager::Get().Delete(*Path, false);
	TestTrue(TEXT("SaveFile"), Park3DLevelSlots::SaveFile(Path, TEXT("/Game/Levels/LV_Test"), New));
	TArray<Park3DLevelSlots::FSlotInstance> Loaded; FString L2;
	TestTrue(TEXT("LoadFile"), Park3DLevelSlots::LoadFile(Path, Loaded, L2));
	TestEqual(TEXT("파일 2개"), Loaded.Num(), 2);
	IFileManager::Get().Delete(*Path, false);

	A1->Destroy();
	A2->Destroy();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
