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
#include "../Park3DDataPaths.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"

namespace
{
	/** 윤곽선 굵기(cm) — 카메라 스트림(1280×720, 수십 m)에서 보이는 최소 굵기. */
	constexpr float OverlayThicknessCm = 6.f;
	/** 다시 그리는 주기(초) — 강조된 차가 움직이면 따라간다(car.drive·시뮬). */
	constexpr float OverlayRedrawSec = 0.1f;
	/** 점선 — 선 길이·틈(cm). */
	constexpr float PolyDashCm = 100.f;
	constexpr float PolyGapCm = 60.f;

	double PolyCross(const FVector2D& O, const FVector2D& A, const FVector2D& B)
	{
		return (A.X - O.X) * (B.Y - O.Y) - (A.Y - O.Y) * (B.X - O.X);
	}
}

bool FRpcPreviewPolygon::Prepare()
{
	// 연속 중복(1 cm 이내)·닫는 점(첫 점 반복) 제거.
	TArray<FVector2D> Clean;
	for (const FVector2D& P : Points)
	{
		if (Clean.Num() > 0 && FVector2D::Distance(Clean.Last(), P) < 1.0) continue;
		Clean.Add(P);
	}
	while (Clean.Num() > 1 && FVector2D::Distance(Clean[0], Clean.Last()) < 1.0) { Clean.Pop(); }
	Points = MoveTemp(Clean);
	Triangles.Reset();
	if (Points.Num() < 3) return false;

	// 귀 자르기 — 볼록/오목 단순 다각형. 방향(CW/CCW)은 부호 면적으로 맞춘다.
	double Area2 = 0.0;
	for (int32 i = 0; i < Points.Num(); ++i) { Area2 += PolyCross(FVector2D::ZeroVector, Points[i], Points[(i + 1) % Points.Num()]); }
	const double Sign = Area2 >= 0.0 ? 1.0 : -1.0;

	TArray<int32> V;
	for (int32 i = 0; i < Points.Num(); ++i) { V.Add(i); }
	while (V.Num() > 3)
	{
		bool bClipped = false;
		for (int32 k = 0; k < V.Num(); ++k)
		{
			const int32 Ia = V[(k + V.Num() - 1) % V.Num()], Ib = V[k], Ic = V[(k + 1) % V.Num()];
			const FVector2D &A = Points[Ia], &B = Points[Ib], &C = Points[Ic];
			const double Cr = PolyCross(A, B, C) * Sign;
			if (FMath::Abs(Cr) < 1e-6) { V.RemoveAt(k); bClipped = true; break; }   // 일직선 꼭짓점은 버린다
			if (Cr < 0.0) continue;                                                   // 오목 꼭짓점
			bool bInside = false;
			for (const int32 Ip : V)
			{
				if (Ip == Ia || Ip == Ib || Ip == Ic) continue;
				const FVector2D& P = Points[Ip];
				if (PolyCross(A, B, P) * Sign >= 0.0 && PolyCross(B, C, P) * Sign >= 0.0 && PolyCross(C, A, P) * Sign >= 0.0) { bInside = true; break; }
			}
			if (bInside) continue;
			Triangles.Append({ Ia, Ib, Ic });
			V.RemoveAt(k);
			bClipped = true;
			break;
		}
		if (!bClipped) break;   // 자기교차 — 채움 포기(윤곽은 그린다)
	}
	if (V.Num() == 3)
	{
		Triangles.Append({ V[0], V[1], V[2] });
	}
	else
	{
		Triangles.Reset();
	}
	return true;
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
	return CarHighlights.Num() == 0 && PresetHighlights.Num() == 0 && Previews.Num() == 0 && !Lot.IsSet()
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

void ARpcOverlayActor::DrawPolygon(const FRpcPreviewPolygon& Poly)
{
	const int32 N = Poly.Points.Num();
	if (N < 3) return;
	if (Poly.bFill && Poly.Triangles.Num() >= 3)
	{
		TArray<FVector> Verts;
		Verts.Reserve(N);
		for (const FVector2D& P : Poly.Points) { Verts.Add(FVector(P.X, P.Y, Poly.ZCm)); }
		// 앞뒷면 둘 다 — 감기 방향과 무관하게 위에서 보이게.
		TArray<int32> Idx = Poly.Triangles;
		for (int32 t = 0; t + 2 < Poly.Triangles.Num(); t += 3)
		{
			Idx.Append({ Poly.Triangles[t], Poly.Triangles[t + 2], Poly.Triangles[t + 1] });
		}
		FColor Fill = Poly.Color.ToFColor(true);
		Fill.A = static_cast<uint8>(FMath::Clamp(Poly.Opacity, 0.f, 1.f) * 255.f);
		Lines->DrawMesh(Verts, Idx, Fill, SDPG_World, /*LifeTime=*/0.f);
	}
	if (!Poly.bLine) return;
	const float Z = Poly.ZCm + 1.f;   // 채움 위
	float Phase = 0.f;                // 점선 위상 — 모서리를 건너 이어진다
	for (int32 i = 0; i < N; ++i)
	{
		const FVector A(Poly.Points[i].X, Poly.Points[i].Y, Z);
		const FVector B(Poly.Points[(i + 1) % N].X, Poly.Points[(i + 1) % N].Y, Z);
		if (!Poly.bDashed)
		{
			Lines->DrawLine(A, B, Poly.Color, SDPG_World, Poly.LineWidthCm);
			continue;
		}
		const float Len = FVector::Dist(A, B);
		const FVector Dir = (B - A).GetSafeNormal();
		float s = 0.f;
		while (s < Len)
		{
			const bool bOn = Phase < PolyDashCm;
			const float Step = FMath::Min(Len - s, bOn ? PolyDashCm - Phase : PolyDashCm + PolyGapCm - Phase);
			if (bOn) { Lines->DrawLine(A + Dir * s, A + Dir * (s + Step), Poly.Color, SDPG_World, Poly.LineWidthCm); }
			s += Step;
			Phase = FMath::Fmod(Phase + Step, PolyDashCm + PolyGapCm);
		}
	}
}

FString ARpcOverlayActor::GetLotFilePath(const UWorld* World)
{
	const FString Level = World ? UWorld::RemovePIEPrefix(World->GetMapName()) : FString(TEXT("NoWorld"));
	return Park3DDataPaths::GetDataFilePath(TEXT("Lot"), *FString::Printf(TEXT("Lot_%s.json"), *Level));
}

TSharedPtr<FJsonObject> ARpcOverlayActor::PolygonToJson(const FRpcPreviewPolygon& Poly)
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Pts;
	for (const FVector2D& P : Poly.Points)
	{
		TSharedPtr<FJsonObject> J = MakeShared<FJsonObject>();
		J->SetNumberField(TEXT("x"), P.X / 100.0);
		J->SetNumberField(TEXT("y"), P.Y / 100.0);
		Pts.Add(MakeShared<FJsonValueObject>(J));
	}
	O->SetArrayField(TEXT("points"), Pts);
	O->SetNumberField(TEXT("z"), Poly.ZCm / 100.0);
	O->SetBoolField(TEXT("fill"), Poly.bFill);
	O->SetNumberField(TEXT("opacity"), Poly.Opacity);
	O->SetBoolField(TEXT("line"), Poly.bLine);
	O->SetNumberField(TEXT("lineWidth"), Poly.LineWidthCm / 100.0);
	O->SetBoolField(TEXT("dashed"), Poly.bDashed);
	O->SetStringField(TEXT("color"), TEXT("#") + Poly.Color.ToFColor(true).ToHex().Left(6));
	return O;
}

bool ARpcOverlayActor::PolygonFromJson(const TSharedPtr<FJsonObject>& O, FRpcPreviewPolygon& Out)
{
	if (!O.IsValid()) return false;
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (!O->TryGetArrayField(TEXT("points"), Arr)) return false;
	Out.Points.Reset();
	for (const TSharedPtr<FJsonValue>& V : *Arr)
	{
		const TSharedPtr<FJsonObject>* P = nullptr;
		if (!V->TryGetObject(P) || !P) continue;
		Out.Points.Add(FVector2D((*P)->GetNumberField(TEXT("x")) * 100.0, (*P)->GetNumberField(TEXT("y")) * 100.0));
	}
	double D = 0.0;
	bool B = false;
	FString S;
	if (O->TryGetNumberField(TEXT("z"), D)) { Out.ZCm = D * 100.0; }
	if (O->TryGetBoolField(TEXT("fill"), B)) { Out.bFill = B; }
	if (O->TryGetNumberField(TEXT("opacity"), D)) { Out.Opacity = FMath::Clamp(D, 0.0, 1.0); }
	if (O->TryGetBoolField(TEXT("line"), B)) { Out.bLine = B; }
	if (O->TryGetNumberField(TEXT("lineWidth"), D)) { Out.LineWidthCm = FMath::Max(D * 100.0, 1.0); }
	if (O->TryGetBoolField(TEXT("dashed"), B)) { Out.bDashed = B; }
	if (O->TryGetStringField(TEXT("color"), S) && S.StartsWith(TEXT("#")) && S.Len() == 7) { Out.Color = FLinearColor(FColor::FromHex(S)); }
	return Out.Prepare();
}

bool ARpcOverlayActor::SaveLot(FString& OutPath) const
{
	OutPath = GetLotFilePath(GetWorld());
	if (!Lot.IsSet())
	{
		return !IFileManager::Get().FileExists(*OutPath) || IFileManager::Get().Delete(*OutPath);
	}
	TSharedPtr<FJsonObject> O = PolygonToJson(Lot.GetValue());
	O->SetStringField(TEXT("level"), UWorld::RemovePIEPrefix(GetWorld()->GetMapName()));
	FString Text;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Text);
	FJsonSerializer::Serialize(O.ToSharedRef(), W);
	return FFileHelper::SaveStringToFile(Text, *OutPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool ARpcOverlayActor::LoadLotForWorld(UWorld* World)
{
	const FString Path = GetLotFilePath(World);
	FString Text;
	if (!World || !FFileHelper::LoadFileToString(Text, *Path)) return false;
	TSharedPtr<FJsonObject> O;
	FRpcPreviewPolygon Poly;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), O) || !PolygonFromJson(O, Poly))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Lot] 주차장 영역 파일을 읽지 못했습니다: %s"), *Path);
		return false;
	}
	ARpcOverlayActor* Ov = Get(World);
	if (!Ov) return false;
	Ov->Lot = Poly;
	Ov->Redraw();
	UE_LOG(LogTemp, Log, TEXT("[Lot] 주차장 영역 %d점 복원 ← %s"), Poly.Points.Num(), *Path);
	return true;
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

	// 주차장 영역(가장 아래에 깔린다 — 먼저 그림).
	if (Lot.IsSet()) { DrawPolygon(Lot.GetValue()); }

	// 미리보기 고스트.
	for (const TPair<FString, FRpcPreviewSet>& KV : Previews)
	{
		for (const FRpcPreviewPolygon& Poly : KV.Value.Polygons) { DrawPolygon(Poly); }
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
