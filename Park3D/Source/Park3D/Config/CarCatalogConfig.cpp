// Copyright Epic Games, Inc. All Rights Reserved.

#include "CarCatalogConfig.h"
#include "Park3DAppConfig.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	// 캐시된 순서와 로드 여부. 파일이 없어도(빈 배열) 재시도하지 않도록 플래그를 따로 둔다.
	TArray<FString> GCachedOrder;
	bool GOrderLoaded = false;
}

FString UCarCatalogConfigLibrary::GetFilePath()
{
	return FPaths::Combine(UPark3DAppConfigLibrary::GetConfigDir(), GetFileName());
}

bool UCarCatalogConfigLibrary::ParseOrder(const FString& Json, TArray<FString>& OutNames)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Cars = nullptr;
	if (!Root->TryGetArrayField(TEXT("cars"), Cars) || !Cars)
	{
		return false;
	}

	// 부분 실패로 반쯤 채워진 목록을 만들지 않는다(config_pmaker.json 선례).
	TArray<FString> Parsed;
	for (const TSharedPtr<FJsonValue>& V : *Cars)
	{
		FString Name;
		if (V.IsValid() && V->TryGetString(Name))
		{
			Name = Name.TrimStartAndEnd();
			if (!Name.IsEmpty())
			{
				Parsed.Add(Name);
			}
		}
	}

	OutNames = MoveTemp(Parsed);
	return true;
}

bool UCarCatalogConfigLibrary::LoadOrder(TArray<FString>& OutNames)
{
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *GetFilePath()))
	{
		return false;
	}
	return ParseOrder(Json, OutNames);
}

void UCarCatalogConfigLibrary::ApplyOrder(const TArray<FString>& Order, TArray<FCarPresetEntry>& InOut)
{
	if (Order.Num() == 0 || InOut.Num() == 0)
	{
		return;
	}

	TArray<FCarPresetEntry> Sorted;
	Sorted.Reserve(InOut.Num());
	TArray<bool> Taken;
	Taken.Init(false, InOut.Num());

	for (const FString& Name : Order)
	{
		// 같은 이름이 두 번 적혀도 한 항목이 두 번 들어가지 않도록 Taken 으로 소모 처리한다.
		int32 Idx = INDEX_NONE;
		for (int32 i = 0; i < InOut.Num(); ++i)
		{
			if (!Taken[i] && InOut[i].PrefabName == Name)
			{
				Idx = i;
				break;
			}
		}

		if (Idx == INDEX_NONE)
		{
			UE_LOG(LogTemp, Warning, TEXT("[CarCatalog] 목록의 \"%s\" 가 DT_CarCatalog 에 없습니다 — 건너뜁니다."), *Name);
			continue;
		}
		Taken[Idx] = true;
		Sorted.Add(InOut[Idx]);
	}

	// 목록에 없던 차종은 버리지 않고 원래 순서로 뒤에 이어 붙인다.
	for (int32 i = 0; i < InOut.Num(); ++i)
	{
		if (!Taken[i])
		{
			UE_LOG(LogTemp, Warning, TEXT("[CarCatalog] \"%s\" 가 %s 에 없습니다 — 뒤에 이어 붙입니다(prefabId=%d)."),
				*InOut[i].PrefabName, GetFileName(), Sorted.Num() + 1);
			Sorted.Add(InOut[i]);
		}
	}

	// prefabId 는 1부터. 배열 위치가 곧 id 다.
	for (int32 i = 0; i < Sorted.Num(); ++i)
	{
		Sorted[i].Idx = i + 1;
	}

	InOut = MoveTemp(Sorted);
}

const TArray<FString>& UCarCatalogConfigLibrary::GetCachedOrder()
{
	if (!GOrderLoaded)
	{
		GOrderLoaded = true;
		if (!LoadOrder(GCachedOrder))
		{
			GCachedOrder.Reset();
			UE_LOG(LogTemp, Log, TEXT("[CarCatalog] 인덱스 파일 없음/읽기 실패 — DT_CarCatalog 순서를 그대로 씁니다: %s"),
				*GetFilePath());
		}
	}
	return GCachedOrder;
}

void UCarCatalogConfigLibrary::InvalidateCache()
{
	GCachedOrder.Reset();
	GOrderLoaded = false;
}
