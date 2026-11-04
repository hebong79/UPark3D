// Copyright Epic Games, Inc. All Rights Reserved.
// ParkAgentOutfit : 사람 옷의 값(이름표). 칠하는 일은 AParkAgentActor::ApplyOutfit 과 재질 M_AgentOutfit 이 한다.
//   같은 seed = id·생성 순서와 무관하게 같은 옷. agent.list 가 그대로 돌려주고, 스냅샷은 이 값을 그대로 복원한다.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;

struct FParkAgentOutfit
{
	int32 Seed = 0;
	FString Sleeve;       // long | short | none
	FString Bottom;       // long | shorts
	FString Pattern;      // plain | stripe | check
	FString Top;
	FString TopAlt;       // 무늬의 둘째 색
	FString BottomColor;
	FString Shoes;
	FString Skin;
	FString Hair;

	bool operator==(const FParkAgentOutfit& O) const
	{
		return Seed == O.Seed && Sleeve == O.Sleeve && Bottom == O.Bottom && Pattern == O.Pattern && Top == O.Top && TopAlt == O.TopAlt
			&& BottomColor == O.BottomColor && Shoes == O.Shoes && Skin == O.Skin && Hair == O.Hair;
	}
};

namespace ParkAgentOutfit
{
	/** 새 seed(1 이상). */
	int32 NewSeed();
	/** seed 로 옷 전부를 뽑는다. */
	FParkAgentOutfit Draw(int32 Seed);
	/**
	 * seed 로 뽑은 뒤 Over 의 값을 덮는다. Seed ≤ 0 이면 Over.seed, 그것도 없으면 새 seed.
	 * Over 는 agent.list 의 outfit 을 그대로 줘도 된다(applied 는 무시).
	 * @return 모르는 키·이름이면 false + OutError(고를 수 있는 이름 포함).
	 */
	bool Resolve(const TSharedPtr<FJsonObject>& Over, int32 Seed, FParkAgentOutfit& Out, FString& OutError);
	TSharedPtr<FJsonObject> ToJson(const FParkAgentOutfit& Outfit);

	/** 이름 → 재질에 넣을 선형 색. 표에 없으면 회색. */
	FLinearColor TopColor(const FString& Name);
	FLinearColor BottomColor(const FString& Name);
	FLinearColor ShoesColor(const FString& Name);
	FLinearColor SkinColor(const FString& Name);
	FLinearColor HairColor(const FString& Name);
}
