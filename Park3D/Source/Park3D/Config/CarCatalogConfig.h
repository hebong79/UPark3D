// Copyright Epic Games, Inc. All Rights Reserved.
// CarCatalogConfig : 차량 프리팹 인덱스 파일(Save/Config/car_catalog.json) 파싱·적용.
// DT_CarCatalog 는 "어떤 차종이 있고 어떤 메시를 쓰는가"의 단일 진실원으로 두고,
// 이 파일은 "prefabId 몇 번이 어느 차종인가"(순서)만 결정한다 — 순서를 바꾸려고 바이너리 에셋을
// 열거나 재쿠킹하지 않기 위함이다. 파일이 없으면 DataTable 순서를 그대로 쓴다.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "../ParkingCarTypes.h"
#include "CarCatalogConfig.generated.h"

UCLASS()
class PARK3D_API UCarCatalogConfigLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	static const TCHAR* GetFileName() { return TEXT("car_catalog.json"); }

	/** Save/Config/car_catalog.json 절대 경로(패키지에서는 스테이지 루트 기준). */
	UFUNCTION(BlueprintCallable, Category = "Car|Config")
	static FString GetFilePath();

	/** JSON 문자열 → 이름 순서 배열. cars 배열이 없거나 파싱 실패면 false(Out 미변경). */
	static bool ParseOrder(const FString& Json, TArray<FString>& OutNames);

	/** 파일에서 순서를 읽는다. 파일 없음/파싱 실패면 false. */
	UFUNCTION(BlueprintCallable, Category = "Car|Config")
	static bool LoadOrder(TArray<FString>& OutNames);

	/** JSON 문자열 → 차량 메시 폴더("meshDir"). 키가 없거나 비면 false(Out 미변경). */
	static bool ParseMeshDir(const FString& Json, FString& OutDir);

	/** 파일에서 메시 폴더를 읽는다. 파일 없음/키 없음이면 false. */
	static bool LoadMeshDir(FString& OutDir);

	/** 시작 시 1회 로드해 캐시하는 메시 폴더(GetCachedOrder 와 같은 관례). */
	static const FString& GetCachedMeshDir();

	/**
	 * cars 이름과 meshDir 로 카탈로그를 직접 구성한다 — DT_CarCatalog 가 없을 때의 폴백.
	 * 메시 경로 규칙은 "<meshDir>/<이름>.<이름>"(언리얼 에셋은 패키지명과 오브젝트명이 같다).
	 * meshDir 이나 cars 가 없으면 빈 배열을 돌려준다.
	 * Type 은 이 파일이 갖고 있지 않으므로 기본값(Medium)이다 — 분류는 UI 표시에만 쓰인다.
	 */
	static TArray<FCarPresetEntry> BuildCatalogFromConfig();

	/**
	 * 카탈로그를 Order 순서로 재배열하고 Idx 를 1부터 다시 매긴다.
	 * - Order 에 있는 이름부터 그 순서대로 앞에 놓는다.
	 * - Order 에 없는 항목은 조용히 사라지지 않게 원래 순서 그대로 뒤에 이어 붙인다.
	 * - Order 에만 있고 카탈로그에 없는 이름은 건너뛴다(경고 로그).
	 * - Order 가 비어 있으면 아무것도 하지 않는다(Idx 도 건드리지 않는다).
	 */
	static void ApplyOrder(const TArray<FString>& Order, TArray<FCarPresetEntry>& InOut);

	/**
	 * 시작 시 1회 로드해 캐시하는 순서. 최초 호출 시 파일을 읽는다.
	 * CatalogFromTable 이 매번 파일을 읽지 않도록 하는 캐시이며, 앱 시작 시
	 * APark3DGameMode::ApplyStartupConfig 가 먼저 채워 로그를 남긴다.
	 */
	static const TArray<FString>& GetCachedOrder();

	/** 캐시를 버린다(파일을 고친 뒤 재적용, 테스트 격리용). */
	static void InvalidateCache();
};
