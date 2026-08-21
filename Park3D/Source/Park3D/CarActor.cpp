// Copyright Epic Games, Inc. All Rights Reserved.

#include "CarActor.h"
#include "CarPlateNumberWidget.h"
#include "Components/WidgetComponent.h"
#include "CarColorComponent.h"
#include "CarPlacementLibrary.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Font.h"
#include "Engine/HitResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
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

	// 실제 표시는 3D 위젯이 한다(위 TextRender 는 Runtime 폰트를 못 그린다 — CarPlateNumberWidget.h 주석).
	// DrawSize 는 번호판 비율(520×110)과 같게 두고, 실제 크기는 아래 정렬에서 스케일로 맞춘다.
	FrontPlateWidget = CreateDefaultSubobject<UWidgetComponent>(TEXT("FrontPlateWidget"));
	FrontPlateWidget->SetupAttachment(FrontPlateComp);
	BackPlateWidget = CreateDefaultSubobject<UWidgetComponent>(TEXT("BackPlateWidget"));
	BackPlateWidget->SetupAttachment(BackPlateComp);
	for (UWidgetComponent* PlateWidget : { FrontPlateWidget, BackPlateWidget })
	{
		PlateWidget->SetWidgetClass(UCarPlateNumberWidget::StaticClass());
		PlateWidget->SetWidgetSpace(EWidgetSpace::World);
		PlateWidget->SetDrawSize(FVector2D(520.f, 110.f));
		PlateWidget->SetTwoSided(false);
		PlateWidget->SetBackgroundColor(FLinearColor::Transparent); // 판의 흰 바탕·파란 KOR 은 메시가 그린다.
		PlateWidget->SetTickWhenOffscreen(false);
		PlateWidget->SetGenerateOverlapEvents(false);
		PlateWidget->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PlateWidget->SetCastShadow(false);
	}
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
	// 실제 번호판은 "672우 3269" 처럼 앞 세 자리와 한글이 붙고 뒤 네 자리만 떨어진다.
	return CanonicalNumber.Len() == 8
		? CanonicalNumber.Left(3) + CanonicalNumber.Mid(3, 1) + TEXT(" ") + CanonicalNumber.Right(4)
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

	// 실제로 보이는 것은 위젯이다. 같은 면 바깥에 두되, 위젯 평면은 자기 +X 가 아니라 -X 를 향해 그려지므로
	// (WidgetComponent 는 XY 평면에 그리고 법선이 +X 다) 텍스트와 같은 회전을 쓰고 위치만 살짝 더 띄운다.
	if (UWidgetComponent* Widget = (Plate == FrontPlateComp) ? FrontPlateWidget : BackPlateWidget)
	{
		Widget->SetRelativeLocation(BoundsOrigin + OutwardLocal * (ThinExtent + PlateWidgetSurfaceGap));
		Widget->SetRelativeRotation(FRotationMatrix::MakeFromXZ(OutwardLocal, MeshWide ^ OutwardLocal).Rotator());
		// DrawSize(520×110 px) → 실제 판 크기(긴 축 지름 = 넓은 축 extent×2 cm)에 맞춘 스케일.
		// 위젯은 판 전체를 1:1 로 덮고, 번호가 놓이는 영역은 위젯 안쪽 여백이 정한다.
		const double PlateWidthCm = Ext[WideAxis] * 2.0;
		const float Scale = static_cast<float>(PlateWidthCm / PlateWidgetDrawWidth);
		Widget->SetRelativeScale3D(FVector(Scale));
		if (UCarPlateNumberWidget* PlateWidget = Cast<UCarPlateNumberWidget>(Widget->GetUserWidgetObject()))
		{
			PlateWidget->SetPlateNumber(MakePlateDisplayText(PlateNumber));
		}
	}

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

	// 데이터의 z 는 노면 높이를 모른다. CarPos_Seoshin_2Cam.json 23건은 0 ~ −5cm 인데 LV_Park_01 의
	// 노면은 Z=10cm 라 전부 10~15cm 묻혀 있었다(커서 피킹이 노면이 아니라 Landscape 를 맞힌 결과).
	// 여기서 지면에 앉힌다 — 저장하면 고쳐진 z 가 파일에도 남는다.
	SnapToGround(MetersToUU);
}

bool ACarActor::SnapToGround(float MetersToUU)
{
	UWorld* World = GetWorld();
	if (!World || !MeshComp || !MeshComp->GetStaticMesh())
	{
		return false;
	}

	// 메시 로컬 바운즈의 바닥 = 바퀴 접지면. MeshComp 가 루트이고 스케일을 건드리지 않으며 회전이
	// yaw 뿐이라, 이 값은 액터 원점에서 접지면까지의 월드 Z 차이와 그대로 같다.
	// (차량 메시 23종 실측: 바닥 z 가 −0.4cm ~ 0cm 라 사실상 피벗이 접지면이다 — car.catalog boundsMin)
	FVector LocalMin = FVector::ZeroVector;
	FVector LocalMax = FVector::ZeroVector;
	MeshComp->GetLocalBounds(LocalMin, LocalMax);

	const FVector ActorLoc = GetActorLocation();
	const double BottomZ = ActorLoc.Z + LocalMin.Z;

	// ⚠ 채널이 아니라 **정적 오브젝트 질의**로 찾는다. 프로젝트의 다른 트레이스(view.pick·measure.*)가
	//   쓰는 ECC_Visibility 로는 노면을 못 찾는다 — LV_Park_01 의 아스팔트(BP_SplineMeshPlane)는
	//   Visibility 응답이 Ignore 라 트레이스가 그대로 통과해 10cm 아래 Landscape 를 맞힌다.
	//   실측(SnapDiag): 통로 (13.0, 4.0) 에서 Visibility=0.00cm(Landscape) / 정적질의=10.00cm(노면).
	//   주차면 위에서만 Visibility 가 10.10cm 를 주는데, 그건 노면이 아니라 BP_ParkingSlot 의 콜리전이다.
	// 정적 질의는 런타임 스폰된 차량(Movable=WorldDynamic)을 걸러 주기도 한다 — 주행 시뮬에서 차가
	// 겹치는 순간 남의 지붕에 올라타는 일이 없다(실측: 차량 위에서도 노면 10.00cm 를 반환).
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CarSnapToGround), /*bTraceComplex=*/true, this);
	if (!World->LineTraceSingleByObjectType(Hit,
		FVector(ActorLoc.X, ActorLoc.Y, BottomZ + GroundTraceUpCm),
		FVector(ActorLoc.X, ActorLoc.Y, BottomZ - GroundTraceDownCm),
		FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllStaticObjects), Params))
	{
		// 못 찾으면 높이를 지어내지 않고 데이터 값을 그대로 둔다(view.pick 의 hit=false 규약과 같은 태도).
		UE_LOG(LogTemp, Verbose, TEXT("[Car] %s 접지 실패 — 아래 %.0fcm 안에 지면이 없어 z=%.2f 를 유지합니다."),
			*CarData.id, GroundTraceDownCm, ActorLoc.Z);
		return false;
	}

	const double NewZ = Hit.ImpactPoint.Z - LocalMin.Z;
	if (FMath::IsNearlyEqual(NewZ, ActorLoc.Z, GroundSnapToleranceCm))
	{
		return false;   // 이미 앉아 있다.
	}

	const FVector NewLoc(ActorLoc.X, ActorLoc.Y, NewZ);
	SetActorLocation(NewLoc);
	CarData.pos.z = UCarPlacementLibrary::WorldToUnrealMeters(NewLoc, MetersToUU).z;
	return true;
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
	ApplySelectionVisual();
}

void ACarActor::SetSelectionMarkVisible(bool bInVisible)
{
	bSelectionMarkVisible = bInVisible;
	ApplySelectionVisual();
}

void ACarActor::ApplySelectionVisual()
{
	if (!MeshComp)
	{
		return;
	}

	// 선택돼 있어도 표시가 꺼져 있으면 아무것도 덧입히지 않는다. bSelected 는 그대로 두므로
	// IsSelected() 를 보는 쪽(Automation 테스트·RPC)은 표시 설정과 무관하게 동작한다.
	const bool bShowMark = bSelected && bSelectionMarkVisible;

	// 도색(carpaint)과 겹치지 않게: 메시 위에 오버레이 머티리얼을 덧입혀 청록 림 발광으로 선택 표시.
	MeshComp->SetOverlayMaterial(bShowMark ? SelectionOverlayMaterial : nullptr);
	MeshComp->SetRenderCustomDepth(bShowMark);  // 외곽선 포스트프로세스 연동 시 사용(옵션).
}
