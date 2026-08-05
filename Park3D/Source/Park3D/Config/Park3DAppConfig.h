// Copyright Epic Games, Inc. All Rights Reserved.
// Park3DAppConfig : 앱 시작 설정(Save/Config/config_pmaker.json) 파싱·경로 해석.
// Unity CPresetMakerScene.LoadInitialData + CPSimConfig 대응. 월드/액터에 의존하지 않아 유닛테스트로 전량 검증한다.
// 실제 적용은 APark3DGameMode::ApplyStartupConfig(데이터 3종)과 URpcServerSubsystem(포트)이 담당한다.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Park3DAppConfig.generated.h"

/** config_pmaker.json 한 벌. 값이 없는 항목은 "미지정"(0/빈 문자열)으로 남겨 호출부가 건너뛴다. */
USTRUCT(BlueprintType)
struct FPark3DAppConfig
{
	GENERATED_BODY()

	/** JSON-RPC 리슨 포트. 0 = 미지정(포트 결정 체인에서 스킵). */
	UPROPERTY(BlueprintReadWrite, Category = "Config")
	int32 RpcPort = 0;

	/** Save/3D/Preset 기준 프리셋 파일명(또는 경로). 빈 문자열 = 미지정. */
	UPROPERTY(BlueprintReadWrite, Category = "Config")
	FString PresetFile;

	/** Save/3D/CarPos 기준 차량배치 파일명(또는 경로). */
	UPROPERTY(BlueprintReadWrite, Category = "Config")
	FString CarPosFile;

	/** Save/3D/CameraPos 기준 카메라위치 파일명(또는 경로). */
	UPROPERTY(BlueprintReadWrite, Category = "Config")
	FString CameraPosFile;

	/** PTZ 카메라 줌 상한(배율). 0 이하 = 미지정(액터 기본값 유지). */
	UPROPERTY(BlueprintReadWrite, Category = "Config")
	float MaxZoom = 0.f;
};

UCLASS()
class PARK3D_API UPark3DAppConfigLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 설정 파일 이름(고정). */
	static const TCHAR* GetConfigFileName() { return TEXT("config_pmaker.json"); }

	/** <SaveRoot>/Config 절대 경로. */
	static FString GetConfigDir();

	/** <SaveRoot>/Config/config_pmaker.json 절대 경로. */
	static FString GetConfigFilePath();

	/**
	 * JSON 문자열 → 설정. 파싱 실패 시 false 를 반환하고 Out 을 건드리지 않는다.
	 * 없는 키는 Out 의 기본값을 유지한다(부분 설정 허용).
	 * 필드 단위로 직접 읽는다 — FJsonObjectConverter 는 루트 키만 맞으면 다른 스키마도
	 * 조용히 성공시켜, 엉뚱한 파일을 설정 파일로 오인하는 전례가 있었다.
	 */
	static bool FromJson(const FString& Json, FPark3DAppConfig& Out);

	/** 파일 읽기 + 파싱. 파일이 없거나 파싱 실패면 false. */
	static bool LoadFromFile(const FString& Path, FPark3DAppConfig& Out);

	/** 기본 경로(GetConfigFilePath)에서 로드. */
	static bool Load(FPark3DAppConfig& Out);

	/**
	 * 데이터 파일 경로 해석. 파일명만 주어지면 Save/3D/<SubDir>/<파일명>,
	 * 경로 구분자가 포함되면 그대로(상대경로는 절대경로로 변환) 사용한다.
	 * 빈 문자열이면 빈 문자열을 돌려준다(= 미지정).
	 */
	static FString ResolveDataPath(const TCHAR* SubDir, const FString& FileNameOrPath);
};
