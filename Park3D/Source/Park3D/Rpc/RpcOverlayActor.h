// Copyright Epic Games, Inc. All Rights Reserved.
// RpcOverlayActor : RPC 가 3D 화면에 덧그리는 표시 — 강조 윤곽·라벨·미리보기 고스트(보드 #1102).
// 대상 객체(차량·프리셋)를 바꾸지 않고, 목록(car.list·preset.list)과 저장 파일에도 들어가지 않는다.
// 자기 ULineBatchComponent 에 그린다 — 월드 영구 디버그 라인은 프리셋 매니저가 RefreshView 때 통째로 지운다.
// 표시는 월드 하나에 한 벌이라 모든 시청자(메인 뷰·카메라 스트림)에 똑같이 보인다(global).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RpcOverlayActor.generated.h"

class FJsonObject;
class ULineBatchComponent;
class UTextRenderComponent;

/** 미리보기 고스트 한 묶음(preview.show 의 id 하나). 좌표는 월드 cm, 회전은 yaw(도). */
struct FRpcPreviewFace
{
	FVector Center = FVector::ZeroVector;
	float YawDeg = 0.f;      // 길이축 방향
	float WidthCm = 250.f;
	float LengthCm = 500.f;
};

struct FRpcPreviewBox
{
	FVector Center = FVector::ZeroVector;
	float YawDeg = 0.f;
	FVector HalfSizeCm = FVector(100.f);
};

/** 바닥 다각형(주차장 영역 — 보드 #1168). 꼭짓점은 월드 cm, 높이는 한 장(평면). */
struct FRpcPreviewPolygon
{
	TArray<FVector2D> Points;
	float ZCm = 3.f;
	bool bFill = true;
	float Opacity = 0.25f;
	bool bLine = true;
	float LineWidthCm = 30.f;
	bool bDashed = false;
	FLinearColor Color = FLinearColor(FColor(255, 159, 28));   // #ff9f1c — SettingManager 도면 영역 색
	/** 채움 삼각형(Points 인덱스). 자기교차 등으로 귀 자르기가 못 끝나면 비어 있다. */
	TArray<int32> Triangles;

	/** 연속 중복·닫는 점을 걷어내고 Triangles 를 채운다. 점이 3개 미만이면 false. */
	bool Prepare();
};

struct FRpcPreviewSet
{
	TArray<FRpcPreviewFace> Faces;
	TArray<FRpcPreviewBox> Boxes;
	TArray<FRpcPreviewPolygon> Polygons;
	FLinearColor Color = FLinearColor(0.1f, 0.9f, 1.f, 1.f);
};

UCLASS(NotBlueprintable, NotPlaceable)
class PARK3D_API ARpcOverlayActor : public AActor
{
	GENERATED_BODY()

public:
	ARpcOverlayActor();

	/** 월드의 오버레이(없으면 스폰). bCreate=false 면 찾기만 한다. */
	static ARpcOverlayActor* Get(UWorld* World, bool bCreate = true);

	virtual void Tick(float DeltaSeconds) override;

	// ---- 강조(윤곽선) — 차량은 carNameId, 프리셋은 PresetIdx ----
	TMap<FString, FLinearColor> CarHighlights;
	TMap<int32, FLinearColor> PresetHighlights;

	// ---- 라벨 ----
	bool bLabelCars = false;
	bool bLabelPresets = false;
	bool bLabelCameras = false;

	// ---- 미리보기 ----
	TMap<FString, FRpcPreviewSet> Previews;

	// ---- 주차장 영역(lot.*) — 미리보기와 달리 레벨별 파일(Save/3D/Lot/Lot_<레벨>.json)에 남는다 ----
	TOptional<FRpcPreviewPolygon> Lot;

	/** 이 월드(레벨)의 영역 파일 경로. */
	static FString GetLotFilePath(const UWorld* World);
	/** 파일이 있으면 읽어 그린다(GameMode BeginPlay — 재기동·레벨 전환 후 복원). 파일이 없으면 아무것도 스폰하지 않는다. */
	static bool LoadLotForWorld(UWorld* World);
	/** Lot 을 파일에 쓴다(없으면 파일 삭제). */
	bool SaveLot(FString& OutPath) const;
	/** 다각형 ↔ JSON(m) — lot 파일과 lot.get 응답이 같은 모양. */
	static TSharedPtr<FJsonObject> PolygonToJson(const FRpcPreviewPolygon& Poly);
	static bool PolygonFromJson(const TSharedPtr<FJsonObject>& O, FRpcPreviewPolygon& Out);

	/** 지금 상태로 즉시 다시 그린다(RPC 가 바꾼 직후 — 다음 틱을 기다리지 않는다). */
	void Redraw();

	/** 강조·라벨·미리보기가 하나도 없으면 true(틱을 쉬게 한다). */
	bool IsEmpty() const;

private:
	UPROPERTY()
	TObjectPtr<ULineBatchComponent> Lines;

	UPROPERTY()
	TArray<TObjectPtr<UTextRenderComponent>> LabelPool;

	int32 UsedLabels = 0;
	float RedrawAccum = 0.f;

	void DrawOutlineBox(const FVector& Center, const FVector& HalfSize, const FQuat& Rot, const FLinearColor& Color, float Thickness);
	void DrawFace(const FRpcPreviewFace& F, const FLinearColor& Color);
	void DrawPolygon(const FRpcPreviewPolygon& Poly);
	void PlaceLabel(const FVector& WorldLoc, const FString& Text, const FColor& Color, const FRotator& ViewRot);
};
