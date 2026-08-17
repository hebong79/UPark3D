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

	/**
	 * PTZ 카메라 줌 상한(배율). 0 이하 = 미지정(액터 기본값 유지).
	 * 최상위 max_zoom 과 camera.max_zoom 이 모두 여기로 들어오며, 둘 다 있으면 camera 쪽이 이긴다(설계 §3.2).
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Config")
	float MaxZoom = 0.f;

	/**
	 * 기준 수평 화각(도, zoom=1 일 때의 FOV). 0 이하 = 미지정(액터 기본값 56.5 유지).
	 * camera.hfov_wide 전용 — 최상위에는 대응 키를 두지 않는다(신규 항목이라 하위 호환 대상이 없다).
	 * 범위 검증은 여기서 하지 않는다. 원본 값을 그대로 담아 적용 함수
	 * (ACameraControlManager::SetCameraDefaultHFov)가 (0,180) 판정과 경고를 맡는다 — max_zoom 선례(설계 §4.2).
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Config")
	float CameraHFovWide = 0.f;

	/**
	 * 카메라 모델 라벨(예: "HNR-2036LA"). 빈 문자열 = 미지정.
	 * 코드가 값으로 쓰지 않는다 — 어느 장비 기준 데이터인지 추적하기 위한 라벨이며 시작 로그에만 찍힌다(설계 §1.2 R3).
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Config")
	FString CameraModel;

	/** 카메라 스트림 포트 대역 시작(camId 1 의 포트). 0 = 미지정. */
	UPROPERTY(BlueprintReadWrite, Category = "Config")
	int32 CamPortMin = 0;

	/** 카메라 스트림 포트 대역 끝. 대역 크기 = Max-Min+1 이 곧 채널 상한이다. 0 = 미지정. */
	UPROPERTY(BlueprintReadWrite, Category = "Config")
	int32 CamPortMax = 0;

	/**
	 * 코드가 까는 단색 바닥(AMapFloorActor)을 쓸 것인가. 기본 true = 기존 동작(빈 부트 맵 대비).
	 * 레벨이 자기 노면(아스팔트·차선·연석)을 갖고 있으면 false 로 둔다 — true 로 두면 160×160m
	 * 평면이 그 위를 덮어 레벨 노면이 보이지 않는다.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Config")
	bool bMapFloor = true;

	/** 포트 대역이 유효한가(둘 다 지정 + 2<=min<=max<=65535). min=1 은 BasePort 0 이 되어 거부된다. */
	bool HasValidCamPortRange() const
	{
		return CamPortMin >= 2 && CamPortMax >= CamPortMin && CamPortMax <= 65535;
	}
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
	 * config 파일의 cam_port_min/max 를 바꿔 다시 쓴다. 파일이 없거나 파싱/기록 실패면 false.
	 * 구조체를 통째로 직렬화하지 않고 원본 JSON 오브젝트의 해당 필드만 교체한다 —
	 * 이 구조체가 모르는 키(사람이 손으로 넣은 항목)가 자동 수정에 지워지지 않게 하려는 것이다.
	 * max 만 쓰지 않는 이유: FromJson 이 두 키가 모두 있을 때만 대역을 채우므로, min 이 없는
	 * config(패키지 기본 파일)에서는 확장이 다음 기동에 조용히 사라진다.
	 */
	static bool UpdateCamPortRange(const FString& Path, int32 NewMin, int32 NewMax);

	/**
	 * 데이터 파일 경로 해석. 파일명만 주어지면 Save/3D/<SubDir>/<파일명>,
	 * 경로 구분자가 포함되면 그대로(상대경로는 절대경로로 변환) 사용한다.
	 * 빈 문자열이면 빈 문자열을 돌려준다(= 미지정).
	 */
	static FString ResolveDataPath(const TCHAR* SubDir, const FString& FileNameOrPath);
};
