// Copyright Epic Games, Inc. All Rights Reserved.

#include "Park3DAppConfig.h"
#include "../Park3DDataPaths.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

FString UPark3DAppConfigLibrary::GetConfigDir()
{
	return FPaths::Combine(Park3DDataPaths::GetSaveRootDir(), TEXT("Config"));
}

FString UPark3DAppConfigLibrary::GetConfigFilePath()
{
	return FPaths::Combine(GetConfigDir(), GetConfigFileName());
}

bool UPark3DAppConfigLibrary::FromJson(const FString& Json, FPark3DAppConfig& Out)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return false;
	}

	// 성공한 파싱만 Out 에 반영한다(부분 실패로 반쯤 채워진 설정을 만들지 않는다).
	FPark3DAppConfig Parsed = Out;

	double Num = 0.0;
	if (Root->TryGetNumberField(TEXT("rpc_port"), Num))
	{
		const int32 PortValue = static_cast<int32>(Num);
		// 범위 밖은 미지정으로 둔다 — 호출부(포트 결정 체인)가 조용히 이상한 포트를 잡지 않게 한다.
		Parsed.RpcPort = (PortValue > 0 && PortValue <= 65535) ? PortValue : 0;
	}
	if (Root->TryGetNumberField(TEXT("max_zoom"), Num))
	{
		Parsed.MaxZoom = static_cast<float>(Num);
	}

	FString Str;
	if (Root->TryGetStringField(TEXT("preset_file"), Str))    { Parsed.PresetFile = Str.TrimStartAndEnd(); }
	if (Root->TryGetStringField(TEXT("carpos_file"), Str))    { Parsed.CarPosFile = Str.TrimStartAndEnd(); }
	if (Root->TryGetStringField(TEXT("camerapos_file"), Str)) { Parsed.CameraPosFile = Str.TrimStartAndEnd(); }

	Out = Parsed;
	return true;
}

bool UPark3DAppConfigLibrary::LoadFromFile(const FString& Path, FPark3DAppConfig& Out)
{
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		return false;
	}
	return FromJson(Json, Out);
}

bool UPark3DAppConfigLibrary::Load(FPark3DAppConfig& Out)
{
	return LoadFromFile(GetConfigFilePath(), Out);
}

FString UPark3DAppConfigLibrary::ResolveDataPath(const TCHAR* SubDir, const FString& FileNameOrPath)
{
	if (FileNameOrPath.IsEmpty())
	{
		return FString();
	}

	// 경로가 섞여 있으면(사용자가 다른 폴더를 가리킨 경우) 그대로 존중한다.
	if (FileNameOrPath.Contains(TEXT("/")) || FileNameOrPath.Contains(TEXT("\\")))
	{
		return FPaths::IsRelative(FileNameOrPath)
			? FPaths::ConvertRelativePathToFull(FPaths::Combine(Park3DDataPaths::GetSaveRootDir(), FileNameOrPath))
			: FileNameOrPath;
	}

	return Park3DDataPaths::GetDataFilePath(SubDir, *FileNameOrPath);
}
