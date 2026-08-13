// Copyright Epic Games, Inc. All Rights Reserved.

#include "CarPlacementLibrary.h"
#include "UnityUnrealCoordinateConverter.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	/**
	 * 차량 배치 파일인지 판별한다.
	 *
	 * PresetMaker 파일(주차면 프리셋)도 루트 키가 똑같이 "datas" 배열이라
	 * FJsonObjectConverter 가 조용히 "성공"하고, 원소 수만큼 전 필드 기본값짜리 FCarPos 를 만든다.
	 * (프리셋 1개짜리 파일을 차량 열기로 고르면 "차량 1대 로드 성공"으로 보고되고
	 *  원점에 정체불명 차량 1대가 생긴다 — 실제 사용자 신고 사례.)
	 * 따라서 원소가 차량 고유 키를 갖는지 확인한다. 프리셋 원소에는 이 키들이 없다
	 * (프리셋: idx/faceCount/offsetPos/xSize/zSize/dirType/camIdx...).
	 */
	bool LooksLikeCarDatas(const FString& Json, FString& OutReason)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutReason = TEXT("JSON 파싱 실패");
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* Datas = nullptr;
		if (!Root->TryGetArrayField(TEXT("datas"), Datas) || !Datas)
		{
			OutReason = TEXT("루트에 datas 배열이 없음");
			return false;
		}
		if (Datas->Num() == 0)
		{
			return true; // 빈 목록은 유효(전체 삭제 후 저장본).
		}

		const TSharedPtr<FJsonObject>* First = nullptr;
		if (!(*Datas)[0]->TryGetObject(First) || !First || !First->IsValid())
		{
			OutReason = TEXT("datas[0] 이 객체가 아님");
			return false;
		}

		static const TCHAR* CarOnlyKeys[] = { TEXT("prefabId"), TEXT("prefabName"), TEXT("rotY"), TEXT("isFront") };
		for (const TCHAR* Key : CarOnlyKeys)
		{
			if ((*First)->HasField(Key))
			{
				return true;
			}
		}

		OutReason = TEXT("차량 항목 키(prefabId/prefabName/rotY/isFront)가 없음 — 프리셋 등 다른 종류의 파일");
		return false;
	}
}

// === 좌표 변환 ===
// legacy Unity(x,y,z; m) -> UE(z,x,y; cm). 내부 JSON은 Unreal 미터로 정규화한다.

FVector UCarPlacementLibrary::UnityPosToUE(const FCarVec3& UnityMeters, float MetersToUU)
{
	return UUnityUnrealCoordinateConverter::UnrealMetersToWorld(
		UUnityUnrealCoordinateConverter::UnityMetersToUnrealMeters(FVector(UnityMeters.x, UnityMeters.y, UnityMeters.z)), MetersToUU);
}

FCarVec3 UCarPlacementLibrary::UEToUnityPos(const FVector& UECm, float MetersToUU)
{
	const float U = (FMath::IsNearlyZero(MetersToUU)) ? 1.f : MetersToUU;
	FCarVec3 Out;
	const FVector Unity = UUnityUnrealCoordinateConverter::UnrealMetersToUnityMeters(
		UUnityUnrealCoordinateConverter::WorldToUnrealMeters(UECm, U));
	Out.x = static_cast<float>(Unity.X);
	Out.y = static_cast<float>(Unity.Y);
	Out.z = static_cast<float>(Unity.Z);
	return Out;
}

FVector UCarPlacementLibrary::UnrealMetersToWorld(const FCarVec3& UnrealMeters, float MetersToUU)
{
	return UUnityUnrealCoordinateConverter::UnrealMetersToWorld(FVector(UnrealMeters.x, UnrealMeters.y, UnrealMeters.z), MetersToUU);
}

FCarVec3 UCarPlacementLibrary::WorldToUnrealMeters(const FVector& UECm, float MetersToUU)
{
	const FVector UnrealMeters = UUnityUnrealCoordinateConverter::WorldToUnrealMeters(UECm, MetersToUU);
	FCarVec3 Out;
	Out.x = static_cast<float>(UnrealMeters.X);
	Out.y = static_cast<float>(UnrealMeters.Y);
	Out.z = static_cast<float>(UnrealMeters.Z);
	return Out;
}

// === 회전 변환 (좌표계 변환만; 메시 forward 오프셋은 ACarActor 에서 적용) ===
float UCarPlacementLibrary::UnityRotYToUEYaw(float UnityRotY)
{
	return UnityRotY;
}

float UCarPlacementLibrary::UEYawToUnityRotY(float UEYaw)
{
	return UEYaw;
}

float UCarPlacementLibrary::AddYawDeg(float CurrentDeg, float DeltaDeg)
{
	float Result = FMath::Fmod(CurrentDeg + DeltaDeg, 360.f);
	if (Result < 0.f)
	{
		Result += 360.f;
	}
	return Result;
}

TArray<int32> UCarPlacementLibrary::ToggleSelection(const TArray<int32>& Current, int32 ItemCount, int32 Index)
{
	// 유효 범위 밖 항목은 결과에서 제외한다(데이터 삭제 후 남은 인덱스 정리 겸용).
	TArray<int32> Out = Current.FilterByPredicate([ItemCount](int32 Value)
	{
		return FMath::IsWithinInclusive(Value, 0, ItemCount - 1);
	});

	if (FMath::IsWithinInclusive(Index, 0, ItemCount - 1) && Out.Remove(Index) == 0)
	{
		Out.Add(Index);
	}
	Out.Sort();
	return Out;
}

// === 자동 배치 ===
FVector UCarPlacementLibrary::AutoPlacePosition(const FVector& BaseWorld, const FVector& RightDir,
	int32 Index1Based, float SpacingMeters, bool bVertical, float MetersToUU)
{
	const float Dist = Index1Based * SpacingMeters * MetersToUU;
	if (bVertical)
	{
		// Unity 세로배치는 전역 +Z(전방) 고정 → UE 에서는 전역 +X 방향.
		return BaseWorld + FVector(Dist, 0.f, 0.f);
	}

	FVector Dir = RightDir;
	if (!Dir.Normalize())
	{
		Dir = FVector::ForwardVector; // 영벡터 폴백: UE +X
	}
	return BaseWorld + Dir * Dist;
}

// === 프리팹 콤보 ===
int32 UCarPlacementLibrary::PrefabIdFromComboIndex(const TArray<FCarPresetEntry>& Catalog, int32 ComboIndex, int32 FallbackId)
{
	return Catalog.IsValidIndex(ComboIndex) ? Catalog[ComboIndex].Idx : FallbackId;
}

FString UCarPlacementLibrary::PrefabNameFromId(const TArray<FCarPresetEntry>& Catalog, int32 PrefabId)
{
	for (const FCarPresetEntry& E : Catalog)
	{
		if (E.Idx == PrefabId)
		{
			return E.PrefabName;
		}
	}
	return FString();
}

int32 UCarPlacementLibrary::NormalizeCarPrefabs(const TArray<FCarPresetEntry>& Catalog, FCarPosDatas& Data)
{
	int32 Unresolved = 0;
	for (FCarPos& Pos : Data.datas)
	{
		// 1) 이름 우선 — 카탈로그가 재정렬되거나 Idx 가 바뀌어도 여기서 prefabId 를 교정한다.
		if (!Pos.prefabName.IsEmpty())
		{
			const FCarPresetEntry* Hit = Catalog.FindByPredicate(
				[&Pos](const FCarPresetEntry& E) { return E.PrefabName == Pos.prefabName; });
			if (Hit)
			{
				Pos.prefabId = Hit->Idx;
				continue;
			}
		}

		// 2) 이름이 없거나(구 파일) 못 찾으면 prefabId 로 해석하고 이름을 백필한다.
		const FString Name = PrefabNameFromId(Catalog, Pos.prefabId);
		if (!Name.IsEmpty())
		{
			Pos.prefabName = Name;
			continue;
		}

		// 3) 둘 다 실패 — 값을 건드리지 않고(스폰 단계 폴백에 맡김) 건수만 센다.
		++Unresolved;
	}
	return Unresolved;
}

// === ID 생성 ===
FString UCarPlacementLibrary::MakeCarId(int32 Index)
{
	const FDateTime Now = FDateTime::Now();
	return MakeCarIdFromParts(Index, Now.GetHour(), Now.GetMinute(), Now.GetSecond());
}

FString UCarPlacementLibrary::MakeCarIdFromParts(int32 Index, int32 Hour, int32 Minute, int32 Second)
{
	// Unity 포맷 "{idx}-{HH.mm.ss}" (2자리 영점 채움).
	return FString::Printf(TEXT("%d-%02d.%02d.%02d"), Index, Hour, Minute, Second);
}

// === 타입 이름 (Unity GetCarTypeName) ===
FString UCarPlacementLibrary::GetCarTypeName(ECarType Type)
{
	switch (Type)
	{
	case ECarType::Small:  return TEXT("소형차");
	case ECarType::Medium: return TEXT("중형차");
	case ECarType::Large:  return TEXT("대형차");
	case ECarType::Suv:    return TEXT("SUV");
	case ECarType::Bongo:  return TEXT("봉고차");
	case ECarType::Truck:  return TEXT("트럭");
	case ECarType::None:
	default:               return TEXT("None");
	}
}

FString UCarPlacementLibrary::GetRandomResetModeName(ERandomResetMode Mode)
{
	switch (Mode)
	{
	case ERandomResetMode::ColorOnly:           return TEXT("색상만 랜덤");
	case ERandomResetMode::CountObjectAndColor: return TEXT("개수 + 객체 + 색상");
	case ERandomResetMode::ObjectAndColor:
	default:                                    return TEXT("객체 + 색상");
	}
}

bool UCarPlacementLibrary::ParseRandomResetMode(const FString& In, ERandomResetMode& OutMode)
{
	const FString M = In.TrimStartAndEnd().ToLower();

	if (M == TEXT("coloronly") || M == TEXT("color") || M == TEXT("0"))
	{
		OutMode = ERandomResetMode::ColorOnly;
		return true;
	}
	// 빈 문자열 = 기본 모드(ObjectAndColor). 기존 car.resetRandom 계약을 그대로 유지한다.
	if (M.IsEmpty() || M == TEXT("objectandcolor") || M == TEXT("objectcolor") || M == TEXT("1"))
	{
		OutMode = ERandomResetMode::ObjectAndColor;
		return true;
	}
	if (M == TEXT("countobjectandcolor") || M == TEXT("countobjectcolor") || M == TEXT("2"))
	{
		OutMode = ERandomResetMode::CountObjectAndColor;
		return true;
	}
	return false;
}

// === JSON 입출력 (PresetMakerWidget::Save/LoadToJsonFile 와 동일 패턴) ===
bool UCarPlacementLibrary::SaveCarDatasToJson(const FString& FilePath, const FCarPosDatas& Data)
{
	FCarPosDatas UnrealData = Data;
	UnrealData.isUnreal = true;
	FString Json;
	if (!FJsonObjectConverter::UStructToJsonObjectString(UnrealData, Json))
	{
		UE_LOG(LogTemp, Warning, TEXT("[CarPlacement] JSON 직렬화 실패"));
		return false;
	}

	// 인코딩을 명시하지 않으면 AutoDetect 가 비-ASCII 한 글자에도 UTF-16 으로 쓴다.
	if (!FFileHelper::SaveStringToFile(Json, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogTemp, Warning, TEXT("[CarPlacement] 파일 쓰기 실패: %s"), *FilePath);
		return false;
	}
	UE_LOG(LogTemp, Log, TEXT("[CarPlacement] 저장(UE 좌표) %d개 → %s"), UnrealData.datas.Num(), *FilePath);
	return true;
}

bool UCarPlacementLibrary::LoadCarDatasFromJson(const FString& FilePath, FCarPosDatas& OutData)
{
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *FilePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("[CarPlacement] 파일 읽기 실패: %s"), *FilePath);
		return false;
	}

	// 역직렬화 전에 종류를 확인한다. 키 이름이 겹치는 다른 파일도 "성공"해 버리기 때문이다.
	FString Reason;
	if (!LooksLikeCarDatas(Json, Reason))
	{
		UE_LOG(LogTemp, Warning, TEXT("[CarPlacement] 차량 배치 파일이 아님(%s): %s"), *Reason, *FilePath);
		return false;
	}

	OutData = FCarPosDatas();
	if (!FJsonObjectConverter::JsonObjectStringToUStruct(Json, &OutData, 0, 0))
	{
		UE_LOG(LogTemp, Warning, TEXT("[CarPlacement] JSON 역직렬화 실패"));
		return false;
	}
	const bool bSourceIsUnreal = OutData.isUnreal;
	if (!bSourceIsUnreal)
	{
		for (FCarPos& Pos : OutData.datas)
		{
			const FVector UnrealMeters = UUnityUnrealCoordinateConverter::UnityMetersToUnrealMeters(FVector(Pos.pos.x, Pos.pos.y, Pos.pos.z));
			Pos.pos = { static_cast<float>(UnrealMeters.X), static_cast<float>(UnrealMeters.Y), static_cast<float>(UnrealMeters.Z) };
		}
	}
	OutData.isUnreal = true;
	UE_LOG(LogTemp, Log, TEXT("[CarPlacement] 로드 %d개 ← %s (%s)"), OutData.datas.Num(), *FilePath,
		bSourceIsUnreal ? TEXT("UE 그대로") : TEXT("Unity→UE 정규화"));
	return true;
}
