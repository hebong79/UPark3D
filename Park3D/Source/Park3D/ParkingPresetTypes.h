// Copyright Epic Games, Inc. All Rights Reserved.
// ParkingPresetTypes : 주차 프리셋 데이터 모델 + 기하 계산 결과 타입.
// Unity CPresetMaker 포팅 (SDPresetInfo / EFaceDirType / CParkingGeometry 결과 / SPSpaceAssignment).

#pragma once

#include "CoreMinimal.h"
#include "ParkingPresetTypes.generated.h"

/** 주차면 배치 방향 타입 (Unity EFaceDirType). */
UENUM(BlueprintType)
enum class EFaceDirType : uint8
{
	Default UMETA(DisplayName = "Default"),  // 월드축 정렬 배치(가로 줄)
	Dir     UMETA(DisplayName = "Dir"),      // 면 로컬 방향으로 대각선 배치
};

/**
 * 주차장 프리셋 한 개(= 카메라 한 시야에 들어가는 연속 주차면 한 줄). Unity SDPresetInfo 대응.
 * 카메라는 CameraIdx(번호)만 사용한다. (camPos/camRot/fov 포즈는 포팅 제외)
 */
USTRUCT(BlueprintType)
struct FParkingPreset
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preset")
	int32 PresetIdx = 1;                 // 1부터, 0=미설정

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preset")
	FString PresetName = TEXT("Preset 1");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preset")
	int32 FaceCount = 7;                  // 주차면 개수 N

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preset")
	FVector Offset = FVector::ZeroVector; // 시작 위치(미터)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preset")
	float FaceRotate = 0.f;              // 개별 주차면 회전(사선각, deg)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preset")
	float GroupFaceRotate = 0.f;         // 그룹 전체 회전(deg)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preset")
	float BoxSizeX = 2.5f;               // 박스 가로(폭, m) — Unity xSize

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preset")
	float BoxSizeZ = 5.0f;               // 박스 세로(길이, m) — Unity zSize

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preset")
	EFaceDirType DirType = EFaceDirType::Default;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preset")
	bool bIsBaseWidth = true;            // true=BoxSizeX가 폭, false=BoxSizeZ가 폭

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preset")
	int32 CameraIdx = 1;                 // 카메라 번호(주차면 번호 할당 1순위 키)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preset")
	bool bUse3D = false;
};

/** 프리셋 목록 JSON 루트 (Unity SDPresetDatas { datas }). 저장/열기 직렬화 단위. */
USTRUCT(BlueprintType)
struct FParkingPresetDatas
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preset")
	TArray<FParkingPreset> Datas;
};

// ─────────────────────────────────────────────────────────────
// Unity 스키마 직렬화 DTO (참조: Save/3D/Preset/*.json = Unity SDPresetInfo).
// 멤버명(소문자 시작)이 곧 JSON 키가 되도록 두어 Unity 키와 정확히 일치시킨다
// (idx/offsetPos/faceRot/groupRot/xSize/zSize/useBaseWidth/camIdx). 도메인 FParkingPreset 과 매핑.
// ─────────────────────────────────────────────────────────────

/** Unity offsetPos(m). JSON 키 x/y/z. */
USTRUCT()
struct FPresetVec3
{
	GENERATED_BODY()
	UPROPERTY() float x = 0.f;
	UPROPERTY() float y = 0.f;
	UPROPERTY() float z = 0.f;
};

/** Unity SDPresetInfo 직렬화 DTO. JSON 키 = Unity. use3D 는 Unity 파일에 없는 확장 키(데이터 보존용). */
USTRUCT()
struct FParkingPresetDTO
{
	GENERATED_BODY()
	UPROPERTY() int32 idx = 1;
	UPROPERTY() FString presetName = TEXT("Preset 1");
	UPROPERTY() int32 faceCount = 7;
	UPROPERTY() FPresetVec3 offsetPos;
	UPROPERTY() float faceRot = 0.f;
	UPROPERTY() float groupRot = 0.f;
	UPROPERTY() float xSize = 2.5f;
	UPROPERTY() float zSize = 5.0f;
	UPROPERTY() int32 dirType = 0;      // 0=Default, 1=Dir
	UPROPERTY() bool useBaseWidth = true;
	UPROPERTY() int32 camIdx = 1;
	UPROPERTY() bool use3D = false;     // Unity 파일엔 없음 → 없으면 기본값(false)으로 로드
};

/** Unity SDPresetDatas 직렬화 DTO 루트({ datas }). */
USTRUCT()
struct FParkingPresetDTOList
{
	GENERATED_BODY()
	/** true이면 datas의 offsetPos가 이미 Unreal 미터 좌표다. 누락/false는 Unity legacy 형식. */
	UPROPERTY() bool isUnreal = false;
	UPROPERTY() TArray<FParkingPresetDTO> datas;
};

/** 단일 주차면 회전 치수 (Unity SingleParkingResult). 단위는 입력과 동일(m). */
USTRUCT(BlueprintType)
struct FSingleParkingResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Parking") float BoxWidth = 0.f;    // 전체 수평 폭(bounding box)
	UPROPERTY(BlueprintReadOnly, Category = "Parking") float LeftSlopeW = 0.f;  // 중심선→왼쪽 끝
	UPROPERTY(BlueprintReadOnly, Category = "Parking") float RightSlopeW = 0.f; // 중심선→오른쪽 끝
	UPROPERTY(BlueprintReadOnly, Category = "Parking") float BoxHeight = 0.f;   // 전체 수직 높이
};

/** 복수 주차면 배열 치수 (Unity MultiParkingResult). */
USTRUCT(BlueprintType)
struct FMultiParkingResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Parking") float TotWidth = 0.f;     // 전체 배열 수평 길이
	UPROPERTY(BlueprintReadOnly, Category = "Parking") float OneBoxW = 0.f;      // box 1개 수평 폭
	UPROPERTY(BlueprintReadOnly, Category = "Parking") float SlopHoriDist = 0.f; // 시작선→첫 중심
	UPROPERTY(BlueprintReadOnly, Category = "Parking") float StepW = 0.f;        // 인접 면 중심 간격
	UPROPERTY(BlueprintReadOnly, Category = "Parking") float OneH = 0.f;         // 배열 수직 높이
};

/** 주차면 번호 할당 결과 (Unity SPSpaceAssignment). */
USTRUCT(BlueprintType)
struct FParkingSpaceAssignment
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Parking") int32 CamIdx = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Parking") int32 PresetIdx = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Parking") int32 StartFaceNum = 0; // 1-based
	UPROPERTY(BlueprintReadOnly, Category = "Parking") int32 FaceCount = 0;

	/** 마지막 주차면 번호. */
	int32 EndFaceNum() const { return StartFaceNum + FaceCount - 1; }
};
