// Copyright Epic Games, Inc. All Rights Reserved.

#include "LotLampActor.h"

#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/ConstructorHelpers.h"
#include "../Park3DDataPaths.h"

ALotLampActor::ALotLampActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	RootComponent = Root;

	// 엔진 기본 도형 — CarActor 가 이미 쓰고 있어 패키지에 쿠킹돼 있다(새 에셋 없이 만든다).
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube"));

	PoleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Pole"));
	PoleMesh->SetupAttachment(Root);
	PoleMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (CylinderFinder.Succeeded()) { PoleMesh->SetStaticMesh(CylinderFinder.Object); }

	HeadMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Head"));
	HeadMesh->SetupAttachment(Root);
	HeadMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (CubeFinder.Succeeded()) { HeadMesh->SetStaticMesh(CubeFinder.Object); }

	Spot = CreateDefaultSubobject<USpotLightComponent>(TEXT("Spot"));
	Spot->SetupAttachment(Root);
	Spot->SetMobility(EComponentMobility::Movable);
	Spot->SetIntensityUnits(ELightUnits::Candelas);
	Spot->SetAttenuationRadius(2500.0f);
	Spot->SetCastShadows(true);
}

void ALotLampActor::ApplyShape()
{
	const float H = HeightM * 100.0f;
	// Cylinder·Cube 는 원점 중심 100cm 도형이다.
	PoleMesh->SetRelativeLocation(FVector(0.0, 0.0, H * 0.5f));
	PoleMesh->SetRelativeScale3D(FVector(0.15, 0.15, H / 100.0f));
	HeadMesh->SetRelativeLocation(FVector(0.0, 0.0, H));
	HeadMesh->SetRelativeScale3D(FVector(0.6, 0.35, 0.12));
	// 등기구 바로 아래에서 수직 하향. 기둥은 그림자를 드리우지 않게(빛이 기둥 안에서 나온다) 등기구 아래로 뺀다.
	Spot->SetRelativeLocation(FVector(0.0, 0.0, H - 12.0f));
	Spot->SetRelativeRotation(FRotator(-90.0, 0.0, 0.0));
	Spot->SetOuterConeAngle(ConeDeg * 0.5f);
	Spot->SetInnerConeAngle(ConeDeg * 0.3f);
	Spot->SetIntensity(IntensityCd);
	Spot->SetLightColor(Color);
	Spot->SetVisibility(bOn);
}

void ALotLampActor::Configure(const FString& InName, float InHeightM, float InIntensityCd, const FLinearColor& InColor, float InConeDeg, bool bInOn)
{
	LampName = InName;
	HeightM = FMath::Clamp(InHeightM, 1.0f, 30.0f);
	IntensityCd = FMath::Clamp(InIntensityCd, 0.0f, 100000.0f);
	Color = InColor;
	Color.A = 1.0f;
	ConeDeg = FMath::Clamp(InConeDeg, 5.0f, 170.0f);
	bOn = bInOn;
	ApplyShape();
}

void ALotLampActor::SetOn(bool bInOn) { bOn = bInOn; Spot->SetVisibility(bOn); }
void ALotLampActor::SetIntensity(float InIntensityCd) { IntensityCd = FMath::Clamp(InIntensityCd, 0.0f, 100000.0f); Spot->SetIntensity(IntensityCd); }
void ALotLampActor::SetColor(const FLinearColor& InColor) { Color = InColor; Color.A = 1.0f; Spot->SetLightColor(Color); }

TSharedPtr<FJsonObject> ALotLampActor::ToJson() const
{
	auto V3 = [](double X, double Y, double Z)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("x"), X);
		O->SetNumberField(TEXT("y"), Y);
		O->SetNumberField(TEXT("z"), Z);
		return O;
	};
	const FVector L = GetActorLocation();
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("name"), LampName);
	O->SetObjectField(TEXT("pos"), V3(L.X / 100.0, L.Y / 100.0, L.Z / 100.0));
	O->SetNumberField(TEXT("height"), HeightM);
	O->SetNumberField(TEXT("intensity"), IntensityCd);
	O->SetObjectField(TEXT("color"), V3(Color.R, Color.G, Color.B));
	O->SetNumberField(TEXT("coneDeg"), ConeDeg);
	O->SetBoolField(TEXT("on"), bOn);
	return O;
}

TArray<ALotLampActor*> ALotLampActor::GetAll(UWorld* World)
{
	TArray<ALotLampActor*> Out;
	if (World)
	{
		for (TActorIterator<ALotLampActor> It(World); It; ++It)
		{
			if (IsValid(*It)) { Out.Add(*It); }
		}
	}
	Out.Sort([](const ALotLampActor& A, const ALotLampActor& B) { return A.LampName < B.LampName; });
	return Out;
}

ALotLampActor* ALotLampActor::FindByName(UWorld* World, const FString& Name)
{
	for (ALotLampActor* L : GetAll(World))
	{
		if (L->LampName == Name) { return L; }
	}
	return nullptr;
}

FString ALotLampActor::MakeUniqueName(UWorld* World, const FString& Wanted)
{
	if (!Wanted.IsEmpty() && !FindByName(World, Wanted))
	{
		return Wanted;
	}
	for (int32 i = 1;; ++i)
	{
		const FString N = FString::Printf(TEXT("Lamp_%d"), i);
		if (!FindByName(World, N)) { return N; }
	}
}

ALotLampActor* ALotLampActor::Spawn(UWorld* World, const FVector& GroundCm, const FString& Name, float HeightM, float IntensityCd,
	const FLinearColor& Color, float ConeDeg, bool bOn)
{
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ALotLampActor* L = World->SpawnActor<ALotLampActor>(ALotLampActor::StaticClass(), GroundCm, FRotator::ZeroRotator, Params);
	if (L)
	{
		L->Configure(Name, HeightM, IntensityCd, Color, ConeDeg, bOn);
	}
	return L;
}

FString ALotLampActor::GetFilePath(const UWorld* World)
{
	const FString Level = World ? UWorld::RemovePIEPrefix(World->GetMapName()) : FString(TEXT("NoWorld"));
	return Park3DDataPaths::GetDataFilePath(TEXT("Lamp"), *FString::Printf(TEXT("Lamp_%s.json"), *Level));
}

bool ALotLampActor::SaveAll(UWorld* World, FString& OutPath)
{
	OutPath = GetFilePath(World);
	const TArray<ALotLampActor*> All = GetAll(World);
	if (All.Num() == 0)
	{
		return !IFileManager::Get().FileExists(*OutPath) || IFileManager::Get().Delete(*OutPath);
	}
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const ALotLampActor* L : All) { Arr.Add(MakeShared<FJsonValueObject>(L->ToJson())); }
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("level"), UWorld::RemovePIEPrefix(World->GetMapName()));
	Root->SetArrayField(TEXT("lamps"), Arr);
	FString Text;
	FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&Text));
	return FFileHelper::SaveStringToFile(Text, *OutPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

int32 ALotLampActor::LoadForWorld(UWorld* World)
{
	const FString Path = GetFilePath(World);
	FString Text;
	if (!World || !FFileHelper::LoadFileToString(Text, *Path))
	{
		return 0;
	}
	TSharedPtr<FJsonObject> Root;
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid() || !Root->TryGetArrayField(TEXT("lamps"), Arr))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Lamp] 가로등 파일을 읽지 못했습니다: %s"), *Path);
		return 0;
	}
	for (ALotLampActor* Old : GetAll(World)) { Old->Destroy(); }

	int32 Count = 0;
	for (const TSharedPtr<FJsonValue>& V : *Arr)
	{
		const TSharedPtr<FJsonObject>* O = nullptr;
		const TSharedPtr<FJsonObject>* P = nullptr;
		if (!V.IsValid() || !V->TryGetObject(O) || !(*O)->TryGetObjectField(TEXT("pos"), P)) { continue; }
		const FVector Ground((*P)->GetNumberField(TEXT("x")) * 100.0, (*P)->GetNumberField(TEXT("y")) * 100.0, (*P)->GetNumberField(TEXT("z")) * 100.0);
		double H = DefaultHeightM, I = DefaultIntensityCd, Cone = DefaultConeDeg;
		bool bLampOn = true;
		(*O)->TryGetNumberField(TEXT("height"), H);
		(*O)->TryGetNumberField(TEXT("intensity"), I);
		(*O)->TryGetNumberField(TEXT("coneDeg"), Cone);
		(*O)->TryGetBoolField(TEXT("on"), bLampOn);
		FLinearColor C(1.0f, 0.85f, 0.65f);
		const TSharedPtr<FJsonObject>* CO = nullptr;
		if ((*O)->TryGetObjectField(TEXT("color"), CO))
		{
			C = FLinearColor((*CO)->GetNumberField(TEXT("x")), (*CO)->GetNumberField(TEXT("y")), (*CO)->GetNumberField(TEXT("z")));
		}
		FString Name;
		(*O)->TryGetStringField(TEXT("name"), Name);
		if (Spawn(World, Ground, MakeUniqueName(World, Name), H, I, C, Cone, bLampOn)) { ++Count; }
	}
	UE_LOG(LogTemp, Log, TEXT("[Lamp] 가로등 %d개 복원 ← %s"), Count, *Path);
	return Count;
}
