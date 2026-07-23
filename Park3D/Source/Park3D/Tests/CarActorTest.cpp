// Copyright Epic Games, Inc. All Rights Reserved.
// CarActorTest : ACarActor 트랜스폼 라운드트립 + UCarColorComponent::ColorForEnum 검증.
//  트랜스폼 테스트는 에디터 월드에 임시 액터를 스폰 후 즉시 파괴한다(PIE 불필요).

#include "Misc/AutomationTest.h"
#include "../CarActor.h"
#include "../CarColorComponent.h"
#include "../CarPlacementLibrary.h"
#include "../UnityUnrealCoordinateConverter.h"
#include "../ParkingCarTypes.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

#if WITH_DEV_AUTOMATION_TESTS

// ===== ColorForEnum 매핑(순수) =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarColorEnumTest,
	"Park3D.CarPlacement.ColorEnum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCarColorEnumTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("White"), UCarColorComponent::ColorForEnum(ECarColor::White).Equals(FLinearColor(1.f, 1.f, 1.f)));
	TestEqual(TEXT("Black R"), UCarColorComponent::ColorForEnum(ECarColor::Black).R, 0.02f, 1e-4f);
	TestEqual(TEXT("Red R"), UCarColorComponent::ColorForEnum(ECarColor::Red).R, 0.60f, 1e-4f);
	TestEqual(TEXT("Blue B"), UCarColorComponent::ColorForEnum(ECarColor::Blue).B, 0.50f, 1e-4f);
	// 미정의 값은 White 폴백.
	TestTrue(TEXT("Default White"), UCarColorComponent::ColorForEnum((ECarColor)200).Equals(FLinearColor::White));
	return true;
}

// ===== 트랜스폼 라운드트립(스폰 → InitFromPos → ToCarPos) =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarActorTransformRoundTripTest,
	"Park3D.CarPlacement.ActorTransformRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCarActorTransformRoundTripTest::RunTest(const FString& Parameters)
{
	UWorld* World = (GEngine && GEngine->GetWorldContexts().Num() > 0) ? GWorld : nullptr;
	if (!World)
	{
		AddWarning(TEXT("에디터 월드 없음 — 트랜스폼 라운드트립 건너뜀."));
		return true;
	}

	// 다양한 입력(전/후면, 여러 rotY)으로 라운드트립 검증.
	struct FCase { FCarVec3 Pos; float RotY; bool bFront; };
	const FCase Cases[] = {
		{ {12.6805592f, -0.0504936576f, 21.1645584f}, 180.0f, true },
		{ {32.34019f, -0.0389035344f, 15.7780008f}, 271.13f, true },
		{ {14.53335f, -0.03937483f, 4.79093742f}, 1.566f, false },
		{ {-5.5f, 0.001f, -10.125f}, 90.674f, true },
	};

	for (const FCase& C : Cases)
	{
		ACarActor* Car = World->SpawnActor<ACarActor>();
		if (!TestNotNull(TEXT("스폰 성공"), Car))
		{
			continue;
		}

		FCarPos In;
		In.id = TEXT("9-12.34.56");
		In.type = 2; In.presetId = 3; In.slotId = 4; In.prefabId = 1;
		In.pos = C.Pos; In.rotY = C.RotY; In.isFront = C.bFront;

		Car->InitFromPos(In, nullptr, 100.f);   // 메시 없이 트랜스폼만 검증.
		// Loop 5 시각 보정(+270): front 메시 방향은 Loop 4의 반대 논리 forward이며,
		// rear는 그 정확한 180도 반대가 된다.
		const FVector MeshForward = Car->GetActorTransform().TransformVectorNoScale(-FVector::RightVector);
		const FVector ExpectedForward = UUnityUnrealCoordinateConverter::UnityYawToUnrealForward(In.rotY)
			* (In.isFront ? -1.f : 1.f);
		TestTrue(TEXT("메시 시각 정면 보정"), MeshForward.Equals(ExpectedForward, 1e-3f));
		const FCarPos Out = Car->ToCarPos(100.f);

		// 위치 라운드트립.
		TestEqual(TEXT("pos.x"), Out.pos.x, In.pos.x, 1e-2f);
		TestEqual(TEXT("pos.y"), Out.pos.y, In.pos.y, 1e-2f);
		TestEqual(TEXT("pos.z"), Out.pos.z, In.pos.z, 1e-2f);

		// 회전 라운드트립([0,360) 정규화 기준 비교).
		const float ExpectRot = UCarPlacementLibrary::AddYawDeg(In.rotY, 0.f);
		TestEqual(TEXT("rotY"), Out.rotY, ExpectRot, 1e-1f);

		// 메타 보존.
		TestEqual(TEXT("id 보존"), Out.id, In.id);
		TestEqual(TEXT("type 보존"), Out.type, In.type);
		TestEqual(TEXT("isFront 보존"), Out.isFront, In.isFront);

		Car->Destroy();
	}

	// isFront은 동일 rotY에서 반드시 정확히 180도 반대여야 한다.
	FCarPos Front;
	Front.id = TEXT("front-back-pair");
	Front.pos = {1.f, 2.f, 3.f};
	Front.rotY = 37.f;
	Front.isFront = true;
	FCarPos Back = Front;
	Back.isFront = false;

	ACarActor* FrontCar = World->SpawnActor<ACarActor>();
	ACarActor* BackCar = World->SpawnActor<ACarActor>();
	if (TestNotNull(TEXT("전면 비교차 스폰"), FrontCar) && TestNotNull(TEXT("후면 비교차 스폰"), BackCar))
	{
		FrontCar->InitFromPos(Front, nullptr, 100.f);
		BackCar->InitFromPos(Back, nullptr, 100.f);
		const float YawDelta = UCarPlacementLibrary::AddYawDeg(
			BackCar->GetActorRotation().Yaw, -FrontCar->GetActorRotation().Yaw);
		TestEqual(TEXT("isFront false yaw는 정확히 180도 반대"), YawDelta, 180.f, 1e-3f);

		const FVector FrontVisual = FrontCar->GetActorTransform().TransformVectorNoScale(-FVector::RightVector);
		const FVector BackVisual = BackCar->GetActorTransform().TransformVectorNoScale(-FVector::RightVector);
		TestTrue(TEXT("isFront false 메시 방향은 정확히 반대"), BackVisual.Equals(-FrontVisual, 1e-3f));
	}
	if (FrontCar) FrontCar->Destroy();
	if (BackCar) BackCar->Destroy();

	return true;
}

// ===== 번호판: 결정성/앞뒤 동일/actor 수명 중 1회 초기화 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarActorPlateNumberTest,
	"Park3D.CarPlacement.PlateNumber",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCarActorPlateNumberTest::RunTest(const FString& Parameters)
{
	FCarPos Source;
	Source.id = TEXT("7-12.34.56");
	Source.type = 2;
	Source.presetId = 3;
	Source.slotId = 4;
	Source.prefabId = 5;
	Source.pos = {21.16f, 12.68f, 5.f};
	Source.rotY = 37.f;
	Source.isFront = true;

	const FString Expected = ACarActor::MakeDeterministicPlateNumber(Source);
	const FString ExpectedDisplay = Expected.Left(3) + TEXT(" ") + Expected.Mid(3, 1) + TEXT(" ") + Expected.Right(4);
	TestFalse(TEXT("결정적 번호는 비어있지 않음"), Expected.IsEmpty());
	TestEqual(TEXT("한국 일반 번호판 형식 길이 123다4567"), Expected.Len(), 8);
	const FString AllowedPassengerChars = TEXT("가나다라마바사거너더러머버서어저고노도로모보소오조구누두루무부수우주");
	if (Expected.Len() == 8)
	{
		TestTrue(TEXT("앞 세 자리는 숫자"), FChar::IsDigit(Expected[0]) && FChar::IsDigit(Expected[1]) && FChar::IsDigit(Expected[2]));
		const int32 Prefix = FCString::Atoi(*Expected.Left(3));
		TestTrue(TEXT("일반 승용 prefix 100~699"), Prefix >= 100 && Prefix <= 699);
		const FString UsageChar = Expected.Mid(3, 1);
		TestTrue(TEXT("일반용 한글만 사용"), AllowedPassengerChars.Contains(UsageChar));
		TestFalse(TEXT("rental 하 제외"), UsageChar == TEXT("하"));
		TestFalse(TEXT("rental 허 제외"), UsageChar == TEXT("허"));
		TestFalse(TEXT("rental 호 제외"), UsageChar == TEXT("호"));
		TestTrue(TEXT("뒤 네 자리는 숫자"), FChar::IsDigit(Expected[4]) && FChar::IsDigit(Expected[5])
			&& FChar::IsDigit(Expected[6]) && FChar::IsDigit(Expected[7]));
	}

	UWorld* World = (GEngine && GEngine->GetWorldContexts().Num() > 0) ? GWorld : nullptr;
	if (!World)
	{
		AddWarning(TEXT("에디터 월드 없음 — 번호판 actor 수명 검증 건너뜀."));
		return true;
	}

	ACarActor* Car = World->SpawnActor<ACarActor>();
	if (!TestNotNull(TEXT("번호판 차량 스폰"), Car))
	{
		return false;
	}
	TestNotNull(TEXT("앞 번호판 컴포넌트"), Car->FrontPlateComp);
	TestNotNull(TEXT("뒤 번호판 컴포넌트"), Car->BackPlateComp);
	TestNotNull(TEXT("앞 번호판 frame 컴포넌트"), Car->FrontPlateFrameComp);
	TestNotNull(TEXT("뒤 번호판 frame 컴포넌트"), Car->BackPlateFrameComp);
	TestNotNull(TEXT("앞 KOR strip 컴포넌트"), Car->FrontPlateSecurityStripComp);
	TestNotNull(TEXT("뒤 KOR strip 컴포넌트"), Car->BackPlateSecurityStripComp);
	TestNotNull(TEXT("앞 번호 텍스트 컴포넌트"), Car->FrontPlateText);
	TestNotNull(TEXT("뒤 번호 텍스트 컴포넌트"), Car->BackPlateText);
	if (Car->FrontPlateComp) TestTrue(TEXT("앞 번호판 기존 Content mesh"), Car->FrontPlateComp->GetStaticMesh() != nullptr);
	if (Car->BackPlateComp) TestTrue(TEXT("뒤 번호판 기존 Content mesh"), Car->BackPlateComp->GetStaticMesh() != nullptr);

	Car->InitFromPos(Source, nullptr, 100.f);
	TestEqual(TEXT("최초 Init 결정적 번호"), Car->GetPlateNumber(), Expected);
	if (Car->FrontPlateText && Car->BackPlateText)
	{
		TestEqual(TEXT("앞 텍스트=간격 표시 번호"), Car->FrontPlateText->Text.ToString(), ExpectedDisplay);
		TestEqual(TEXT("뒤 텍스트=간격 표시 번호"), Car->BackPlateText->Text.ToString(), ExpectedDisplay);
	}

	// actor 수명 중에는 재 Init/재표시에서 번호를 다시 뽑지 않는다.
	FCarPos Reinit = Source;
	Reinit.id = TEXT("different-id-should-not-replace-existing-plate");
	Car->InitFromPos(Reinit, nullptr, 100.f);
	TestEqual(TEXT("재 Init 번호 불변"), Car->GetPlateNumber(), Expected);

	// 실제 차량 메시에서 번호판이 차량 외측에 장착되고 렌더 가능한 상태인지 확인한다.
	// (일반차_번호판 메시의 표면 법선은 local +Y: 전면은 -Y를 보도록 yaw 180, 후면은 yaw 0.)
	UStaticMesh* VehicleMesh = LoadObject<UStaticMesh>(nullptr,
		TEXT("/Game/Cars/Car_no_plate/현대_쏘나타.현대_쏘나타"));
	TestTrue(TEXT("실차 메시 로드"), VehicleMesh != nullptr);
	if (VehicleMesh)
	{
		Car->InitFromPos(Source, VehicleMesh, 100.f);
		TestEqual(TEXT("실차 재표시에도 번호 불변"), Car->GetPlateNumber(), Expected);

		FVector MeshMin = FVector::ZeroVector;
		FVector MeshMax = FVector::ZeroVector;
		Car->MeshComp->GetLocalBounds(MeshMin, MeshMax);
		const FVector MeshOrigin = (MeshMin + MeshMax) * 0.5f;
		const FVector MeshExtent = (MeshMax - MeshMin) * 0.5f;
		const float ExpectedMountY = MeshExtent.Y + 1.f;
		const float ExpectedMountZ = MeshOrigin.Z - MeshExtent.Z * 0.4f;

		if (Car->FrontPlateComp && Car->BackPlateComp)
		{
			TestTrue(TEXT("앞 번호판 registered"), Car->FrontPlateComp->IsRegistered());
			TestTrue(TEXT("뒤 번호판 registered"), Car->BackPlateComp->IsRegistered());
			TestTrue(TEXT("앞 번호판 visible"), Car->FrontPlateComp->IsVisible());
			TestTrue(TEXT("뒤 번호판 visible"), Car->BackPlateComp->IsVisible());
			TestEqual(TEXT("앞 번호판 외측 Y"), Car->FrontPlateComp->GetRelativeLocation().Y, MeshOrigin.Y - ExpectedMountY, 1e-3);
			TestEqual(TEXT("뒤 번호판 외측 Y"), Car->BackPlateComp->GetRelativeLocation().Y, MeshOrigin.Y + ExpectedMountY, 1e-3);
			TestEqual(TEXT("번호판 장착 Z"), Car->FrontPlateComp->GetRelativeLocation().Z, static_cast<double>(ExpectedMountZ), 1e-3);
			TestEqual(TEXT("앞 번호판 외측 방향"), FRotator::NormalizeAxis(Car->FrontPlateComp->GetRelativeRotation().Yaw), 180.0, 1e-3);
			TestEqual(TEXT("뒤 번호판 외측 방향"), FRotator::NormalizeAxis(Car->BackPlateComp->GetRelativeRotation().Yaw), 0.0, 1e-3);

			for (UStaticMeshComponent* PlateComp : { Car->FrontPlateComp, Car->BackPlateComp })
			{
				TestTrue(TEXT("번호판 Content bezel material M_Plate"), PlateComp->GetMaterial(0) != nullptr
					&& PlateComp->GetMaterial(0)->GetPathName().Contains(TEXT("/Game/Cars/번호판/M_Plate")));
				TestTrue(TEXT("번호판 Content KOR background M_Num"), PlateComp->GetMaterial(1) != nullptr
					&& PlateComp->GetMaterial(1)->GetPathName().Contains(TEXT("/Game/Cars/번호판/M_Num")));
			}
		}
		if (Car->FrontPlateFrameComp && Car->BackPlateFrameComp && Car->FrontPlateSecurityStripComp && Car->BackPlateSecurityStripComp)
		{
			const FLinearColor KoreaSecurityBlue(0.02f, 0.18f, 0.78f, 1.f);
			for (UStaticMeshComponent* PlateFrame : { Car->FrontPlateFrameComp, Car->BackPlateFrameComp })
			{
				TestTrue(TEXT("frame cube mesh"), PlateFrame->GetStaticMesh() != nullptr
					&& PlateFrame->GetStaticMesh()->GetPathName().Contains(TEXT("/Engine/BasicShapes/Cube")));
				TestTrue(TEXT("frame registered but Content bezel을 위해 hidden"), PlateFrame->IsRegistered() && !PlateFrame->IsVisible());
				UMaterialInstanceDynamic* FrameMID = Cast<UMaterialInstanceDynamic>(PlateFrame->GetMaterial(0));
				TestTrue(TEXT("frame black MID"), FrameMID != nullptr
					&& FrameMID->K2_GetVectorParameterValue(TEXT("Color")).Equals(FLinearColor::Black, 1e-4f));
				TestEqual(TEXT("frame body 뒤 Y"), PlateFrame->GetRelativeLocation().Y, -1.7, 1e-3);
				TestEqual(TEXT("frame 폭 54cm"), PlateFrame->GetRelativeScale3D().X, 0.54, 1e-3);
				TestEqual(TEXT("frame 높이 13cm"), PlateFrame->GetRelativeScale3D().Z, 0.13, 1e-3);
			}
			for (UStaticMeshComponent* SecurityStrip : { Car->FrontPlateSecurityStripComp, Car->BackPlateSecurityStripComp })
			{
				TestTrue(TEXT("KOR strip cube mesh"), SecurityStrip->GetStaticMesh() != nullptr
					&& SecurityStrip->GetStaticMesh()->GetPathName().Contains(TEXT("/Engine/BasicShapes/Cube")));
				TestTrue(TEXT("KOR strip registered but Content field를 위해 hidden"), SecurityStrip->IsRegistered() && !SecurityStrip->IsVisible());
				UMaterialInstanceDynamic* StripMID = Cast<UMaterialInstanceDynamic>(SecurityStrip->GetMaterial(0));
				TestTrue(TEXT("KOR strip blue MID"), StripMID != nullptr
					&& StripMID->K2_GetVectorParameterValue(TEXT("Color")).Equals(KoreaSecurityBlue, 1e-4f));
				TestEqual(TEXT("KOR strip 좌측 X"), SecurityStrip->GetRelativeLocation().X, -22.0, 1e-3);
				TestEqual(TEXT("KOR strip exterior Y"), SecurityStrip->GetRelativeLocation().Y, 1.35, 1e-3);
				TestEqual(TEXT("KOR strip 폭 4cm"), SecurityStrip->GetRelativeScale3D().X, 0.04, 1e-3);
			}
		}
		if (Car->FrontPlateText && Car->BackPlateText)
		{
			TestTrue(TEXT("앞 텍스트 registered"), Car->FrontPlateText->IsRegistered());
			TestTrue(TEXT("뒤 텍스트 registered"), Car->BackPlateText->IsRegistered());
			TestTrue(TEXT("앞 텍스트 visible"), Car->FrontPlateText->IsVisible());
			TestTrue(TEXT("뒤 텍스트 visible"), Car->BackPlateText->IsVisible());
			TestTrue(TEXT("앞 텍스트 font"), Car->FrontPlateText->Font != nullptr);
			TestTrue(TEXT("앞 텍스트 material"), Car->FrontPlateText->TextMaterial != nullptr);
			TestTrue(TEXT("텍스트 검정"), Car->FrontPlateText->TextRenderColor == FColor::Black);
			TestEqual(TEXT("텍스트 Content blue field 뒤 text field 중심 X"), Car->FrontPlateText->GetRelativeLocation().X, 4.0, 1e-3);
			TestEqual(TEXT("텍스트 plate 바깥 오프셋"), Car->FrontPlateText->GetRelativeLocation().Y, 1.55, 1e-3);
			TestEqual(TEXT("텍스트 plate normal yaw"), FRotator::NormalizeAxis(Car->FrontPlateText->GetRelativeRotation().Yaw), 90.0, 1e-3);
			TestEqual(TEXT("텍스트 수평 roll"), FRotator::NormalizeAxis(Car->FrontPlateText->GetRelativeRotation().Roll), 0.0, 1e-3);
			TestEqual(TEXT("텍스트 1.2배 확대 world size"), static_cast<double>(Car->FrontPlateText->WorldSize), 10.8, 1e-3);
			TestEqual(TEXT("텍스트 remaining field horizontal scale"), static_cast<double>(Car->FrontPlateText->XScale), 0.80, 1e-3);
		}
	}

	// 새 actor라도 같은 JSON 입력이면 같은 번호 → 로드/재빌드 flicker 방지.
	ACarActor* ReloadedCar = World->SpawnActor<ACarActor>();
	if (TestNotNull(TEXT("재로드 차량 스폰"), ReloadedCar))
	{
		ReloadedCar->InitFromPos(Source, nullptr, 100.f);
		TestEqual(TEXT("같은 JSON 재로드 번호 결정성"), ReloadedCar->GetPlateNumber(), Expected);
		ReloadedCar->Destroy();
	}
	Car->Destroy();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
