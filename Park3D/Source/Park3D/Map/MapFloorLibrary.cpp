// Copyright Epic Games, Inc. All Rights Reserved.

#include "MapFloorLibrary.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"

bool UMapFloorLibrary::ParseSizeMeters(const FString& Text, float& OutMeters)
{
	const FString Trimmed = Text.TrimStartAndEnd();
	if (!Trimmed.IsNumeric())
	{
		return false; // 빈 문자열 / "abc" / "12abc" → 실패. OutMeters 미변경.
	}
	OutMeters = FCString::Atof(*Trimmed);
	return true;
}

float UMapFloorLibrary::ClampSizeMeters(float Meters)
{
	if (!FMath::IsFinite(Meters))
	{
		return DefaultSizeM; // NaN/Inf 는 클램프로 걸러지지 않으므로 기본값 폴백.
	}
	return FMath::Clamp(Meters, MinSizeM, MaxSizeM);
}

FVector UMapFloorLibrary::MapSizeToPlaneScale(float WidthM, float DepthM, float MetersToUU, float PlaneBaseUU)
{
	if (FMath::IsNearlyZero(PlaneBaseUU))
	{
		return FVector::OneVector;
	}
	// 가로(X) → UE X, 세로(Z) → UE Y. 평면이므로 Z 스케일은 1 고정.
	return FVector(
		(WidthM * MetersToUU) / PlaneBaseUU,
		(DepthM * MetersToUU) / PlaneBaseUU,
		1.f);
}

bool UMapFloorLibrary::SaveMapSizeToJson(const FString& FilePath, float WidthM, float DepthM)
{
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("sizeX"), WidthM);
	Root->SetNumberField(TEXT("sizeZ"), DepthM);

	FString Out;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	if (!FJsonSerializer::Serialize(Root, Writer))
	{
		return false;
	}
	return FFileHelper::SaveStringToFile(Out, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool UMapFloorLibrary::LoadMapSizeFromJson(const FString& FilePath, float& OutWidthM, float& OutDepthM)
{
	FString In;
	if (!FFileHelper::LoadFileToString(In, *FilePath))
	{
		return false;
	}
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(In);
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return false;
	}
	double W = 0.0, D = 0.0;
	if (!Root->TryGetNumberField(TEXT("sizeX"), W) || !Root->TryGetNumberField(TEXT("sizeZ"), D))
	{
		return false;
	}
	OutWidthM = static_cast<float>(W);
	OutDepthM = static_cast<float>(D);
	return true;
}
