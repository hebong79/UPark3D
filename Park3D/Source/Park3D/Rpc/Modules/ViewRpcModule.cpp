// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewRpcModule.h"
#include "../RpcDispatcher.h"
#include "../RpcParamUtil.h"
#include "../CamStreamSubsystem.h"
#include "../../CameraControlManager.h"
#include "../../CameraControlLibrary.h"
#include "../../CarActor.h"
#include "../../PTZCameraActor.h"
#include "../../ParkingPresetManager.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Camera/PlayerCameraManager.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

namespace
{
	/** view.pick 라인트레이스 사거리(cm). measure.cameraHeight 와 같은 10km. */
	constexpr double ViewPickTraceCm = 1.0e6;

	/** 메인 뷰의 권위 = 첫 번째 플레이어 컨트롤러. 없으면 -32000(다른 모듈의 "월드 없음"과 같은 실패 방식). */
	APlayerController* GetViewController(UWorld* World, FRpcError& E)
	{
		if (!World)
		{
			E.FailDomain(TEXT("월드 컨텍스트 없음"));
			return nullptr;
		}
		APlayerController* PC = World->GetFirstPlayerController();
		if (!PC)
		{
			E.FailDomain(TEXT("플레이어 컨트롤러 없음 — 월드/맵이 로드된 상태(PIE 또는 -game)가 필요합니다."));
		}
		return PC;
	}

	/**
	 * 메인 뷰 MJPEG 채널을 소유한 서브시스템(월드에 없으면 nullptr). CamRpcModule 과 동형.
	 *
	 * ⚠ 이름을 `GetStreamSubsystem` 으로 두면 **unity 빌드에서 CamRpcModule.cpp 와 충돌한다**
	 *   (C2084 — 익명 네임스페이스가 한 번역단위로 합쳐지면서 재정의가 된다).
	 *   처음에 그 이름으로 뒀다가 실제로 빌드가 깨졌다. adaptive unity 가 **변경된 파일을
	 *   unity 에서 빼기 때문에**, 이 파일을 고친 직후의 빌드에서는 통과해 버려 더 늦게 터진다.
	 */
	UCamStreamSubsystem* GetMainStreamSubsystem(UWorld* World)
	{
		return World ? World->GetSubsystem<UCamStreamSubsystem>() : nullptr;
	}

	/**
	 * 현재 메인 뷰 상태 → {pos,rot,fov,streamPort}. view.get / view.set / view.lookAt 이 공유한다.
	 * ⚠ PlayerCameraManager 의 POV 는 틱 캐시다. set 직후 그냥 읽으면 "바꾸기 전 값"이 응답으로 나가
	 *   호출자가 실패로 오해한다. 0 델타로 강제 갱신한 뒤 읽어, 방금 준 명령이 실제로 반영된 값을 돌려준다.
	 *   (반영되지 않았다면 요청값이 아니라 반영 안 된 값이 그대로 나간다 — 조용한 성공을 만들지 않는다.)
	 */
	TSharedPtr<FJsonValue> BuildViewState(UWorld* World, APlayerController* PC, float MetersToUU)
	{
		if (PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->UpdateCamera(0.f);
		}

		FVector ViewLoc = FVector::ZeroVector;
		FRotator ViewRot = FRotator::ZeroRotator;
		PC->GetPlayerViewPoint(ViewLoc, ViewRot);
		const FCamVec3 PosMeters = UCameraControlLibrary::WorldToUnrealMeters(ViewLoc, MetersToUU);

		// pitch/yaw 는 ±180 로 접어서 내보낸다. FRotator 는 0~360 으로 정규화돼 있을 수 있어
		// 그대로 내보내면 "아래로 30도"가 330 으로 보이고, 웹의 부호 판정이 뒤집힌다.
		TSharedPtr<FJsonObject> RotObj = MakeShared<FJsonObject>();
		RotObj->SetNumberField(TEXT("pitch"), FRotator::NormalizeAxis(ViewRot.Pitch));
		RotObj->SetNumberField(TEXT("yaw"), FRotator::NormalizeAxis(ViewRot.Yaw));

		// 메인 뷰 스트림 포트(http://<host>:<port>/). 채널이 안 떠 있으면 0 = 영상 없음(cam.list 의 규약과 동일).
		int32 StreamPort = 0;
		if (UCamStreamSubsystem* S = GetMainStreamSubsystem(World))
		{
			S->GetMainStreamPort(StreamPort);
		}

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetObjectField(TEXT("pos"), RpcDto::Vec3(PosMeters.x, PosMeters.y, PosMeters.z));
		O->SetObjectField(TEXT("rot"), RotObj);
		O->SetNumberField(TEXT("fov"), PC->PlayerCameraManager ? PC->PlayerCameraManager->GetFOVAngle() : 0.f);
		O->SetNumberField(TEXT("streamPort"), StreamPort);
		return RpcDto::MakeObject(O);
	}

	/**
	 * 시점 회전 적용. 폰 액터 회전 + 컨트롤러 회전을 함께 맞춘다
	 * (APark3DGameMode::ApplyCameraStart 와 같은 경로 — 컨트롤러만 돌리면 폰 이동 방향이 어긋난다).
	 */
	void ApplyViewRotation(APlayerController* PC, const FRotator& NewRot)
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			Pawn->SetActorRotation(NewRot);
		}
		PC->SetControlRotation(NewRot);
	}

	/**
	 * 시점 위치 적용. 시점은 폰 위치 + 눈높이(APawn::GetPawnViewLocation)라 눈높이를 빼고 폰을 놓아야
	 * view.get 과 왕복이 맞는다(ParkFlyPawn 의 베이스 ADefaultPawn 은 BaseEyeHeight=0 이지만 가정하지 않는다).
	 */
	bool ApplyViewLocation(APlayerController* PC, const FVector& NewViewLoc, FRpcError& E)
	{
		APawn* Pawn = PC->GetPawn();
		if (!Pawn)
		{
			E.FailDomain(TEXT("플레이어 폰 없음 — 시점을 옮길 대상이 없습니다."));
			return false;
		}
		Pawn->SetActorLocation(NewViewLoc - FVector(0.f, 0.f, Pawn->BaseEyeHeight));
		return true;
	}

	/** id 는 문자열·숫자 둘 다 받는다(carNameId 는 문자열, camId·presetIdx·faceNumber 는 숫자). */
	FString ReadTargetId(const TSharedPtr<FJsonObject>& P)
	{
		FString Id = RpcParam::GetString(P, TEXT("id"));
		if (Id.IsEmpty() && RpcParam::Has(P, TEXT("id")))
		{
			Id = FString::FromInt(RpcParam::GetInt(P, TEXT("id")));
		}
		return Id;
	}

	/**
	 * {target:"car"|"face"|"preset"|"camera", id} → 월드 중심(cm)과 수평 반경(cm). 보드 #1102 — 점이 아니라 대상으로 이동.
	 * face 의 id 는 faceKey("level:…#n" / "preset:i#s") 또는 바닥 번호. 매니저는 스폰하지 않는다(조회가 씬을 바꾸지 않게).
	 */
	bool ResolveViewTarget(UWorld* World, ACameraControlManager* CamMgr, const TSharedPtr<FJsonObject>& P,
		FVector& OutCenter, double& OutRadiusCm, FRpcError& E)
	{
		const FString Kind = RpcParam::GetString(P, TEXT("target")).ToLower();
		const FString Id = ReadTargetId(P);
		if (Id.IsEmpty())
		{
			E.FailDomain(TEXT("target 에는 id 가 필요합니다"), ERpcErrorKind::BadParams);
			return false;
		}
		if (Kind == TEXT("car"))
		{
			for (TActorIterator<ACarActor> It(World); It; ++It)
			{
				if (It->CarData.id == Id)
				{
					FVector Origin, Extent;
					It->GetActorBounds(/*bOnlyCollidingComponents=*/false, Origin, Extent);
					OutCenter = Origin;
					OutRadiusCm = FMath::Max(Extent.X, Extent.Y);
					return true;
				}
			}
			E.FailDomain(FString::Printf(TEXT("차량 없음: %s"), *Id), ERpcErrorKind::NotFound);
			return false;
		}
		if (Kind == TEXT("camera"))
		{
			APTZCameraActor* Cam = CamMgr->GetCamera(FCString::Atoi(*Id) - 1);
			if (!Cam)
			{
				E.FailDomain(FString::Printf(TEXT("카메라 없음: camId=%s"), *Id), ERpcErrorKind::NotFound);
				return false;
			}
			OutCenter = Cam->GetActorLocation();
			OutRadiusCm = 100.0;
			return true;
		}
		if (Kind == TEXT("face") || Kind == TEXT("preset"))
		{
			AParkingPresetManager* PresetMgr = Cast<AParkingPresetManager>(
				UGameplayStatics::GetActorOfClass(World, AParkingPresetManager::StaticClass()));
			TArray<FParkingSlotNumberInfo> Slots;
			if (PresetMgr) { PresetMgr->CollectSlotNumbers(PresetMgr->ResolvePresets(), Slots); }
			const bool bNumeric = Id.IsNumeric();
			const int32 IdNum = FCString::Atoi(*Id);
			FBox Box(ForceInit);
			for (const FParkingSlotNumberInfo& S : Slots)
			{
				const bool bMatch = Kind == TEXT("face")
					? (S.FaceKey() == Id || (bNumeric && S.Number == IdNum))
					: (S.bFromPreset && S.PresetIdx == IdNum);
				if (!bMatch) continue;
				const double R = 0.5 * FMath::Max(S.LengthCm, S.WidthCm);
				Box += S.Center + FVector(R, R, 0.0);
				Box += S.Center - FVector(R, R, 0.0);
				if (Kind == TEXT("face")) break;   // 번호 중복이면 앞의 면(preset.numbers 순서)
			}
			if (!Box.IsValid)
			{
				E.FailDomain(FString::Printf(TEXT("%s 없음: %s (preset.numbers 로 확인)"), Kind == TEXT("face") ? TEXT("주차면") : TEXT("프리셋"), *Id),
					ERpcErrorKind::NotFound);
				return false;
			}
			OutCenter = Box.GetCenter();
			OutRadiusCm = FMath::Max(Box.GetExtent().X, Box.GetExtent().Y);
			return true;
		}
		E.FailDomain(FString::Printf(TEXT("target 은 car | face | preset | camera 중 하나입니다: %s"), *Kind), ERpcErrorKind::BadParams);
		return false;
	}
}

void FViewRpcModule::Register(URpcDispatcher& Dispatcher)
{
	// view.get — 현재 메인 뷰 상태 {pos,rot,fov,streamPort}.
	Dispatcher.Register(TEXT("view.get"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* World = GetWorldPtr();
		APlayerController* PC = GetViewController(World, E); if (!PC) return nullptr;
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;   // 미터 변환 계수 권위
		return BuildViewState(World, PC, Mgr->MetersToUU);
	});

	// view.set {pos?, rot?, fov?} — 부분 갱신. 주지 않은 축은 현재 값 유지.
	Dispatcher.Register(TEXT("view.set"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* World = GetWorldPtr();
		APlayerController* PC = GetViewController(World, E); if (!PC) return nullptr;
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;

		FVector CurLoc = FVector::ZeroVector;
		FRotator CurRot = FRotator::ZeroRotator;
		PC->GetPlayerViewPoint(CurLoc, CurRot);

		if (RpcParam::Has(P, TEXT("pos")))
		{
			// 현재 값을 Default 로 넘긴다 → pos:{z:20} 처럼 한 축만 줘도 나머지는 그대로 유지된다.
			const FCamVec3 CurMeters = UCameraControlLibrary::WorldToUnrealMeters(CurLoc, Mgr->MetersToUU);
			const FVector NewMeters = RpcParam::GetVec3(P, TEXT("pos"),
				FVector(CurMeters.x, CurMeters.y, CurMeters.z));
			const FVector NewWorld = UCameraControlLibrary::UnrealMetersToWorld(
				FCamVec3{ static_cast<float>(NewMeters.X), static_cast<float>(NewMeters.Y), static_cast<float>(NewMeters.Z) },
				Mgr->MetersToUU);
			if (!ApplyViewLocation(PC, NewWorld, E)) return nullptr;
		}

		if (RpcParam::Has(P, TEXT("rot")))
		{
			// rot 은 {pitch,yaw} 2필드라 GetVec3 를 쓸 수 없다. 없는 필드는 현재 값 유지.
			double NewPitch = FRotator::NormalizeAxis(CurRot.Pitch);
			double NewYaw = FRotator::NormalizeAxis(CurRot.Yaw);
			const TSharedPtr<FJsonObject>* RotObj = nullptr;
			RpcParam::MarkRead(P, TEXT("rot"));
			if (!P->TryGetObjectField(TEXT("rot"), RotObj) || !RotObj || !RotObj->IsValid())
			{
				E.FailDomain(TEXT("rot 은 {pitch?,yaw?} 객체여야 합니다."));
				return nullptr;
			}
			(*RotObj)->TryGetNumberField(TEXT("pitch"), NewPitch);
			(*RotObj)->TryGetNumberField(TEXT("yaw"), NewYaw);
			// pitch 는 양수가 위다. ±89 를 넘으면 정면이 뒤집혀(짐벌) 방향키가 거꾸로 도는 사고가 난다 — 잘라낸다.
			NewPitch = FMath::Clamp(NewPitch, -89.0, 89.0);
			ApplyViewRotation(PC, FRotator(NewPitch, NewYaw, 0.f));
		}

		if (RpcParam::Has(P, TEXT("fov")))
		{
			const double NewFov = RpcParam::GetFloat(P, TEXT("fov"), 0.0);
			if (NewFov < 0.0 || NewFov >= 180.0)
			{
				E.FailDomain(FString::Printf(
					TEXT("fov 는 0(잠금 해제) 또는 0 초과 180 미만이어야 합니다(수평 화각, 도): %.3f"), NewFov));
				return nullptr;
			}
			if (!PC->PlayerCameraManager)
			{
				E.FailDomain(TEXT("PlayerCameraManager 없음 — fov 를 적용할 수 없습니다."));
				return nullptr;
			}
			// 화각 잠금은 화면을 통째로 바꾸는데 흔적이 남지 않아, "화면 비율이 이상하다"는 신고가 와도
			// 누가 언제 걸었는지 추적할 수 없었다(2026-08-13, LockedFOV=138 사고). 그래서 앞뒤 값을 남긴다.
			const float PrevFov = PC->PlayerCameraManager->GetFOVAngle();

			if (NewFov == 0.0)
			{
				// fov=0 은 잠금 해제다. LockedFOV 를 0 으로 되돌려 뷰타깃 기본 화각(DefaultFOV)으로 복귀시킨다.
				// 이 수단이 없으면 한 번 건 화각을 되돌릴 방법이 없다.
				PC->PlayerCameraManager->UnlockFOV();
				UE_LOG(LogTemp, Log, TEXT("[View] 화각 잠금 해제: %.2f° → 기본 %.2f°"),
					PrevFov, PC->PlayerCameraManager->GetFOVAngle());
			}
			else
			{
				// SetFOV 는 LockedFOV 를 건다. GetFOVAngle()·ULocalPlayer::GetViewPoint·메인 뷰 캡처가 모두 이 값을 쓴다.
				PC->PlayerCameraManager->SetFOV(static_cast<float>(NewFov));
				UE_LOG(LogTemp, Log, TEXT("[View] 화각 잠금: %.2f° → %.2f°(수평)"), PrevFov, static_cast<float>(NewFov));
			}
		}

		return BuildViewState(World, PC, Mgr->MetersToUU);
	});

	// view.pick {x,y} — 메인 뷰 영상 픽셀 → 월드 지점. 좌표계는 13600 스트림 이미지(MainWidth×MainHeight).
	Dispatcher.Register(TEXT("view.pick"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* World = GetWorldPtr();
		APlayerController* PC = GetViewController(World, E); if (!PC) return nullptr;
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;

		// 픽셀 기준면은 "웹이 실제로 보고 있는 이미지" = 메인 뷰 렌더타깃이다. 게임 창 크기가 아니다
		// (렌더타깃은 창과 무관하게 MainWidth×MainHeight 로 고정된다 — CamStreamSubsystem::EnsureMainCapture).
		UCamStreamSubsystem* Stream = GetMainStreamSubsystem(World);
		if (!Stream)
		{
			E.FailDomain(TEXT("스트림 서브시스템 없음 — 메인 뷰 영상 좌표계를 정할 수 없습니다."));
			return nullptr;
		}
		const double ImageW = FMath::Max(16, Stream->MainWidth);
		const double ImageH = FMath::Max(16, Stream->MainHeight);

		// 좌표는 픽셀 {x,y} 또는 정규화 {u,v}(0~1, 왼쪽 위 원점 — 해상도와 무관, 보드 #1102).
		double PixelX = 0.0, PixelY = 0.0;
		if (RpcParam::Has(P, TEXT("u")) || RpcParam::Has(P, TEXT("v")))
		{
			double U = 0.0, V = 0.0;
			if (!RpcParam::RequireFloat(P, TEXT("u"), U, E)) return nullptr;
			if (!RpcParam::RequireFloat(P, TEXT("v"), V, E)) return nullptr;
			if (U < 0.0 || U > 1.0 || V < 0.0 || V > 1.0)
			{
				E.FailDomain(FString::Printf(TEXT("u,v 는 0~1 범위여야 합니다: (%.3f, %.3f)"), U, V));
				return nullptr;
			}
			PixelX = U * ImageW;
			PixelY = V * ImageH;
		}
		else
		{
			if (!RpcParam::RequireFloat(P, TEXT("x"), PixelX, E)) return nullptr;
			if (!RpcParam::RequireFloat(P, TEXT("y"), PixelY, E)) return nullptr;
		}
		if (PixelX < 0.0 || PixelX > ImageW || PixelY < 0.0 || PixelY > ImageH)
		{
			E.FailDomain(FString::Printf(
				TEXT("x,y 는 메인 뷰 영상 픽셀 범위(0~%d, 0~%d) 여야 합니다: (%.1f, %.1f)"),
				static_cast<int32>(ImageW), static_cast<int32>(ImageH), PixelX, PixelY));
			return nullptr;
		}

		FVector ViewLoc = FVector::ZeroVector;
		FRotator ViewRot = FRotator::ZeroRotator;
		PC->GetPlayerViewPoint(ViewLoc, ViewRot);
		const double FovDeg = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetFOVAngle() : 0.0;
		if (FovDeg <= 0.0 || FovDeg >= 180.0)
		{
			E.FailDomain(FString::Printf(TEXT("현재 화각이 유효하지 않아 픽 방향을 계산할 수 없습니다: %.3f"), FovDeg));
			return nullptr;
		}

		// 메인 뷰 캡처와 같은 투영을 재현한다:
		// SceneCaptureRendering::BuildProjectionMatrix 는 FOVAngle 을 "수평" 반각으로 쓰고
		// 수직은 렌더타깃 화면비(H/W)로 파생한다. 그래서 tanV = tanH * (H/W) 다.
		const double TanH = FMath::Tan(FMath::DegreesToRadians(FovDeg * 0.5));
		const double TanV = TanH * (ImageH / ImageW);
		const double NdcX = (2.0 * PixelX / ImageW) - 1.0;   // -1 = 왼쪽 끝, +1 = 오른쪽 끝
		const double NdcY = 1.0 - (2.0 * PixelY / ImageH);   // +1 = 위쪽 끝, -1 = 아래쪽 끝

		const FMatrix Basis = FRotationMatrix(ViewRot);
		const FVector Dir = (Basis.GetUnitAxis(EAxis::X)
			+ Basis.GetUnitAxis(EAxis::Y) * (NdcX * TanH)
			+ Basis.GetUnitAxis(EAxis::Z) * (NdcY * TanV)).GetSafeNormal();
		if (Dir.IsNearlyZero())
		{
			E.FailDomain(TEXT("픽 방향 계산 실패(시점 회전 이상)"));
			return nullptr;
		}

		// AMapFloorActor 는 NoCollision(순수 시각)이라 지면은 Landscape 가 받는다 → measure.* 와 같은 ECC_Visibility.
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ViewPick), /*bTraceComplex=*/true);
		if (APawn* Pawn = PC->GetPawn())
		{
			Params.AddIgnoredActor(Pawn);   // 트레이스가 폰 콜리전 안에서 출발한다 — 자기 자신 오탐 방지.
		}
		const bool bHit = World->LineTraceSingleByChannel(Hit, ViewLoc, ViewLoc + Dir * ViewPickTraceCm, ECC_Visibility, Params);

		// 빗나가면(콜리전 없는 바닥 — 보드 #1093) 지면 평면 z=groundZ(m, 기본 0)와 만나는 점으로 대신한다.
		// 그때도 hit=false 는 그대로 두고 source=groundPlane 으로 구분한다(world 는 그 평면 위 점).
		const double GroundZCm = RpcParam::GetFloat(P, TEXT("groundZ"), 0.0) * Mgr->MetersToUU;
		bool bGround = false;
		FVector Point = FVector::ZeroVector;
		if (bHit)
		{
			Point = Hit.ImpactPoint;
		}
		else if (Dir.Z < -1e-4)
		{
			const double T = (GroundZCm - ViewLoc.Z) / Dir.Z;
			if (T > 0.0 && T < ViewPickTraceCm)
			{
				Point = ViewLoc + Dir * T;
				bGround = true;
			}
		}
		const FCamVec3 HitMeters = UCameraControlLibrary::WorldToUnrealMeters(Point, Mgr->MetersToUU);

		// target — 무엇을 가리켰나(보드 #1102). type: car | camera | face | prop | ground | none.
		// face 는 바닥 번호와 같은 목록에서 찾는다(화면 숫자와 어긋나지 않는다). 프리셋 면이면 presetIdx·slotId 가 붙는다.
		TSharedPtr<FJsonObject> Target = MakeShared<FJsonObject>();
		FString Type;
		AActor* HitActor = bHit ? Hit.GetActor() : nullptr;
		if (ACarActor* Car = Cast<ACarActor>(HitActor))
		{
			Type = TEXT("car");
			Target->SetStringField(TEXT("id"), Car->CarData.id);
			Target->SetStringField(TEXT("carNameId"), Car->CarData.id);
		}
		else if (APTZCameraActor* Cam = Cast<APTZCameraActor>(HitActor))
		{
			Type = TEXT("camera");
			int32 CamIndex = INDEX_NONE;
			for (int32 i = 0; i < Mgr->GetCameraCount(); ++i) { if (Mgr->GetCamera(i) == Cam) { CamIndex = i; break; } }
			Target->SetNumberField(TEXT("camId"), CamIndex + 1);   // 매니저 밖 카메라면 0
			Target->SetStringField(TEXT("id"), FString::FromInt(CamIndex + 1));
		}
		if (Type.IsEmpty() && (bHit || bGround))
		{
			// 매니저를 스폰하지 않는다 — 조회가 씬을 바꾸면 안 된다.
			AParkingPresetManager* PresetMgr = Cast<AParkingPresetManager>(
				UGameplayStatics::GetActorOfClass(World, AParkingPresetManager::StaticClass()));
			FParkingSlotNumberInfo Info;
			if (PresetMgr && PresetMgr->FindSlotNumberAtWorld(Point, Info))
			{
				Type = TEXT("face");
				Target->SetStringField(TEXT("id"), Info.FaceKey());
				Target->SetStringField(TEXT("faceKey"), Info.FaceKey());
				Target->SetNumberField(TEXT("faceNumber"), Info.Number);
				if (Info.bFromPreset)
				{
					Target->SetNumberField(TEXT("presetIdx"), Info.PresetIdx);
					Target->SetNumberField(TEXT("slotId"), Info.SlotId);
				}
			}
		}
		if (Type.IsEmpty())
		{
			// 위를 향한 면에 맞았고 지면 평면 근처면 지면, 아니면 이름 있는 소품(env.list 의 name 과 같은 값).
			const bool bGroundLike = bHit && Hit.ImpactNormal.Z > 0.7 && FMath::Abs(Point.Z - GroundZCm) < 0.5 * Mgr->MetersToUU;
			if (bGround || bGroundLike || (bHit && !HitActor))
			{
				Type = TEXT("ground");
			}
			else if (bHit)
			{
				Type = TEXT("prop");
				Target->SetStringField(TEXT("id"), HitActor->GetName());
			}
			else
			{
				Type = TEXT("none");
			}
		}
		Target->SetStringField(TEXT("type"), Type);

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("hit"), bHit);
		O->SetStringField(TEXT("source"), bHit ? TEXT("trace") : (bGround ? TEXT("groundPlane") : TEXT("none")));
		O->SetObjectField(TEXT("world"), RpcDto::Vec3(HitMeters.x, HitMeters.y, HitMeters.z));
		if (bHit || bGround)
		{
			O->SetObjectField(TEXT("pos"), RpcDto::Vec3(HitMeters.x, HitMeters.y, HitMeters.z));
		}
		O->SetObjectField(TEXT("target"), Target);
		if (ACarActor* Car = Cast<ACarActor>(HitActor))
		{
			O->SetStringField(TEXT("actorId"), Car->CarData.id);   // 옛 키 — car.* 의 carNameId 와 같은 값
		}
		return RpcDto::MakeObject(O);
	});

	// view.lookAt {world, distance?} — 그 지점을 바라본다. distance 를 주면 그 거리(m)만큼 떨어진 자리로도 이동.
	Dispatcher.Register(TEXT("view.lookAt"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* World = GetWorldPtr();
		APlayerController* PC = GetViewController(World, E); if (!PC) return nullptr;
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;

		// 대상은 world{x,y,z}(m) 또는 {target, id}(보드 #1102). target 이면 distance 를 생략해도 대상 크기에 맞춰 물러난다.
		FVector TargetWorld;
		double AutoDistM = 0.0;
		if (RpcParam::Has(P, TEXT("target")))
		{
			double RadiusCm = 0.0;
			if (!ResolveViewTarget(World, Mgr, P, TargetWorld, RadiusCm, E)) return nullptr;
			AutoDistM = FMath::Max(8.0, 3.0 * RadiusCm / Mgr->MetersToUU);
		}
		else
		{
			FVector TargetMeters;
			if (!RpcParam::RequireVec3(P, TEXT("world"), TargetMeters, E)) return nullptr;
			TargetWorld = UCameraControlLibrary::UnrealMetersToWorld(
				FCamVec3{ static_cast<float>(TargetMeters.X), static_cast<float>(TargetMeters.Y), static_cast<float>(TargetMeters.Z) },
				Mgr->MetersToUU);
		}

		FVector CurLoc = FVector::ZeroVector;
		FRotator CurRot = FRotator::ZeroRotator;
		PC->GetPlayerViewPoint(CurLoc, CurRot);

		FVector Dir = TargetWorld - CurLoc;
		if (!Dir.Normalize())
		{
			E.FailDomain(TEXT("world 가 현재 시점 위치와 같아 바라볼 방향을 정할 수 없습니다."));
			return nullptr;
		}

		// distance 를 주면 방향은 유지한 채 타겟에서 그만큼 물러난 자리로 옮긴다(= 그 지점을 그 거리에서 본다).
		// 생략하면 자리는 그대로 두고 방향만 돌린다.
		if (RpcParam::Has(P, TEXT("distance")) || AutoDistM > 0.0)
		{
			const double DistMeters = RpcParam::GetFloat(P, TEXT("distance"), AutoDistM);
			if (DistMeters <= 0.0)
			{
				E.FailDomain(FString::Printf(TEXT("distance 는 0 보다 커야 합니다(미터): %.3f"), DistMeters));
				return nullptr;
			}
			if (!ApplyViewLocation(PC, TargetWorld - Dir * (DistMeters * Mgr->MetersToUU), E)) return nullptr;
		}

		// pitch 는 ±89 로 자른다(view.set 과 같은 이유 — 짐벌 뒤집힘 방지). 수직 바로 위/아래는 표현하지 않는다.
		FRotator NewRot = Dir.Rotation();
		NewRot.Pitch = FMath::Clamp(FRotator::NormalizeAxis(NewRot.Pitch), -89.0, 89.0);
		NewRot.Roll = 0.f;
		ApplyViewRotation(PC, NewRot);

		return BuildViewState(World, PC, Mgr->MetersToUU);
	});

	// view.topDown {target?, id?, bounds?{min{x,y},max{x,y}}, height?} — 위에서 내려다본다(보드 #1102).
	// 대상도 bounds 도 없으면 바닥 번호가 있는 면 전부를 담는다. height(m) 를 안 주면 수평 화각에 맞춰 정한다.
	Dispatcher.Register(TEXT("view.topDown"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* World = GetWorldPtr();
		APlayerController* PC = GetViewController(World, E); if (!PC) return nullptr;
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;
		const double U = Mgr->MetersToUU;

		FVector Center;
		double HalfCm = 0.0;
		if (RpcParam::Has(P, TEXT("target")))
		{
			if (!ResolveViewTarget(World, Mgr, P, Center, HalfCm, E)) return nullptr;
		}
		else if (RpcParam::Has(P, TEXT("bounds")))
		{
			const TSharedPtr<FJsonObject>* B = nullptr;
			RpcParam::MarkRead(P, TEXT("bounds"));
			if (!P->TryGetObjectField(TEXT("bounds"), B) || !B || !B->IsValid())
			{
				E.FailDomain(TEXT("bounds 는 {min{x,y}, max{x,y}} (m) 여야 합니다"), ERpcErrorKind::BadParams);
				return nullptr;
			}
			const FVector Mn = RpcParam::GetVec3(*B, TEXT("min"));
			const FVector Mx = RpcParam::GetVec3(*B, TEXT("max"));
			Center = FVector((Mn.X + Mx.X) * 0.5 * U, (Mn.Y + Mx.Y) * 0.5 * U, 0.0);
			HalfCm = 0.5 * FMath::Max(FMath::Abs(Mx.X - Mn.X), FMath::Abs(Mx.Y - Mn.Y)) * U;
		}
		else
		{
			AParkingPresetManager* PresetMgr = Cast<AParkingPresetManager>(
				UGameplayStatics::GetActorOfClass(World, AParkingPresetManager::StaticClass()));
			TArray<FParkingSlotNumberInfo> Slots;
			if (PresetMgr) { PresetMgr->CollectSlotNumbers(PresetMgr->ResolvePresets(), Slots); }
			FBox Box(ForceInit);
			for (const FParkingSlotNumberInfo& S : Slots) { Box += S.Center; }
			if (!Box.IsValid)
			{
				E.FailDomain(TEXT("내려다볼 주차면이 없습니다 — target 또는 bounds 를 주세요"), ERpcErrorKind::NotFound);
				return nullptr;
			}
			Center = Box.GetCenter();
			HalfCm = FMath::Max(Box.GetExtent().X, Box.GetExtent().Y) + 300.0;
		}

		double HeightM = RpcParam::GetFloat(P, TEXT("height"), 0.0);
		if (HeightM <= 0.0)
		{
			const double Fov = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetFOVAngle() : 90.0;
			const double TanH = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(Fov, 10.0, 170.0) * 0.5));
			// 화면 세로(가로×9/16)에도 들어오게 1.8 배 여유.
			HeightM = FMath::Clamp(HalfCm * 1.8 / TanH / U, 5.0, 400.0);
		}
		if (!ApplyViewLocation(PC, FVector(Center.X, Center.Y, Center.Z + HeightM * U), E)) return nullptr;
		FVector CurLoc; FRotator CurRot;
		PC->GetPlayerViewPoint(CurLoc, CurRot);
		ApplyViewRotation(PC, FRotator(-89.0, CurRot.Yaw, 0.0));   // view.set 과 같은 ±89 한계
		return BuildViewState(World, PC, U);
	});
}
