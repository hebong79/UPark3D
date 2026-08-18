// Copyright Epic Games, Inc. All Rights Reserved.

#include "LightControlLibrary.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "../Park3DDataPaths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	// 허용 범위. 노출은 음수(더 밝게)도 쓸 수 있어야 하므로 하한을 -5 로 둔다.
	constexpr float ExposureMin = -5.0f;
	constexpr float ExposureMax = 20.0f;
	constexpr float SunIntensityMax = 150.0f;
	constexpr float SkyIntensityMax = 20.0f;
	// 채움광은 태양을 보조하는 용도라 태양 상한까지 열어 둘 이유가 없다. 실측 조정 폭(0~수 lux)의
	// 여유를 두고 20 에서 끊는다.
	constexpr float FillIntensityMax = 20.0f;

	const TCHAR* KeyExposure = TEXT("ExposureEV100");
	const TCHAR* KeySunIntensity = TEXT("SunIntensity");
	const TCHAR* KeySunColorR = TEXT("SunColorR");
	const TCHAR* KeySunColorG = TEXT("SunColorG");
	const TCHAR* KeySunColorB = TEXT("SunColorB");
	const TCHAR* KeyAltitude = TEXT("SunAltitudeDeg");
	const TCHAR* KeyAzimuth = TEXT("SunAzimuthDeg");
	const TCHAR* KeySkyIntensity = TEXT("SkyIntensity");
	const TCHAR* KeyShadowFill = TEXT("ShadowFillIntensity");
	const TCHAR* KeyCarFill = TEXT("CarFillIntensity");
}

void ULightControlLibrary::ClampSettings(FLightSettings& S)
{
	S.ExposureEV100 = FMath::Clamp(S.ExposureEV100, ExposureMin, ExposureMax);
	S.SunIntensity = FMath::Clamp(S.SunIntensity, 0.0f, SunIntensityMax);
	S.SkyIntensity = FMath::Clamp(S.SkyIntensity, 0.0f, SkyIntensityMax);
	S.ShadowFillIntensity = FMath::Clamp(S.ShadowFillIntensity, 0.0f, FillIntensityMax);
	S.CarFillIntensity = FMath::Clamp(S.CarFillIntensity, 0.0f, FillIntensityMax);
	S.SunAltitudeDeg = FMath::Clamp(S.SunAltitudeDeg, 0.0f, 90.0f);

	// 방위는 잘라내지 않고 0~360 으로 감는다(359 → 361 이동이 자연스럽도록).
	S.SunAzimuthDeg = FMath::Fmod(S.SunAzimuthDeg, 360.0f);
	if (S.SunAzimuthDeg < 0.0f)
	{
		S.SunAzimuthDeg += 360.0f;
	}

	S.SunColor.R = FMath::Clamp(S.SunColor.R, 0.0f, 1.0f);
	S.SunColor.G = FMath::Clamp(S.SunColor.G, 0.0f, 1.0f);
	S.SunColor.B = FMath::Clamp(S.SunColor.B, 0.0f, 1.0f);
	S.SunColor.A = 1.0f;
}

FString ULightControlLibrary::ToJson(const FLightSettings& S)
{
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(KeyExposure, S.ExposureEV100);
	Root->SetNumberField(KeySunIntensity, S.SunIntensity);
	Root->SetNumberField(KeySunColorR, S.SunColor.R);
	Root->SetNumberField(KeySunColorG, S.SunColor.G);
	Root->SetNumberField(KeySunColorB, S.SunColor.B);
	Root->SetNumberField(KeyAltitude, S.SunAltitudeDeg);
	Root->SetNumberField(KeyAzimuth, S.SunAzimuthDeg);
	Root->SetNumberField(KeySkyIntensity, S.SkyIntensity);
	Root->SetNumberField(KeyShadowFill, S.ShadowFillIntensity);
	Root->SetNumberField(KeyCarFill, S.CarFillIntensity);

	FString Out;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root, Writer);
	return Out;
}

bool ULightControlLibrary::FromJson(const FString& Json, FLightSettings& Out)
{
	if (Json.IsEmpty())
	{
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return false;
	}

	// 필수 키가 하나라도 없으면 다른 종류의 JSON 일 수 있다 — 조용히 절반만 읽지 않는다.
	// (루트 키만 같으면 통과시켜 엉뚱한 파일을 수용했던 선례를 피한다.)
	for (const TCHAR* Key : { KeyExposure, KeySunIntensity, KeyAltitude, KeyAzimuth, KeySkyIntensity })
	{
		if (!Root->HasTypedField<EJson::Number>(Key))
		{
			return false;
		}
	}

	FLightSettings Parsed;
	Parsed.ExposureEV100 = static_cast<float>(Root->GetNumberField(KeyExposure));
	Parsed.SunIntensity = static_cast<float>(Root->GetNumberField(KeySunIntensity));
	Parsed.SunAltitudeDeg = static_cast<float>(Root->GetNumberField(KeyAltitude));
	Parsed.SunAzimuthDeg = static_cast<float>(Root->GetNumberField(KeyAzimuth));
	Parsed.SkyIntensity = static_cast<float>(Root->GetNumberField(KeySkyIntensity));

	// 채움광은 누락 시 0(끔) 유지 — 이 항목이 생기기 전에 저장된 파일을 그대로 읽을 수 있어야 한다.
	// 필수 키 검사에 넣지 않은 것도 같은 이유다(넣으면 기존 파일이 전부 거절된다).
	double F = 0.0;
	Parsed.ShadowFillIntensity = Root->TryGetNumberField(KeyShadowFill, F) ? static_cast<float>(F) : 0.0f;
	Parsed.CarFillIntensity = Root->TryGetNumberField(KeyCarFill, F) ? static_cast<float>(F) : 0.0f;

	// 색은 누락 시 흰색 유지(구버전 파일 호환).
	double C = 1.0;
	Parsed.SunColor.R = Root->TryGetNumberField(KeySunColorR, C) ? static_cast<float>(C) : 1.0f;
	Parsed.SunColor.G = Root->TryGetNumberField(KeySunColorG, C) ? static_cast<float>(C) : 1.0f;
	Parsed.SunColor.B = Root->TryGetNumberField(KeySunColorB, C) ? static_cast<float>(C) : 1.0f;
	Parsed.SunColor.A = 1.0f;

	ClampSettings(Parsed);
	Out = Parsed;
	return true;
}

bool ULightControlLibrary::SaveToFile(const FString& Path, const FLightSettings& S)
{
	if (Path.IsEmpty())
	{
		return false;
	}
	return FFileHelper::SaveStringToFile(ToJson(S), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool ULightControlLibrary::LoadFromFile(const FString& Path, FLightSettings& Out)
{
	FString Json;
	if (Path.IsEmpty() || !FFileHelper::LoadFileToString(Json, *Path))
	{
		return false;
	}
	return FromJson(Json, Out);
}

FString ULightControlLibrary::GetLightDir()
{
	return FPaths::GetPath(Park3DDataPaths::GetDataFilePath(TEXT("Light"), TEXT("x.json")));
}

FString ULightControlLibrary::GetSuggestedFilePath()
{
	return Park3DDataPaths::GetDataFilePath(TEXT("Light"), TEXT("LightSettings.json"));
}

FString ULightControlLibrary::GetDefaultPointerPath()
{
	return Park3DDataPaths::GetDataFilePath(TEXT("Light"), TEXT("_default.txt"));
}

bool ULightControlLibrary::SetDefaultFile(const FString& SettingsPath)
{
	if (SettingsPath.IsEmpty())
	{
		return false;
	}
	return FFileHelper::SaveStringToFile(SettingsPath, *GetDefaultPointerPath(),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool ULightControlLibrary::LoadDefaultSettings(FLightSettings& Out)
{
	FString Pointer;
	if (!FFileHelper::LoadFileToString(Pointer, *GetDefaultPointerPath()))
	{
		return false;
	}

	Pointer.TrimStartAndEndInline();
	if (Pointer.IsEmpty())
	{
		return false;
	}

	// 포인터가 파일명만 담고 있어도 동작하도록 Light 디렉터리 기준으로 보정한다.
	const FString Path = FPaths::IsRelative(Pointer)
		? FPaths::Combine(GetLightDir(), Pointer)
		: Pointer;

	return LoadFromFile(Path, Out);
}
