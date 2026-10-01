// Copyright Epic Games, Inc. All Rights Reserved.

#include "RpcOverlayActor.h"
#include "../CarActor.h"
#include "../PTZCameraActor.h"
#include "../CameraControlManager.h"
#include "../ParkingPresetManager.h"
#include "Components/LineBatchComponent.h"
#include "Components/TextRenderComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Engine/World.h"

namespace
{
	/** 윤곽선 굵기(cm) — 카메라 스트림(1280×720, 수십 m)에서 보이는 최소 굵기. */
	constexpr float OverlayThicknessCm = 6.f;
	/** 다시 그리는 주기(초) — 강조된 차가 움직이면 따라간다(car.drive·시뮬). */
	constexpr float OverlayRedrawSec = 0.1f;
}

ARpcOverlayActor::ARpcOverlayActor()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Lines = CreateDefaultSubobject<ULineBatchComponent>(TEXT("OverlayLines"));
	Lines->SetupAttachment(RootComponent);
	Lines->SetCastShadow(false);
	Lines->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

ARpcOverlayActor* ARpcOverlayActor::Get(UWorld* World, bool bCreate)
{
	if (!World) return nullptr;
	if (ARpcOverlayActor* Found = Cast<ARpcOverlayActor>(UGameplayStatics::GetActorOfClass(World, ARpcOverlayActor::StaticClass())))
	{
		return Found;
	}
	if (!bCreate) return nullptr;
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<ARpcOverlayActor>(FVector::ZeroVector, FRotator::ZeroRotator, SP);
}

bool ARpcOverlayActor::IsEmpty() const
{
	return CarHighlights.Num() == 0 && PresetHighlights.Num() == 0 && Previews.Num() == 0
		&& !bLabelCars && !bLabelPresets && !bLabelCameras;
}

void ARpcOverlayActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (IsEmpty() && UsedLabels == 0) return;
	RedrawAccum += DeltaSeconds;
	if (RedrawAccum < OverlayRedrawSec) return;
	RedrawAccum = 0.f;
	Redraw();
}

void ARpcOverlayActor::DrawOutlineBox(const FVector& Center, const FVector& HalfSize, const FQuat& Rot, const FLinearColor& Color, float Thickness)
{
	Lines->DrawBox(Center, HalfSize, Rot, Color, /*LifeTime=*/0.f, SDPG_World, Thickness);
}

void ARpcOverlayActor::DrawFace(const FRpcPreviewFace& F, const FLinearColor& Color)
{
	const FQuat Q(FRotator(0.f, F.YawDeg, 0.f));
	const FVector A = Q.RotateVector(FVector(F.LengthCm * 0.5f, 0.f, 0.f));
	const FVector B = Q.RotateVector(FVector(0.f, F.WidthCm * 0.5f, 0.f));
	const FVector C[4] = { F.Center + A + B, F.Center + A - B, F.Center - A - B, F.Center - A + B };
	for (int32 k = 0; k < 4; ++k)
	{
		Lines->DrawLine(C[k], C[(k + 1) % 4], Color, SDPG_World, OverlayThicknessCm);
	}
	// 반투명 채움 — 고스트임이 한눈에 보이게(목록에는 없는 면).
	FColor Fill = Color.ToFColor(true);
	Fill.A = 70;
	Lines->DrawMesh({ C[0], C[1], C[2], C[3] }, { 0, 1, 2, 0, 2, 3 }, Fill, SDPG_World, /*LifeTime=*/0.f);
}

void ARpcOverlayActor::PlaceLabel(const FVector& WorldLoc, const FString& Text, const FColor& Color, const FRotator& ViewRot)
{
	if (!LabelPool.IsValidIndex(UsedLabels))
	{
		// 엔진 기본 폰트(RobotoDistanceField) — 프로젝트 한글 폰트는 TextRender 에 안 그려진다(보드 #63). 라벨은 ASCII 만 쓴다.
		UTextRenderComponent* T = NewObject<UTextRenderComponent>(this);
		T->SetupAttachment(RootComponent);
		T->SetHorizontalAlignment(EHTA_Center);
		T->SetVerticalAlignment(EVRTA_TextBottom);
		T->SetWorldSize(60.f);
		T->SetCastShadow(false);
		T->RegisterComponent();
		LabelPool.Add(T);
	}
	UTextRenderComponent* T = LabelPool[UsedLabels++];
	T->SetText(FText::FromString(Text));
	T->SetTextRenderColor(Color);
	T->SetWorldLocation(WorldLoc);
	// TextRender 는 로컬 +X 쪽에서 읽힌다 → 메인 뷰 화면과 평행하게(글자 위 = 화면 위). 월드 위쪽에 맞추면
	// 내려다볼 때(view.topDown) 글자가 옆으로 눕는다. PTZ 스트림에서는 비스듬히 보일 수 있다.
	const FRotationMatrix VM(ViewRot);
	T->SetWorldRotation(FRotationMatrix::MakeFromXZ(-VM.GetUnitAxis(EAxis::X), VM.GetUnitAxis(EAxis::Z)).Rotator());
	T->SetVisibility(true);
}

void ARpcOverlayActor::Redraw()
{
	UWorld* World = GetWorld();
	if (!World || !Lines) return;
	Lines->Flush();
	UsedLabels = 0;

	FRotator ViewRot = FRotator(-89.f, 0.f, 0.f);   // 메인 뷰 회전(라벨 방향 기준)
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		if (PC->PlayerCameraManager) { ViewRot = PC->PlayerCameraManager->GetCameraRotation(); }
	}

	// 차량 — 강조 윤곽 + 라벨.
	if (CarHighlights.Num() > 0 || bLabelCars)
	{
		for (TActorIterator<ACarActor> It(World); It; ++It)
		{
			if (It->IsHidden()) continue;
			const FString& Id = It->CarData.id;
			const FLinearColor* HL = CarHighlights.Find(Id);
			if (!HL && !bLabelCars) continue;
			FVector Origin, Extent;
			It->GetActorBounds(/*bOnlyCollidingComponents=*/false, Origin, Extent);
			if (HL)
			{
				// 차 축에 맞춘 상자(바운즈는 월드 축이라 비스듬한 차에서 크다) — 액터 회전으로 로컬 반크기를 근사.
				const FQuat Q = It->GetActorQuat();
				FVector LocalExt = Extent;
				if (const UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(It->GetRootComponent()))
				{
					LocalExt = Root->CalcLocalBounds().BoxExtent * It->GetActorScale3D();
				}
				DrawOutlineBox(Origin, LocalExt + FVector(10.f), Q, *HL, OverlayThicknessCm);
			}
			if (bLabelCars)
			{
				PlaceLabel(Origin + FVector(0.f, 0.f, Extent.Z + 40.f), Id, FColor(255, 230, 80), ViewRot);
			}
		}
	}

	// 프리셋 — 면 윤곽 강조 + 라벨(P<idx>). 면 기하는 바닥 번호와 같은 목록(CollectSlotNumbers).
	if (PresetHighlights.Num() > 0 || bLabelPresets)
	{
		if (AParkingPresetManager* PM = Cast<AParkingPresetManager>(UGameplayStatics::GetActorOfClass(World, AParkingPresetManager::StaticClass())))
		{
			TArray<FParkingSlotNumberInfo> Slots;
			PM->CollectSlotNumbers(PM->ResolvePresets(), Slots);
			TMap<int32, FBox> PresetBoxes;
			for (const FParkingSlotNumberInfo& S : Slots)
			{
				if (!S.bFromPreset) continue;
				PresetBoxes.FindOrAdd(S.PresetIdx, FBox(ForceInit)) += S.Center;
				if (const FLinearColor* HL = PresetHighlights.Find(S.PresetIdx))
				{
					FRpcPreviewFace F;
					F.Center = S.Center + FVector(0.f, 0.f, 3.f);
					F.YawDeg = FMath::RadiansToDegrees(FMath::Atan2(S.AxisDir.Y, S.AxisDir.X));
					F.WidthCm = S.WidthCm;
					F.LengthCm = S.LengthCm;
					const FQuat Q(FRotator(0.f, F.YawDeg, 0.f));
					DrawOutlineBox(F.Center, FVector(F.LengthCm * 0.5f, F.WidthCm * 0.5f, 2.f), Q, *HL, OverlayThicknessCm);
				}
			}
			if (bLabelPresets)
			{
				for (const TPair<int32, FBox>& KV : PresetBoxes)
				{
					PlaceLabel(KV.Value.GetCenter() + FVector(0.f, 0.f, 150.f), FString::Printf(TEXT("P%d"), KV.Key), FColor(120, 255, 140), ViewRot);
				}
			}
		}
	}

	// 카메라 라벨(C<camId>).
	if (bLabelCameras)
	{
		for (TActorIterator<ACameraControlManager> It(World); It; ++It)
		{
			for (int32 i = 0; i < It->GetCameraCount(); ++i)
			{
				if (const APTZCameraActor* Cam = It->GetCamera(i))
				{
					PlaceLabel(Cam->GetActorLocation() + FVector(0.f, 0.f, 60.f), FString::Printf(TEXT("C%d"), i + 1), FColor(120, 200, 255), ViewRot);
				}
			}
			break;
		}
	}

	// 미리보기 고스트.
	for (const TPair<FString, FRpcPreviewSet>& KV : Previews)
	{
		for (const FRpcPreviewFace& F : KV.Value.Faces) { DrawFace(F, KV.Value.Color); }
		for (const FRpcPreviewBox& B : KV.Value.Boxes)
		{
			DrawOutlineBox(B.Center, B.HalfSizeCm, FQuat(FRotator(0.f, B.YawDeg, 0.f)), KV.Value.Color, OverlayThicknessCm);
		}
	}

	for (int32 i = UsedLabels; i < LabelPool.Num(); ++i)
	{
		if (LabelPool[i]) { LabelPool[i]->SetVisibility(false); }
	}
}
