// Copyright Epic Games, Inc. All Rights Reserved.
// LightControlLibrary : 조명 설정의 순수 함수 모음(직렬화·클램프·파일 입출력·기본값 포인터).
// 월드/액터에 의존하지 않으므로 유닛테스트로 전량 검증한다. 실제 적용은 ALightControlManager 담당.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LightControlTypes.h"
#include "LightControlLibrary.generated.h"

UCLASS()
class PARK3D_API ULightControlLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ---- 각도 규약 변환 ----
	/** UI 고도(지평선 위 0~90) → DirectionalLight pitch(하향이 음수). */
	UFUNCTION(BlueprintPure, Category = "Light")
	static float AltitudeToPitch(float AltitudeDeg) { return -AltitudeDeg; }

	/** DirectionalLight pitch → UI 고도. */
	UFUNCTION(BlueprintPure, Category = "Light")
	static float PitchToAltitude(float PitchDeg) { return -PitchDeg; }

	// ---- 값 범위(ClampSettings 와 같은 상수) ----
	/** 키 하나의 허용 범위·UI 권장 간격·단위·기본값. sunColor 는 채널별 0~1 이라 별도다. */
	struct FKeyMeta
	{
		const TCHAR* Key;
		float Min, Max, Step;
		const TCHAR* Unit;
		float Default;
	};
	static TArray<FKeyMeta> GetKeyMeta();

	// ---- 값 검증 ----
	/** 각 항목을 허용 범위로 고정한다. 방위는 0~360으로 정규화(래핑)한다. */
	UFUNCTION(BlueprintCallable, Category = "Light")
	static void ClampSettings(UPARAM(ref) FLightSettings& S);

	// ---- 직렬화 ----
	/** 설정 → JSON 문자열. */
	static FString ToJson(const FLightSettings& S);

	/** JSON 문자열 → 설정. 파싱 실패 시 false 를 반환하고 Out 을 건드리지 않는다. */
	static bool FromJson(const FString& Json, FLightSettings& Out);

	// ---- 시간대(보드 #1326) ----
	/**
	 * 시각 → 태양(기본맵 전용 단순 모델). 해 뜸 6시·짐 18시, 정오 고도 60°.
	 * 방위는 지도 북쪽 = UE +X 로 가정한다: 해는 6시 동(+Y)·12시 남(−X)·18시 서(−Y)에 있고,
	 * 반환 방위는 sunAzimuthDeg 규약(빛이 나아가는 방향의 UE yaw)이다 — 6시 270°, 12시 0°, 18시 90°.
	 * 광량은 고도 0→20° 에서 0→RefIntensity, 색은 고도 0→25° 에서 주황→흰색. 해가 지평선 아래면 bOutNight=true, 광량 0, 고도 0.
	 */
	static void SunFromTimeOfDay(float Hour, float RefIntensity, float& OutAltitudeDeg, float& OutAzimuthDeg,
		float& OutIntensity, FLinearColor& OutColor, bool& bOutNight);

	// ---- 파일 입출력 ----
	static bool SaveToFile(const FString& Path, const FLightSettings& S);
	static bool LoadFromFile(const FString& Path, FLightSettings& Out);
	/**
	 * 시간대·날씨(FLightEnv)를 같은 파일에 선택 키로 함께 쓴다/읽는다(TimeOfDay·NightAmbient·Fog·CloudCoverage·Rain·Wetness).
	 * 읽을 때 그 키가 하나도 없으면 bOutHasEnv=false(옛 파일 — 시간대·날씨는 건드리지 않는다).
	 */
	static bool SaveToFile(const FString& Path, const FLightSettings& S, const FLightEnv& Env);
	static bool LoadFromFile(const FString& Path, FLightSettings& Out, FLightEnv& OutEnv, bool& bOutHasEnv);

	/** Save/3D/Light 디렉터리 절대 경로. */
	static FString GetLightDir();

	/** 저장 다이얼로그 기본 파일 경로(Save/3D/Light/LightSettings.json). */
	static FString GetSuggestedFilePath();

	// ---- 기본값 포인터 ----
	/** 마지막으로 저장·열기한 파일명을 담는 포인터 파일 경로. */
	static FString GetDefaultPointerPath();

	/** 포인터가 가리키는 파일의 이름만(없으면 빈 문자열). 파일 존재 여부는 보지 않는다. */
	static FString GetDefaultFileName();

	/** 포인터 파일에 대상 설정 파일 경로를 기록한다. */
	static bool SetDefaultFile(const FString& SettingsPath);

	/** 포인터가 가리키는 파일을 읽어 설정을 돌려준다. 어느 단계든 실패하면 false. */
	static bool LoadDefaultSettings(FLightSettings& Out);
};
