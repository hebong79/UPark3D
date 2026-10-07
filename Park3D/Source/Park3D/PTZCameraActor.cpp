// Copyright Epic Games, Inc. All Rights Reserved.

#include "PTZCameraActor.h"
#include "CameraControlLibrary.h"
#include "CamLensDistortion.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/World.h"
#include "TextureResource.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

APTZCameraActor::APTZCameraActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// 캡처 컴포넌트: 선택 카메라만 매 프레임 캡처(설계 §12-B). FOVAngle 은 수평 화각(설계 §7.3).
	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
	Capture->SetupAttachment(Root);
	Capture->bCaptureEveryFrame = false;   // 늘 false — 뷰어가 보일 때만 CaptureForViewer 가 프레임당 1장
	Capture->bCaptureOnMovement = false;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;  // 톤매핑된 색(UI 프리뷰용)
	// 비선택 카메라도 FSceneViewState 를 유지한다. 이게 없으면 bCaptureEveryFrame=false 인 캡처마다
	// 엔진(USceneCaptureComponent::GetViewState)이 뷰 스테이트를 파괴해 Lumen GI/리플렉션/TSR
	// 히스토리가 사라진다 — /stream 만 간접광이 빠져 어둡고 갈색으로 보이던 원인.
	Capture->bAlwaysPersistRenderingState = true;
	Capture->FOVAngle = DefaultHFov;       // zoom=1 기준(설계 §7.3)

	// 폴대: 바닥 Z=0 시각 표시. 높이(Z)·팬틸트(회전)를 상속하지 않도록 절대 위치/회전 사용.
	PoleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PoleMesh"));
	PoleMesh->SetupAttachment(Root);
	PoleMesh->SetUsingAbsoluteLocation(true);   // Root 높이(Z) 비상속 → 바닥 고정
	PoleMesh->SetUsingAbsoluteRotation(true);   // Capture 팬틸트 비상속 → 수직 유지
	// 기본은 숨김 — 표시 스위치(ACameraControlManager::ShowAllPoles / RPC cam.setMarks)가 켤 때만 보인다.
	// 기본을 '보임'으로 두면 스위치가 꺼져 있는데도 새로 스폰된 카메라 밑에 기둥이 선다(보드 #940).
	// 콜리전은 SetPoleVisible 과 같은 규약으로 맞춘다(숨김=off, 표시=Query 전용 — 폴대 클릭 선택 TracePole 용, §12-H).
	PoleMesh->SetVisibility(false);
	PoleMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PoleMesh->ComponentTags.AddUnique(PoleTag);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylFinder(TEXT("/Engine/BasicShapes/Cylinder"));
	if (CylFinder.Succeeded())
	{
		PoleMesh->SetStaticMesh(CylFinder.Object);
	}
	// 얇은 기둥(가로 0.1, 세로 1.0 = 기본 실린더 100uu 높이).
	PoleMesh->SetRelativeScale3D(FVector(0.1f, 0.1f, 1.f));
}

void APTZCameraActor::InitRenderTarget(int32 W, int32 H)
{
	if (!RenderTarget)
	{
		RenderTarget = NewObject<UTextureRenderTarget2D>(this);
	}
	// RGBA8 ~3.5MB/1대(설계 §12-B). sRGB 타깃이어야 리드백 바이트가 "화면에 보이는 값"이 된다 —
	// RTF_RGBA8(선형)로 두면 엔진이 화면에 그릴 때만 sRGB 로 바꿔 주고, ReadPixels 로 꺼낸 바이트는
	// 선형 그대로라 JPEG 이 화면보다 어둡고 진하게 나온다(실측 평균 106.8 → 62.1).
	RenderTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8_SRGB;
	RenderTarget->ClearColor = FLinearColor::Black;
	RenderTarget->bAutoGenerateMips = false;
	RenderTarget->InitAutoFormat(W, H);
	RenderTarget->UpdateResourceImmediate(true);

	if (Capture)
	{
		Capture->TextureTarget = RenderTarget;
	}
}

void APTZCameraActor::SetCameraWorldLocation(float X_ue, float Y_ue, float HeightZ_ue)
{
	if (Root)
	{
		Root->SetWorldLocation(FVector(X_ue, Y_ue, HeightZ_ue));
	}
	UpdatePolePosition();
}

void APTZCameraActor::SetPanTilt(float Pan, float Tilt)
{
	// 회전은 Capture 에만 적용(Root 는 미회전 → 폴대 수직 유지). Root 미회전이므로 relative==world.
	if (Capture)
	{
		Capture->SetRelativeRotation(UCameraControlLibrary::PanTiltToRotator(Pan, Tilt));
	}
}

void APTZCameraActor::SetZoom(float Zoom)
{
	// UE FOVAngle 은 이미 수평 화각 → 라이브러리 값 직접 대입(aspect 변환 불필요, 설계 §7.3).
	if (Capture)
	{
		Capture->FOVAngle = UCameraControlLibrary::ZoomToHFov(Zoom, MaxZoom, DefaultHFov);
	}
}

float APTZCameraActor::GetZoom() const
{
	return Capture ? UCameraControlLibrary::HFovToZoom(Capture->FOVAngle, MaxZoom, DefaultHFov) : 1.f;
}

void APTZCameraActor::GetPanTilt(float& OutPan, float& OutTilt) const
{
	// SetPanTilt 이 Capture 의 relative 회전에만 값을 넣으므로 같은 회전을 되읽는다(Root 미회전).
	if (Capture)
	{
		UCameraControlLibrary::RotatorToPanTilt(Capture->GetRelativeRotation(), OutPan, OutTilt);
	}
	else
	{
		OutPan = 0.f;
		OutTilt = 0.f;
	}
}

void APTZCameraActor::UpdatePolePosition()
{
	if (Root && PoleMesh)
	{
		const FVector R = Root->GetComponentLocation();
		// 기본 실린더는 원점 중심(±50uu) → 밑면이 바닥(Z=0)에 닿도록 절반 높이만큼 위로 올림.
		const double HalfHeight = 50.0 * PoleMesh->GetRelativeScale3D().Z;
		PoleMesh->SetWorldLocation(FVector(R.X, R.Y, HalfHeight));
		PoleMesh->SetWorldRotation(FRotator::ZeroRotator);
	}
}

void APTZCameraActor::SetCaptureEnabled(bool bEnabled)
{
	bViewerTarget = bEnabled;
	if (Capture)
	{
		Capture->bCaptureEveryFrame = false;   // 뷰어 갱신은 CaptureForViewer 가 맡는다(헤더 주석)
	}
}

void APTZCameraActor::CaptureOnce()
{
	if (Capture)
	{
		CaptureInto(RenderTarget);
		LastCaptureFrame = GFrameCounter;
	}
}

void APTZCameraActor::CaptureInto(UTextureRenderTarget2D* Out)
{
	if (!Capture)
	{
		return;
	}
	// CaptureScene 은 호출 시점의 타깃·화각으로 렌더 명령을 넣으므로 바로 되돌려도 다음 캡처에 섞이지 않는다.
	UTextureRenderTarget2D* const PrevTarget = Capture->TextureTarget;
	UWorld* World = GetWorld();
	if (!Out || CamLens::IsPinhole(LensK1, LensK2) || !World)
	{
		if (Out) { Capture->TextureTarget = Out; }
		Capture->CaptureScene();
		Capture->TextureTarget = PrevTarget;
		return;
	}

	// ---- 왜곡 렌더(보드 #1297) ----
	// 출력 픽셀 격자의 각 꼭짓점을 왜곡 후 탄젠트 좌표로 보고 역변환해 "그 픽셀이 보는 방향"을 얻는다.
	// 바깥으로 휘는(barrel) 렌즈는 모서리가 원래 화각 밖을 보므로, 그 방향까지 담는 넓은 화각으로 중간 타깃에 찍은 뒤
	// 삼각형 격자(UV = 방향)로 출력 타깃에 그린다. 중심 배율이 같도록 중간 타깃 해상도를 화각 비만큼 키운다.
	const int32 W = Out->SizeX, H = Out->SizeY;
	const double T = FMath::Tan(FMath::DegreesToRadians(static_cast<double>(Capture->FOVAngle) * 0.5));
	const double F = (W * 0.5) / T;
	constexpr int32 GX = 48, GY = 27;
	TArray<FVector2D> Dir;
	Dir.SetNumUninitialized((GX + 1) * (GY + 1));
	double MaxX = T, MaxY = T * H / W;
	for (int32 gy = 0; gy <= GY; ++gy)
	{
		for (int32 gx = 0; gx <= GX; ++gx)
		{
			const FVector2D Xd((W * gx / double(GX) - W * 0.5) / F, (H * gy / double(GY) - H * 0.5) / F);
			const FVector2D U = CamLens::UndistortTan(Xd, LensK1, LensK2);
			Dir[gy * (GX + 1) + gx] = U;
			MaxX = FMath::Max(MaxX, FMath::Abs(U.X));
			MaxY = FMath::Max(MaxY, FMath::Abs(U.Y));
		}
	}
	// 1% 여유는 가장자리 쌍선형 샘플이 타깃 밖을 집지 않게. 85° 반각이 상한(그 이상은 투영이 무너진다).
	const double TW = FMath::Min(FMath::Max(MaxX, MaxY * W / H) * 1.01, FMath::Tan(FMath::DegreesToRadians(85.0)));
	const int32 Ww = FMath::Clamp(FMath::CeilToInt32(W * TW / T), W, 4096);
	const int32 Hw = FMath::Max(1, FMath::RoundToInt32(static_cast<double>(Ww) * H / W));

	if (!WideCaptureTarget)
	{
		WideCaptureTarget = NewObject<UTextureRenderTarget2D>(this);
		WideCaptureTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8_SRGB; // InitRenderTarget 과 같은 감마 규약
		WideCaptureTarget->ClearColor = FLinearColor::Black;
		WideCaptureTarget->bAutoGenerateMips = false;
		WideCaptureTarget->Filter = TF_Bilinear;
		WideCaptureTarget->AddressX = TA_Clamp;
		WideCaptureTarget->AddressY = TA_Clamp;
	}
	if (WideCaptureTarget->SizeX != Ww || WideCaptureTarget->SizeY != Hw)
	{
		WideCaptureTarget->InitAutoFormat(Ww, Hw);
		WideCaptureTarget->UpdateResourceImmediate(true);
	}

	const float PrevFov = Capture->FOVAngle;
	Capture->FOVAngle = static_cast<float>(2.0 * FMath::RadiansToDegrees(FMath::Atan(TW)));
	Capture->TextureTarget = WideCaptureTarget;
	Capture->CaptureScene();
	Capture->FOVAngle = PrevFov;           // GetZoom·화면 투영은 원래 화각을 본다
	Capture->TextureTarget = PrevTarget;

	// 넓은 타깃의 수평·수직 반각 탄젠트는 TW, TW·Hw/Ww → UV = 0.5 + 방향/(2·반각탄젠트).
	const double TWy = TW * Hw / Ww;
	auto UvOf = [&](const FVector2D& U) { return FVector2D(0.5 + U.X / (2.0 * TW), 0.5 + U.Y / (2.0 * TWy)); };
	TArray<FCanvasUVTri> Tris;
	Tris.Reserve(GX * GY * 2);
	for (int32 gy = 0; gy < GY; ++gy)
	{
		for (int32 gx = 0; gx < GX; ++gx)
		{
			const int32 I00 = gy * (GX + 1) + gx, I10 = I00 + 1, I01 = I00 + GX + 1, I11 = I01 + 1;
			const FVector2D P00(W * gx / double(GX), H * gy / double(GY));
			const FVector2D P11(W * (gx + 1) / double(GX), H * (gy + 1) / double(GY));
			const FVector2D P10(P11.X, P00.Y), P01(P00.X, P11.Y);
			// FCanvasUVTri 의 꼭짓점 색 기본값은 (0,0,0,0) 이고 텍스처에 곱해진다 → 흰색을 넣지 않으면 그림이 통째로 검다.
			FCanvasUVTri A;
			A.V0_Color = A.V1_Color = A.V2_Color = FLinearColor::White;
			A.V0_Pos = P00; A.V0_UV = UvOf(Dir[I00]);
			A.V1_Pos = P10; A.V1_UV = UvOf(Dir[I10]);
			A.V2_Pos = P11; A.V2_UV = UvOf(Dir[I11]);
			FCanvasUVTri B;
			B.V0_Color = B.V1_Color = B.V2_Color = FLinearColor::White;
			B.V0_Pos = P00; B.V0_UV = UvOf(Dir[I00]);
			B.V1_Pos = P11; B.V1_UV = UvOf(Dir[I11]);
			B.V2_Pos = P01; B.V2_UV = UvOf(Dir[I01]);
			Tris.Add(A);
			Tris.Add(B);
		}
	}

	// 캔버스는 타깃의 표시 감마(기본 2.2)로 한 번 더 인코딩한다 — sRGB 타깃은 하드웨어가 인코딩하므로 1 로 둔다.
	Out->TargetGamma = 1.f;
	FCanvas Canvas(Out->GameThread_GetRenderTargetResource(), nullptr, World, World->GetFeatureLevel());
	FCanvasTriangleItem Item(Tris, WideCaptureTarget->GetResource());
	Item.BlendMode = SE_BLEND_Opaque;
	Canvas.DrawItem(Item);
	Canvas.Flush_GameThread();
}

void APTZCameraActor::CaptureForViewer()
{
	if (LastCaptureFrame != GFrameCounter)
	{
		CaptureOnce();
	}
}

UTextureRenderTarget2D* APTZCameraActor::CaptureAtSize(int32 W, int32 H)
{
	if (!Capture || !RenderTarget)
	{
		return nullptr;
	}
	if (W == RenderTarget->SizeX && H == RenderTarget->SizeY)
	{
		CaptureOnce();
		return RenderTarget;
	}

	if (!SizedCaptureTarget)
	{
		SizedCaptureTarget = NewObject<UTextureRenderTarget2D>(this);
		SizedCaptureTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8_SRGB; // InitRenderTarget 과 같은 감마 규약
		SizedCaptureTarget->ClearColor = FLinearColor::Black;
		SizedCaptureTarget->bAutoGenerateMips = false;
	}
	if (SizedCaptureTarget->SizeX != W || SizedCaptureTarget->SizeY != H)
	{
		SizedCaptureTarget->InitAutoFormat(W, H);
		SizedCaptureTarget->UpdateResourceImmediate(true);
	}

	// CaptureInto 가 타깃을 바꿔 찍고 RenderTarget 으로 되돌린다 — 스트림에 섞이지 않는다.
	CaptureInto(SizedCaptureTarget);
	return SizedCaptureTarget;
}

void APTZCameraActor::SetPoleVisible(bool bVisible)
{
	if (PoleMesh)
	{
		PoleMesh->SetVisibility(bVisible);
		// §12-H: 숨김 시 콜리전 off(바닥 트레이스 오탐 방지), 표시 시 Query 전용 복원.
		PoleMesh->SetCollisionEnabled(bVisible ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	}
}

void APTZCameraActor::SetPoleHighlight(bool bOn)
{
	if (PoleMesh)
	{
		// CarActor::SetSelected 오버레이 관례 재사용. 머티리얼 미지정 시 무해(no-op).
		PoleMesh->SetOverlayMaterial(bOn ? PoleHighlightMaterial : nullptr);
	}
}
