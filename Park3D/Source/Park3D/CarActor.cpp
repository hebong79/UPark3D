// Copyright Epic Games, Inc. All Rights Reserved.

#include "CarActor.h"
#include "CarColorComponent.h"
#include "CarPlacementLibrary.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Font.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Crc.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	FString MakePlateSeed(const FCarPos& Pos)
	{
		if (!Pos.id.IsEmpty())
		{
			return Pos.id;
		}

		// 비정상 legacy 데이터의 빈 id도 입력 데이터가 같으면 같은 번호가 되도록 고정한다.
		return FString::Printf(TEXT("%d|%d|%d|%d|%.3f|%.3f|%.3f|%.3f|%d"),
			Pos.type, Pos.presetId, Pos.slotId, Pos.prefabId,
			Pos.pos.x, Pos.pos.y, Pos.pos.z, Pos.rotY, Pos.isFront ? 1 : 0);
	}

	void ConfigurePlateVisual(UPrimitiveComponent* Component)
	{
		if (!Component)
		{
			return;
		}
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetGenerateOverlapEvents(false);
		Component->SetCastShadow(false);
		Component->SetVisibility(false, true);
	}
}

ACarActor::ACarActor()
{
	PrimaryActorTick.bCanEverTick = false;

	MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
	SetRootComponent(MeshComp);
	// 차량은 픽(라인트레이스) 대상이지만 물리 충돌은 불필요 → Query 전용.
	MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	MeshComp->SetCustomDepthStencilValue(1);

	FrontPlateComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FrontPlateComp"));
	FrontPlateComp->SetupAttachment(MeshComp);
	BackPlateComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BackPlateComp"));
	BackPlateComp->SetupAttachment(MeshComp);
	ConfigurePlateVisual(FrontPlateComp);
	ConfigurePlateVisual(BackPlateComp);

	FrontPlateFrameComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FrontPlateFrameComp"));
	FrontPlateFrameComp->SetupAttachment(FrontPlateComp);
	BackPlateFrameComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BackPlateFrameComp"));
	BackPlateFrameComp->SetupAttachment(BackPlateComp);
	FrontPlateSecurityStripComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FrontPlateSecurityStripComp"));
	FrontPlateSecurityStripComp->SetupAttachment(FrontPlateComp);
	BackPlateSecurityStripComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BackPlateSecurityStripComp"));
	BackPlateSecurityStripComp->SetupAttachment(BackPlateComp);
	for (UStaticMeshComponent* PlateDetail : { FrontPlateFrameComp, BackPlateFrameComp, FrontPlateSecurityStripComp, BackPlateSecurityStripComp })
	{
		ConfigurePlateVisual(PlateDetail);
	}

	// Content body는 52×11cm(520×110mm 비율)다. backing은 외곽 1cm, strip은 좌측 4×10cm다.
	for (UStaticMeshComponent* PlateFrame : { FrontPlateFrameComp, BackPlateFrameComp })
	{
		PlateFrame->SetRelativeLocation(FVector(0.f, -1.7f, 0.f)); // body 뒤라 text를 가리지 않고 외곽만 보인다.
		PlateFrame->SetRelativeScale3D(FVector(0.54f, 0.004f, 0.13f));
	}
	for (UStaticMeshComponent* SecurityStrip : { FrontPlateSecurityStripComp, BackPlateSecurityStripComp })
	{
		SecurityStrip->SetRelativeLocation(FVector(-22.f, 1.35f, 0.f)); // body local +Y 외측, text 영역의 좌측.
		SecurityStrip->SetRelativeScale3D(FVector(0.04f, 0.004f, 0.10f));
	}

	FrontPlateText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("FrontPlateText"));
	FrontPlateText->SetupAttachment(FrontPlateComp);
	BackPlateText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("BackPlateText"));
	BackPlateText->SetupAttachment(BackPlateComp);
	ConfigurePlateVisual(FrontPlateText);
	ConfigurePlateVisual(BackPlateText);
	for (UTextRenderComponent* PlateText : { FrontPlateText, BackPlateText })
	{
		PlateText->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
		PlateText->SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextCenter);
		PlateText->SetTextRenderColor(FColor::Black);
		PlateText->SetWorldSize(10.8f); // Loop 14 9.0cm에서 사용자 요청 1.2배 확대.
		PlateText->SetXScale(0.80f); // 123 다 4567을 blue field 제외 text field에 맞춘다.
		PlateText->SetYScale(1.f);
		// TextRender source mesh는 local YZ plane(normal +X), horizontal -Y, vertical -Z다.
		// yaw +90으로 normal을 plate local +Y로, horizontal을 plate +X로 맞춘다.
		// front parent의 yaw 180이 normal을 world -Y(전면 외측)로, rear는 world +Y(후면 외측)로 만든다.
		PlateText->SetRelativeLocation(FVector(4.f, 1.55f, 0.f)); // actual blue KOR field 뒤 remaining text field의 중심.
		PlateText->SetRelativeRotation(FRotator(0.f, 90.f, 0.f));
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> FrontPlateFinder(TEXT("/Game/Actors/Car/Plates/Meshs/SM_Plate_F"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> BackPlateFinder(TEXT("/Game/Actors/Car/Plates/Meshs/SM_Plate_B"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlateDetailCubeFinder(TEXT("/Engine/BasicShapes/Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> WhitePlateBaseMatFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial"));
	static ConstructorHelpers::FObjectFinder<UFont> PlateFontFinder(TEXT("/Game/Actors/Car/Plates/Font/수성돋움체"));
	// 프로젝트 사본이던 DefaultTextMaterialTranslucent1 이 사라져 엔진 원본을 쓴다(내용 동일).
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> PlateTextMatFinder(TEXT("/Engine/EngineMaterials/DefaultTextMaterialTranslucent"));
	if (FrontPlateFinder.Succeeded()) FrontPlateComp->SetStaticMesh(FrontPlateFinder.Object);
	if (BackPlateFinder.Succeeded()) BackPlateComp->SetStaticMesh(BackPlateFinder.Object);
	// 기존 Content UV/material(M_Plate+M_Num)의 white body, rounded bezel, blue KOR field를 그대로 사용한다.
	// 이전 loop의 per-component white MID override는 제거한다.
	FrontPlateComp->SetMaterial(0, nullptr);
	FrontPlateComp->SetMaterial(1, nullptr);
	BackPlateComp->SetMaterial(0, nullptr);
	BackPlateComp->SetMaterial(1, nullptr);
	if (PlateDetailCubeFinder.Succeeded())
	{
		for (UStaticMeshComponent* PlateDetail : { FrontPlateFrameComp, BackPlateFrameComp, FrontPlateSecurityStripComp, BackPlateSecurityStripComp })
		{
			PlateDetail->SetStaticMesh(PlateDetailCubeFinder.Object);
		}
	}
	if (WhitePlateBaseMatFinder.Succeeded())
	{
		UMaterialInterface* const WhitePlateBaseMaterial = WhitePlateBaseMatFinder.Object;
		// Detail Cube MIDs are retained for inherited Blueprint compatibility only. The Content background owns the visible bezel/KOR field.
		auto ApplyColoredPlateDetail = [WhitePlateBaseMaterial](UStaticMeshComponent* PlateDetail, const FLinearColor& Color)
		{
			if (PlateDetail)
			{
				if (UMaterialInstanceDynamic* DetailMID = UMaterialInstanceDynamic::Create(WhitePlateBaseMaterial, PlateDetail))
				{
					DetailMID->SetVectorParameterValue(TEXT("Color"), Color);
					PlateDetail->SetMaterial(0, DetailMID);
				}
			}
		};
		for (UStaticMeshComponent* PlateFrame : { FrontPlateFrameComp, BackPlateFrameComp })
		{
			ApplyColoredPlateDetail(PlateFrame, FLinearColor::Black);
		}
		const FLinearColor KoreaSecurityBlue(0.02f, 0.18f, 0.78f, 1.f);
		for (UStaticMeshComponent* SecurityStrip : { FrontPlateSecurityStripComp, BackPlateSecurityStripComp })
		{
			ApplyColoredPlateDetail(SecurityStrip, KoreaSecurityBlue);
		}
	}
	for (UTextRenderComponent* PlateText : { FrontPlateText, BackPlateText })
	{
		if (PlateFontFinder.Succeeded()) PlateText->SetFont(PlateFontFinder.Object);
		if (PlateTextMatFinder.Succeeded()) PlateText->SetTextMaterial(PlateTextMatFinder.Object);
	}

	ColorComp = CreateDefaultSubobject<UCarColorComponent>(TEXT("ColorComp"));

	// 선택 하이라이트 오버레이 머티리얼 기본값.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SelMatFinder(TEXT("/Game/UI/M_CarSelect"));
	if (SelMatFinder.Succeeded())
	{
		SelectionOverlayMaterial = SelMatFinder.Object;
	}
}

void ACarActor::InitFromPos(const FCarPos& Pos, UStaticMesh* InMesh, float MetersToUU)
{
	CarData = Pos;
	if (InMesh && MeshComp)
	{
		MeshComp->SetStaticMesh(InMesh);
	}
	InitializePlateNumberOnce();
	ApplyTransformFromData(MetersToUU);
	UpdatePlatePresentation();

	// 도색: color >= 0 이면 ECarColor 프리셋 적용, -1(기본)이면 원본색 유지.
	if (ColorComp && CarData.color >= 0)
	{
		ColorComp->SetColorByEnum((ECarColor)CarData.color);
	}
}

FString ACarActor::MakeDeterministicPlateNumber(const FCarPos& Pos)
{
	const uint32 Hash = FCrc::StrCrc32(*MakePlateSeed(Pos));
	// 일반 비사업용 승용차: 세 자리(100~699) + 일반용 한글 + 네 자리.
	// rental 하/허/호 및 commercial 계열은 의도적으로 이 pool에 넣지 않는다.
	static const FString AllowedPassengerChars = TEXT("가나다라마바사거너더러머버서어저고노도로모보소오조구누두루무부수우주");
	const uint32 Prefix = 100u + (Hash % 600u);
	const TCHAR UsageChar = AllowedPassengerChars[(Hash / 601u) % static_cast<uint32>(AllowedPassengerChars.Len())];
	const uint32 Serial = (Hash / 997u) % 10000u;
	return FString::Printf(TEXT("%03u%c%04u"), Prefix, UsageChar, Serial);
}

void ACarActor::InitializePlateNumberOnce()
{
	if (PlateNumber.IsEmpty())
	{
		PlateNumber = MakeDeterministicPlateNumber(CarData);
	}

	const FText Text = FText::FromString(MakePlateDisplayText(PlateNumber));
	if (FrontPlateText) FrontPlateText->SetText(Text);
	if (BackPlateText) BackPlateText->SetText(Text);
}

FString ACarActor::MakePlateDisplayText(const FString& CanonicalNumber)
{
	return CanonicalNumber.Len() == 8
		? CanonicalNumber.Left(3) + TEXT(" ") + CanonicalNumber.Mid(3, 1) + TEXT(" ") + CanonicalNumber.Right(4)
		: CanonicalNumber;
}

void ACarActor::AlignPlateAndText(UStaticMeshComponent* Plate, UTextRenderComponent* Text, bool bFront)
{
	if (!Plate || !Text || !Plate->GetStaticMesh())
	{
		return;
	}

	// 메시 바운즈에서 축을 읽는다: 가장 얇은 축 = 판의 면 법선, 가장 긴 축 = 글자가 흐르는 방향.
	// 원점(BoundsOrigin)도 함께 쓴다 — 메시가 원점 기준으로 치우쳐 있으면 두께만으로 띄운 글자가 판 속에 박힌다.
	const FVector BoundsOrigin = Plate->GetStaticMesh()->GetBounds().Origin;
	const FVector Ext = Plate->GetStaticMesh()->GetBounds().BoxExtent;
	const int32 ThinAxis = (Ext.X <= Ext.Y && Ext.X <= Ext.Z) ? 0 : ((Ext.Y <= Ext.Z) ? 1 : 2);
	const int32 WideAxis = (Ext.X >= Ext.Y && Ext.X >= Ext.Z) ? 0 : ((Ext.Y >= Ext.Z) ? 1 : 2);
	const double ThinExtent = Ext[ThinAxis];

	// 판을 돌려 얇은 축을 차량 바깥(전면 -Y / 후면 +Y)으로, 긴 축을 차량 좌우(X)로 보낸다.
	const FVector Outward = bFront ? FVector(0.f, -1.f, 0.f) : FVector(0.f, 1.f, 0.f);
	FVector Axes[3] = { FVector::XAxisVector, FVector::YAxisVector, FVector::ZAxisVector };
	FVector MeshNormal = Axes[ThinAxis];
	FVector MeshWide = Axes[WideAxis];
	// 메시 축 → 원하는 축으로 보내는 회전(축이 축에 대응하므로 XZ 두 축만 지정하면 결정된다).
	const FMatrix Basis = FRotationMatrix::MakeFromXZ(Outward, FVector::ZAxisVector)
		* FRotationMatrix::MakeFromXZ(MeshNormal, MeshWide ^ MeshNormal).Inverse();
	Plate->SetRelativeRotation(Basis.Rotator());

	// 글자는 판의 자식이다. 판 로컬 기준으로 면 바로 바깥에 두고, TextRender 의 기준축(법선 +X)을
	// 판의 법선에 맞춘다. 글자가 흐르는 방향(-Y)은 판의 긴 축과 나란해진다.
	// 어느 쪽이 "바깥"인가는 메시가 정한다(SM_Plate_F/B 는 보이는 면이 local -Y 쪽이다).
	// 판이 차체에 붙는 방향에서 역산한다: 판을 차량 바깥으로 향하게 돌려 놓았으므로,
	// 차량 바깥 방향(전면 -Y / 후면 +Y)을 판 로컬로 되돌리면 그것이 글자를 놓을 쪽이다.
	const FVector OutwardLocal = Plate->GetRelativeRotation().UnrotateVector(Outward).GetSafeNormal();
	const FVector TextLocal = BoundsOrigin + OutwardLocal * (ThinExtent + PlateTextSurfaceGap) + MeshWide * PlateTextSideShift;
	Text->SetRelativeLocation(TextLocal);
	Text->SetRelativeRotation(FRotationMatrix::MakeFromXZ(OutwardLocal, MeshWide ^ OutwardLocal).Rotator());

	// 판·글자가 어디에 어떤 자세로 놓였는지 한 줄로 남긴다. "로그에는 vis=1 인데 화면에 없다"를
	// 다시 만나면 이 값(법선·간격·월드 위치)이 원인을 바로 가리킨다.
	UE_LOG(LogTemp, Display,
		TEXT("[CarPlate] %s 정렬: 메시축(얇=%d 넓=%d) 두께=%.2f 원점=%s → 글자 local=%s world=%s"),
		bFront ? TEXT("Front") : TEXT("Back"), ThinAxis, WideAxis, ThinExtent,
		*BoundsOrigin.ToString(), *TextLocal.ToString(), *Text->GetComponentLocation().ToString());
}

void ACarActor::UpdatePlatePresentation()
{
	const bool bHasVehicleMesh = MeshComp && MeshComp->GetStaticMesh();
	for (UPrimitiveComponent* Component : { static_cast<UPrimitiveComponent*>(FrontPlateComp), static_cast<UPrimitiveComponent*>(BackPlateComp),
		static_cast<UPrimitiveComponent*>(FrontPlateText), static_cast<UPrimitiveComponent*>(BackPlateText) })
	{
		if (Component)
		{
			Component->SetVisibility(bHasVehicleMesh, true);
		}
	}
	// Existing Content's M_Num texture already supplies the inset bezel and left blue KOR/security field.
	// Do not double-render the old runtime primitive approximation over it.
	for (UPrimitiveComponent* Detail : { static_cast<UPrimitiveComponent*>(FrontPlateFrameComp), static_cast<UPrimitiveComponent*>(BackPlateFrameComp),
		static_cast<UPrimitiveComponent*>(FrontPlateSecurityStripComp), static_cast<UPrimitiveComponent*>(BackPlateSecurityStripComp) })
	{
		if (Detail)
		{
			Detail->SetVisibility(false, true);
		}
	}
	if (!bHasVehicleMesh || !FrontPlateComp || !BackPlateComp)
	{
		return;
	}

	FVector Min = FVector::ZeroVector;
	FVector Max = FVector::ZeroVector;
	MeshComp->GetLocalBounds(Min, Max);
	const FVector Origin = (Min + Max) * 0.5f;
	const FVector Extent = (Max - Min) * 0.5f;
	const float MountZ = Origin.Z - Extent.Z * 0.4f;
	const float MountY = Extent.Y + 1.f;
	FrontPlateComp->SetRelativeLocation(FVector(Origin.X, Origin.Y - MountY, MountZ));
	BackPlateComp->SetRelativeLocation(FVector(Origin.X, Origin.Y + MountY, MountZ));

	// 판의 회전과 글자 위치는 **메시의 실제 축**에서 구한다. 예전 에셋은 면 법선이 local +Y 였고 그 값이
	// 코드에 박혀 있었는데, 2026-08-16 콘텐츠 교체로 들어온 SM_Plate_F/B 는 얇은 축이 local X 다.
	// 그래서 글자가 판과 나란히(모서리 방향으로) 서서 화면에서 사라졌다 — 로그에는 vis=1 로 남는다.
	AlignPlateAndText(FrontPlateComp, FrontPlateText, /*bFront=*/true);
	AlignPlateAndText(BackPlateComp, BackPlateText, /*bFront=*/false);

	// PIE 실측 근거: component 생성/가시성만이 아니라 실제 transform/bounds/font/material을 남긴다.
	auto LogPlateState = [this](const TCHAR* Side, const UStaticMeshComponent* Plate, const UTextRenderComponent* Text)
	{
		if (!Plate || !Text)
		{
			UE_LOG(LogTemp, Warning, TEXT("[CarPlate] %s id=%s component missing plate=%d text=%d"),
				Side, *CarData.id, Plate != nullptr, Text != nullptr);
			return;
		}
		UE_LOG(LogTemp, Display,
		TEXT("[CarPlate] %s id=%s number=%s carMesh=%s plateMesh=%s plateMat0=%s plateMat1=%s plate(reg=%d vis=%d hidden=%d rel=%s world=%s boundsO=%s boundsE=%s) text(reg=%d vis=%d hidden=%d text=%s font=%s mat=%s color=%s rel=%s world=%s boundsO=%s boundsE=%s) carBoundsO=%s carBoundsE=%s"),
			Side, *CarData.id, *PlateNumber,
			*GetPathNameSafe(MeshComp ? MeshComp->GetStaticMesh() : nullptr),
			*GetPathNameSafe(Plate->GetStaticMesh()),
			*GetPathNameSafe(Plate->GetMaterial(0)), *GetPathNameSafe(Plate->GetMaterial(1)),
			Plate->IsRegistered(), Plate->IsVisible(), Plate->bHiddenInGame,
			*Plate->GetRelativeTransform().ToHumanReadableString(), *Plate->GetComponentTransform().ToHumanReadableString(),
			*Plate->Bounds.Origin.ToString(), *Plate->Bounds.BoxExtent.ToString(),
			Text->IsRegistered(), Text->IsVisible(), Text->bHiddenInGame, *Text->Text.ToString(),
			*GetPathNameSafe(Text->Font.Get()), *GetPathNameSafe(Text->TextMaterial.Get()), *Text->TextRenderColor.ToString(),
			*Text->GetRelativeTransform().ToHumanReadableString(), *Text->GetComponentTransform().ToHumanReadableString(),
			*Text->Bounds.Origin.ToString(), *Text->Bounds.BoxExtent.ToString(),
			*MeshComp->Bounds.Origin.ToString(), *MeshComp->Bounds.BoxExtent.ToString());
	};
	LogPlateState(TEXT("Front"), FrontPlateComp, FrontPlateText);
	LogPlateState(TEXT("Back"), BackPlateComp, BackPlateText);
}

void ACarActor::ApplyTransformFromData(float MetersToUU)
{
	const FVector Loc = UCarPlacementLibrary::UnrealMetersToWorld(CarData.pos, MetersToUU);

	float Yaw = UCarPlacementLibrary::UnityRotYToUEYaw(CarData.rotY) + MeshForwardYawOffset;
	if (!CarData.isFront)
	{
		Yaw += 180.f;  // 후면 주차: 정면 반대 방향.
	}
	Yaw = UCarPlacementLibrary::AddYawDeg(Yaw, 0.f);  // [0,360) 정규화.

	SetActorLocationAndRotation(Loc, FRotator(0.f, Yaw, 0.f));
}

FCarPos ACarActor::ToCarPos(float MetersToUU) const
{
	FCarPos Out = CarData;  // id/type/presetId/slotId/prefabId/isFront 메타 유지.
	Out.pos = UCarPlacementLibrary::WorldToUnrealMeters(GetActorLocation(), MetersToUU);

	float Yaw = GetActorRotation().Yaw - MeshForwardYawOffset;
	if (!Out.isFront)
	{
		Yaw -= 180.f;
	}
	Out.rotY = UCarPlacementLibrary::AddYawDeg(UCarPlacementLibrary::UEYawToUnityRotY(Yaw), 0.f);
	return Out;
}

void ACarActor::SetSelected(bool bInSelected)
{
	bSelected = bInSelected;
	if (MeshComp)
	{
		// 도색(carpaint)과 겹치지 않게: 메시 위에 오버레이 머티리얼을 덧입혀 청록 림 발광으로 선택 표시.
		MeshComp->SetOverlayMaterial(bInSelected ? SelectionOverlayMaterial : nullptr);
		MeshComp->SetRenderCustomDepth(bInSelected);  // 외곽선 포스트프로세스 연동 시 사용(옵션).
	}
}
