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

	// ---- 값 검증 ----
	/** 각 항목을 허용 범위로 고정한다. 방위는 0~360으로 정규화(래핑)한다. */
	UFUNCTION(BlueprintCallable, Category = "Light")
	static void ClampSettings(UPARAM(ref) FLightSettings& S);

	// ---- 직렬화 ----
	/** 설정 → JSON 문자열. */
	static FString ToJson(const FLightSettings& S);

	/** JSON 문자열 → 설정. 파싱 실패 시 false 를 반환하고 Out 을 건드리지 않는다. */
	static bool FromJson(const FString& Json, FLightSettings& Out);

	// ---- 파일 입출력 ----
	static bool SaveToFile(const FString& Path, const FLightSettings& S);
	static bool LoadFromFile(const FString& Path, FLightSettings& Out);

	/** Save/3D/Light 디렉터리 절대 경로. */
	static FString GetLightDir();

	/** 저장 다이얼로그 기본 파일 경로(Save/3D/Light/LightSettings.json). */
	static FString GetSuggestedFilePath();

	// ---- 기본값 포인터 ----
	/** 마지막으로 저장·열기한 파일명을 담는 포인터 파일 경로. */
	static FString GetDefaultPointerPath();

	/** 포인터 파일에 대상 설정 파일 경로를 기록한다. */
	static bool SetDefaultFile(const FString& SettingsPath);

	/** 포인터가 가리키는 파일을 읽어 설정을 돌려준다. 어느 단계든 실패하면 false. */
	static bool LoadDefaultSettings(FLightSettings& Out);
};
