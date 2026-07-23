// Copyright Epic Games, Inc. All Rights Reserved.
// ParkingCarTypes : 차량 배치 데이터 모델 + 메시 카탈로그 타입.
// Unity CarObject 포팅 (SObjectPos / SVector3 / ECarType / EKeyType / CScoCarData.SCarData).
// JSON 스키마는 Unity CSavePolePosData.cs 의 SObjectPos(8필드, 소문자 키)와 100% 호환되어야 한다.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "ParkingCarTypes.generated.h"

/** 차량 타입 (Unity ECarType). 0=None=미설정. JSON type 정수에 직접 매핑. */
UENUM(BlueprintType)
enum class ECarType : uint8
{
	None   = 0 UMETA(DisplayName = "None"),
	Small  = 1 UMETA(DisplayName = "Small"),
	Medium = 2 UMETA(DisplayName = "Medium"),
	Large  = 3 UMETA(DisplayName = "Large"),
	Suv    = 4 UMETA(DisplayName = "SUV"),
	Bongo  = 5 UMETA(DisplayName = "Bongo"),
	Truck  = 6 UMETA(DisplayName = "Truck"),
};

/** 차량 도색 색상 (UI 프리셋 색상 선택용). */
UENUM(BlueprintType)
enum class ECarColor : uint8
{
	White  = 0,
	Black,
	Silver,
	Gray,
	Red,
	Blue,
	Green,
	Yellow,
	Orange,
	Purple,
};

/** 이동/회전 키 모드 (Unity EKeyType). None=0. */
UENUM(BlueprintType)
enum class ECarMoveMode : uint8
{
	None   = 0,
	Move,
	Rotate,
};

/**
 * === Unity SVector3 (JSON pos) — 소문자 키 강제 ===
 * FVector 직접 사용 시 UE 직렬화가 대문자 X/Y/Z가 되어 Unity 비호환(좌표 손실).
 * 멤버명을 소문자 시작 x/y/z 로 두어 FJsonObjectConverter 직렬화 키를 Unity와 일치시킨다.
 * 좌표계는 Unity(왼손, Y-up, m): x=right, y=up(높이), z=forward.
 */
USTRUCT(BlueprintType)
struct FCarVec3
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Car") float x = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Car") float y = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Car") float z = 0.f;
};

/**
 * === Unity SObjectPos (CarPos JSON 1개 항목, 8필드) ===
 * 멤버명을 소문자 시작으로 두어 JSON 키(id/type/presetId/slotId/prefabId/pos/rotY/isFront)가 Unity와 일치.
 * 내부 pos는 Unreal 미터 좌표다. isUnreal 없는 legacy JSON만 로드 경계에서 Unity 좌표로 판정해 변환한다.
 */
USTRUCT(BlueprintType)
struct FCarPos
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Car") FString  id;            // "idx-HH.mm.ss"
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Car") int32    type = 0;      // = ECarType 정수(0=None=미설정)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Car") int32    presetId = 1;  // 프리셋 그룹 id (1부터)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Car") int32    slotId = 1;    // 주차면 슬롯 (1부터, -1=슬롯 외)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Car") int32    prefabId = 1;  // 차량 메시 id (1부터, 카탈로그 Idx 와 동일 공간)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Car") FCarVec3 pos;           // Unity 좌표(Y-up, m)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Car") float    rotY = 0.f;    // Y축(Unity) 회전 deg
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Car") bool     isFront = true;
	// 확장 필드(Unity 원본 8필드 이후 추가). JSON key "color". -1=미도색(원본색), 0~=ECarColor 정수.
	// Unity 파일에 이 키가 없으면 로드 시 기본값 -1 유지(하위 호환).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Car") int32    color = -1;
	// 확장 필드. JSON key "prefabName". 카탈로그 재정렬/Idx 변경에도 살아남는 안정 키.
	// 로드 시 이 값이 우선이고, 비어있으면(구 파일) prefabId 로 해석한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Car") FString  prefabName;
};

/** === Unity SObjectPosDatas (CarPos JSON 루트) === 루트 키 datas 소문자. */
USTRUCT(BlueprintType)
struct FCarPosDatas
{
	GENERATED_BODY()

	/** true이면 datas.pos는 Unreal 미터 좌표. 누락/false는 Unity legacy 형식. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Car") bool isUnreal = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Car") TArray<FCarPos> datas;
};

/**
 * === Unity CScoCarData.SCarData (메시 카탈로그) ===
 * 데이터테이블(DT_CarCatalog) 행으로 사용 가능하도록 FTableRowBase 상속.
 */
USTRUCT(BlueprintType)
struct FCarPresetEntry : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Car") int32    Idx = 1;        // 1부터
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Car") ECarType Type = ECarType::Medium;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Car") FString  PrefabName;     // 표시/매칭용 이름
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Car") TSoftObjectPtr<UStaticMesh> Mesh; // 임포트된 차량 메시
};
