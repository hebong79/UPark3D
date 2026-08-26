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
	 * 기동할 레벨. 빈 문자열 = 부팅맵(ini GameDefaultMap) 그대로.
	 * 세 가지 표기를 모두 받는다: "Levels/LV_Park_01", "/Game/Levels/LV_Park_01", "LV_Park_01".
	 * 예) "Maps/PresetMaker1"  = 바닥만 있는 기본 레벨(주차면·차량·카메라를 config 파일로 그려 넣는 쪽)
	 *     "Levels/LV_Park_01" = 건물·도로가 있는 도심 주차장 레벨
	 * ⚠ 여기에 적을 수 있는 레벨은 쿡에 포함된 것뿐이다(DefaultGame.ini 의 MapsToCook).
	 * ⚠ 레벨이 자기 노면을 가지면 map_floor 를 false 로 함께 둘 것 — 아니면 단색 바닥이 그 위를 덮는다.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Config")
	FString Level;

	/**
	 * Save/3D/Light 기준 조명 설정 파일명. 빈 문자열 = 기존 동작(_default.txt 가 가리키는 파일).
	 * **레벨과 짝이다.** 통합 하늘이 있는 레벨(LV_Park_01)은 코드가 조명에 손대지 않지만,
	 * 하늘이 없는 레벨(Maps/PresetMaker1)은 코드가 태양·하늘·노출볼륨을 만들고 이 값을 적용한다 —
	 * 도심 레벨용 값을 그대로 쓰면 화면이 하얗게 뜬다. level 을 바꾸면 이 값도 함께 바꿀 것.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Config")
	FString LightFile;

	/**
	 * 시작할 때 숨길 레벨 액터 이름 목록(JSON `hide_actors`). 이름은 `env.list` 가 주는 `name` 이다.
	 * 레벨의 나무·간판처럼 카메라 시야를 가리는 물체를 레벨 에셋을 고치지 않고 치우기 위한 것 —
	 * `env.hide` 와 같은 처리를 기동 시 한 번 적용한다(숨김은 런타임 상태라 실행마다 다시 걸어야 한다).
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Config")
	TArray<FString> HideActors;

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

	/**
	 * 메인 뷰 MJPEG 포트. 0 = 미지정(ini 의 MainPort 를 쓴다).
	 * 한 PC 에서 시뮬레이터를 두 대 띄울 때 두 번째 인스턴스가 이 포트를 비켜 가야 한다 —
	 * ini 는 패키지에 쿠킹돼 인스턴스별로 바꿀 수 없으므로 config 로 덮을 길을 연다(rpc_port 와 같은 규약).
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Config")
	int32 MainPort = 0;

	/**
	 * 동시 캡처 슬롯 수(= 동시에 볼 수 있는 카메라 수). 0 = 미지정(ini 의 ActiveSlots 를 쓴다).
	 *
	 * main_port 와 같은 이유로 여기에 있다 — ini 는 pak 안에 쿠킹돼 인스턴스별로 바꿀 수 없고,
	 * 이 값은 배포된 PC 의 성능·용도에 따라 달라져야 한다. exe 를 갈아끼워도 ini 는 옛 값이
	 * 그대로 이기므로, 재쿡 없이 바꿀 수 있는 길이 여기여야 한다.
	 * 런타임 변경은 cam.setStreamSlots 가 계속 담당한다(이 값은 기동 시 기본값).
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Config")
	int32 StreamSlots = 0;

	/** StreamSlots 가 넘을 수 없는 상한. 0 = 미지정(ini 의 HardMaxSlots). */
	UPROPERTY(BlueprintReadWrite, Category = "Config")
	int32 StreamHardMaxSlots = 0;

	/**
	 * 총 캡처 예산(fps). 0 이하 = 미지정(ini 의 TotalFps).
	 * 채널당 fps 는 이 값을 "지금 보고 있는 채널 수"로 나눈 것이다 — 시청자가 많은 현장에서
	 * 화질(프레임)과 동시 채널 수를 맞바꾸는 손잡이가 이것뿐이라 config 로 연다.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Config")
	float StreamTotalFps = 0.f;

	/**
	 * RPC 리스너 바인드 주소. "any" = 모든 인터페이스, "localhost" = 루프백 전용. 빈 문자열 = 기본값("any").
	 * 이 값이 곧 [HTTPServer.Listeners] 의 포트별 override 로 런타임에 등록된다 —
	 * ini 에 포트를 박아 두면 rpc_port 를 바꿀 때마다 목록이 빗나가 조용히 루프백으로 떨어지기 때문이다.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Config")
	FString RpcBindAddress;
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
