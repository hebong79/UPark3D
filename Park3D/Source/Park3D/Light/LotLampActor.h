// Copyright Epic Games, Inc. All Rights Reserved.
// LotLampActor : 주차장 가로등 1개(보드 #1326 B2.1). 기둥(엔진 Cylinder) + 등기구(Cube) + 아래로 비추는 SpotLight.
// 레벨에 가로등 액터가 없어 RPC(lamp.create)로 만들고, 레벨별 파일(Save/3D/Lamp/Lamp_<레벨>.json)에 남겨
// 기동·레벨 전환 때 GameMode 가 다시 세운다(lot.* 와 같은 규약).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Dom/JsonObject.h"
#include "LotLampActor.generated.h"

class UStaticMeshComponent;
class USpotLightComponent;

UCLASS()
class PARK3D_API ALotLampActor : public AActor
{
	GENERATED_BODY()

public:
	ALotLampActor();

	/** 기본값 — lamp.create 가 생략된 키에 쓴다. 광량 단위는 칸델라(이 프로젝트 태양 5 lux 기준으로 맞춘 값). */
	static constexpr float DefaultHeightM = 6.0f;
	static constexpr float DefaultIntensityCd = 150.0f;
	static constexpr float DefaultConeDeg = 70.0f;

	/** 이름·높이·광량·색·원뿔각을 넣고 형태를 다시 맞춘다. 위치(지면 점)는 액터 위치다. */
	void Configure(const FString& InName, float InHeightM, float InIntensityCd, const FLinearColor& InColor, float InConeDeg, bool bInOn);
	void SetOn(bool bInOn);
	void SetIntensity(float InIntensityCd);
	void SetColor(const FLinearColor& InColor);

	const FString& GetLampName() const { return LampName; }
	float GetHeightM() const { return HeightM; }
	float GetIntensity() const { return IntensityCd; }
	const FLinearColor& GetColor() const { return Color; }
	float GetConeDeg() const { return ConeDeg; }
	bool IsOn() const { return bOn; }

	/** {name, pos{x,y,z} m(지면 점), height, intensity, color{x,y,z}, coneDeg, on}. */
	TSharedPtr<FJsonObject> ToJson() const;

	// ---- 월드 단위 조회·저장 ----
	static TArray<ALotLampActor*> GetAll(UWorld* World);
	static ALotLampActor* FindByName(UWorld* World, const FString& Name);
	/** 이름이 없거나 겹치면 Lamp_<n> 으로 새로 짓는다. */
	static FString MakeUniqueName(UWorld* World, const FString& Wanted);
	static ALotLampActor* Spawn(UWorld* World, const FVector& GroundCm, const FString& Name, float HeightM, float IntensityCd,
		const FLinearColor& Color, float ConeDeg, bool bOn);
	static FString GetFilePath(const UWorld* World);
	/** 지금 월드의 가로등 전부를 레벨 파일에 쓴다(0개면 파일을 지운다). */
	static bool SaveAll(UWorld* World, FString& OutPath);
	/** 레벨 파일을 읽어 가로등을 세운다(기존 가로등은 지운다). 세운 개수(파일 없으면 0). */
	static int32 LoadForWorld(UWorld* World);

private:
	void ApplyShape();

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> PoleMesh;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> HeadMesh;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USpotLightComponent> Spot;

	FString LampName;
	float HeightM = DefaultHeightM;
	float IntensityCd = DefaultIntensityCd;
	FLinearColor Color = FLinearColor(1.0f, 0.85f, 0.65f);
	float ConeDeg = DefaultConeDeg;
	bool bOn = true;
};
