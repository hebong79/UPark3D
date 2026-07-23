// Copyright Epic Games, Inc. All Rights Reserved.
// CarPlacementLibrary : Unity CarObject 의 순수 계산 로직 + JSON 입출력 포팅.
// 좌표/단위 변환, 자동배치 위치 산출, 차량 id 생성, 타입 이름, FCarPosDatas JSON 저장/로드.
// 월드/UMG 의존 없음 → 단위 테스트 용이(유닛테스트 1순위).

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ParkingCarTypes.h"
#include "CarPlacementLibrary.generated.h"

UCLASS()
class PARK3D_API UCarPlacementLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// === 좌표 변환 ===
	// JSON 내부 데이터는 Unreal 미터다. Unity legacy 파일만 공통 변환기로 (z,x,y) 변환한 뒤 사용한다.

	/** Unity 좌표(m) → UE 월드 좌표(cm). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Car|Geometry")
	static FVector UnityPosToUE(const FCarVec3& UnityMeters, float MetersToUU = 100.f);

	/** UE 월드 좌표(cm) → Unity 좌표(m). UEToUnityPos(UnityPosToUE(v)) ≈ v 라운드트립 성립. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Car|Geometry")
	static FCarVec3 UEToUnityPos(const FVector& UECm, float MetersToUU = 100.f);

	/** 내부 Unreal 미터 좌표 → UE 월드 좌표(cm). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Car|Geometry")
	static FVector UnrealMetersToWorld(const FCarVec3& UnrealMeters, float MetersToUU = 100.f);

	/** UE 월드 좌표(cm) → 내부 Unreal 미터 좌표. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Car|Geometry")
	static FCarVec3 WorldToUnrealMeters(const FVector& UECm, float MetersToUU = 100.f);

	// === 회전 변환 ===
	// 좌표계 회전 변환만 담당. 메시 forward 오프셋(정면축 ±90° 보정)은 ACarActor(3단계)에서 별도 적용한다.
	// 1차 가정: UE Yaw = Unity rotY (동일). 동작확인(TP-14)에서 부호/오프셋 최종 확정.

	/** Unity Y축 회전(deg) → UE Yaw(deg). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Car|Geometry")
	static float UnityRotYToUEYaw(float UnityRotY);

	/** UE Yaw(deg) → Unity Y축 회전(deg). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Car|Geometry")
	static float UEYawToUnityRotY(float UEYaw);

	/** 각도 합산 + [0,360) 정규화. (+90° 버튼/휠 회전 공용) */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Car|Geometry")
	static float AddYawDeg(float CurrentDeg, float DeltaDeg);

	/**
	 * Shift 토글 다중 선택. Current 에 Index 가 있으면 제거, 없으면 추가한다.
	 * 프리셋·리스트 연속성과 무관하게 임의의 차량을 골라 담을 수 있다.
	 * 결과는 항상 오름차순이며 [0, ItemCount-1] 범위 밖 인덱스는 모두 걸러낸다.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Car|Selection")
	static TArray<int32> ToggleSelection(const TArray<int32>& Current, int32 ItemCount, int32 Index);

	// === 자동 배치 ===
	/**
	 * i번째(1-based) 차량의 UE 월드 위치 산출 (Unity CreateRandomCarsInLine 위치식 포팅).
	 * 가로(bVertical=false): BaseWorld + RightDir * (Index1Based * SpacingMeters * U).
	 * 세로(bVertical=true) : BaseWorld + UE +Y(Unity z 전방) * (Index1Based * SpacingMeters * U).
	 * RightDir 은 정규화하여 사용. 영벡터면 UE +X 로 폴백.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Car|Geometry")
	static FVector AutoPlacePosition(const FVector& BaseWorld, const FVector& RightDir,
		int32 Index1Based, float SpacingMeters, bool bVertical, float MetersToUU = 100.f);

	// === 프리팹 콤보 ===
	/**
	 * 차량 프리팹 콤보 선택 인덱스 → prefabId.
	 * 콤보 항목은 카탈로그 순서 그대로 채워지므로 Catalog[ComboIndex].Idx 가 곧 prefabId 다.
	 * (Idx 는 1-based 이며 prefabId 와 동일 공간 — CatalogLookup 테스트 참고.)
	 * ComboIndex 가 범위를 벗어나면 FallbackId 를 반환한다.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Car|Data")
	static int32 PrefabIdFromComboIndex(const TArray<FCarPresetEntry>& Catalog, int32 ComboIndex, int32 FallbackId = 1);

	/** prefabId → 카탈로그 PrefabName. 없으면 빈 문자열. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Car|Data")
	static FString PrefabNameFromId(const TArray<FCarPresetEntry>& Catalog, int32 PrefabId);

	/**
	 * 로드 직후 정규화. 각 항목의 prefabId/prefabName 을 카탈로그 기준으로 맞춘다.
	 *  1) prefabName 이 카탈로그에 있으면 → prefabId 를 그 항목 Idx 로 교정(이름 우선).
	 *  2) 아니면 prefabId 가 카탈로그 Idx 와 일치하면 → prefabName 을 카탈로그 값으로 채움(구 파일 호환).
	 *  3) 둘 다 실패 → 원본 값 유지(스폰 단계 폴백에 맡김)하고 실패 건수에 집계.
	 * @return 어느 키로도 해석하지 못한 항목 수. 0보다 크면 호출부가 경고를 남긴다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Car|Data")
	static int32 NormalizeCarPrefabs(const TArray<FCarPresetEntry>& Catalog, UPARAM(ref) FCarPosDatas& Data);

	// === ID 생성 ===
	/** "idx-HH.mm.ss" id 생성 (현재 시각 기반). 내부적으로 MakeCarIdFromParts 호출. */
	UFUNCTION(BlueprintCallable, Category = "Car|Data")
	static FString MakeCarId(int32 Index);

	/** "idx-HH.mm.ss" id 생성 (시각을 명시 — 테스트 용이). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Car|Data")
	static FString MakeCarIdFromParts(int32 Index, int32 Hour, int32 Minute, int32 Second);

	// === 타입 이름 ===
	/** ECarType → 한글 표시명 (Unity GetCarTypeName): 소형차/중형차/대형차/SUV/봉고차/트럭/None. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Car|Data")
	static FString GetCarTypeName(ECarType Type);

	// === JSON 입출력 (ParkingPresetManager / PresetMakerWidget 와 동일 패턴) ===
	/** FCarPosDatas → JSON 파일 저장. 키는 소문자(datas/id/pos/x/y/z/rotY/isFront...)로 직렬화. */
	UFUNCTION(BlueprintCallable, Category = "Car|Data")
	static bool SaveCarDatasToJson(const FString& FilePath, const FCarPosDatas& Data);

	/** JSON 파일 → FCarPosDatas 로드. Unity CarPos_SNum.json 호환. */
	UFUNCTION(BlueprintCallable, Category = "Car|Data")
	static bool LoadCarDatasFromJson(const FString& FilePath, FCarPosDatas& OutData);
};
