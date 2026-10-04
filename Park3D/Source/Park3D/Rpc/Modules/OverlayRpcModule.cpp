// Copyright Epic Games, Inc. All Rights Reserved.

#include "OverlayRpcModule.h"
#include "../RpcDispatcher.h"
#include "../RpcParamUtil.h"
#include "../RpcOverlayActor.h"
#include "../../CarActor.h"
#include "../../ParkingPresetManager.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

namespace
{
	/** 색 — 이름(red·yellow·…), "#rrggbb", {r,g,b}(0~1). 없으면 Default. 못 읽으면 false. */
	bool OvReadColor(const TSharedPtr<FJsonObject>& P, const FLinearColor& Default, FLinearColor& Out, FRpcError& E)
	{
		Out = Default;
		if (!RpcParam::Has(P, TEXT("color"))) return true;
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (P->TryGetObjectField(TEXT("color"), Obj) && Obj && Obj->IsValid())
		{
			Out = FLinearColor(RpcParam::GetFloat(*Obj, TEXT("r"), 1.0), RpcParam::GetFloat(*Obj, TEXT("g"), 1.0), RpcParam::GetFloat(*Obj, TEXT("b"), 1.0), 1.f);
			return true;
		}
		const FString S = RpcParam::GetString(P, TEXT("color")).TrimStartAndEnd().ToLower();
		if (S.StartsWith(TEXT("#")) && S.Len() == 7)
		{
			Out = FLinearColor(FColor::FromHex(S));
			return true;
		}
		static const TPair<const TCHAR*, FLinearColor> Named[] = {
			{ TEXT("red"), FLinearColor(1.f, 0.1f, 0.1f) }, { TEXT("yellow"), FLinearColor(1.f, 0.9f, 0.1f) },
			{ TEXT("green"), FLinearColor(0.2f, 1.f, 0.3f) }, { TEXT("cyan"), FLinearColor(0.1f, 0.9f, 1.f) },
			{ TEXT("blue"), FLinearColor(0.2f, 0.4f, 1.f) }, { TEXT("magenta"), FLinearColor(1.f, 0.2f, 1.f) },
			{ TEXT("orange"), FLinearColor(1.f, 0.55f, 0.1f) }, { TEXT("white"), FLinearColor::White },
		};
		for (const auto& N : Named) { if (S == N.Key) { Out = N.Value; return true; } }
		E.FailDomain(FString::Printf(TEXT("알 수 없는 color: %s (red|yellow|green|cyan|blue|magenta|orange|white, #rrggbb, {r,g,b})"), *S),
			ERpcErrorKind::BadParams);
		return false;
	}

	TSharedPtr<FJsonObject> OvColorDto(const FLinearColor& C)
	{
		return RpcDto::Vec3(C.R, C.G, C.B);
	}

	TArray<TSharedPtr<FJsonValue>> OvStrings(const TArray<FString>& In)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FString& S : In) { Out.Add(MakeShared<FJsonValueString>(S)); }
		return Out;
	}

	/** 현재 강조·라벨·미리보기 상태(모든 set 에 get 이 있게 — 보드 #1101 ⑤). */
	TSharedPtr<FJsonObject> OvState(const ARpcOverlayActor* Ov)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		TArray<FString> Cars, Presets, Previews;
		if (Ov)
		{
			Ov->CarHighlights.GetKeys(Cars);
			for (const auto& KV : Ov->PresetHighlights) { Presets.Add(FString::FromInt(KV.Key)); }
			Ov->Previews.GetKeys(Previews);
		}
		Cars.Sort(); Presets.Sort(); Previews.Sort();
		O->SetArrayField(TEXT("highlightedCars"), OvStrings(Cars));
		TArray<TSharedPtr<FJsonValue>> PIdx;
		for (const FString& S : Presets) { PIdx.Add(MakeShared<FJsonValueNumber>(FCString::Atoi(*S))); }
		O->SetArrayField(TEXT("highlightedPresets"), PIdx);
		O->SetArrayField(TEXT("previews"), OvStrings(Previews));
		O->SetBoolField(TEXT("lot"), Ov && Ov->Lot.IsSet());
		TSharedPtr<FJsonObject> L = MakeShared<FJsonObject>();
		L->SetBoolField(TEXT("cars"), Ov && Ov->bLabelCars);
		L->SetBoolField(TEXT("presets"), Ov && Ov->bLabelPresets);
		L->SetBoolField(TEXT("cameras"), Ov && Ov->bLabelCameras);
		O->SetObjectField(TEXT("labels"), L);
		O->SetBoolField(TEXT("global"), true);   // 시청자별이 아니라 월드에 한 벌
		return O;
	}

	/** {x,y,z?} m → cm. z 생략 = 지면 위 3 cm. */
	FVector OvPoint(const TSharedPtr<FJsonObject>& O, float U)
	{
		return FVector(RpcParam::GetFloat(O, TEXT("x")) * U, RpcParam::GetFloat(O, TEXT("y")) * U, RpcParam::GetFloat(O, TEXT("z"), 0.03) * U);
	}

	/** 다각형 꼭짓점 상한 — 도면 영역은 ~500점. */
	constexpr int32 OvMaxPolygonPoints = 2000;

	/**
	 * 다각형 바닥 높이(cm) — 꼭짓점(최대 64개 표본)마다 정적 오브젝트 질의로 노면을 찾아 중앙값 + 2 cm.
	 * ECC_Visibility 가 아니라 정적 질의인 이유는 CarActor 접지와 같다(LV_Park_01 노면은 Visibility 를 무시하고,
	 * 차량은 WorldDynamic 이라 걸러진다). 하나도 못 찾으면 false.
	 */
	bool OvGroundZ(UWorld* World, const TArray<FVector2D>& Pts, float& OutZ)
	{
		if (!World || Pts.Num() == 0) return false;
		TArray<double> Hits;
		const int32 Step = FMath::Max(1, Pts.Num() / 64);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(LotGround), /*bTraceComplex=*/true);
		for (int32 i = 0; i < Pts.Num(); i += Step)
		{
			FHitResult Hit;
			if (World->LineTraceSingleByObjectType(Hit, FVector(Pts[i].X, Pts[i].Y, 300.0), FVector(Pts[i].X, Pts[i].Y, -1000.0),
				FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllStaticObjects), Params))
			{
				Hits.Add(Hit.ImpactPoint.Z);
			}
		}
		if (Hits.Num() == 0) return false;
		Hits.Sort();
		OutZ = Hits[Hits.Num() / 2] + 2.0;
		return true;
	}

	/**
	 * 다각형 한 개 {points:[{x,y}], z?, fill?, opacity?, line?, lineWidth?, dashed?, color?} (m).
	 * z 생략 = 노면 자동(OutZSource "ground", 못 찾으면 "default" 0.03 m). 점 3개 미만·상한 초과는 -32602.
	 */
	bool OvReadPolygon(const TSharedPtr<FJsonObject>& O, UWorld* World, const FLinearColor& DefaultColor,
		FRpcPreviewPolygon& Out, FString& OutZSource, FRpcError& E)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		RpcParam::MarkRead(O, TEXT("points"));
		if (!O.IsValid() || !O->TryGetArrayField(TEXT("points"), Arr))
		{
			E.FailDomain(TEXT("points:[{x,y}] 가 필요합니다"), ERpcErrorKind::BadParams);
			return false;
		}
		if (Arr->Num() > OvMaxPolygonPoints)
		{
			E.FailDomain(FString::Printf(TEXT("points 가 %d개 — 상한 %d"), Arr->Num(), OvMaxPolygonPoints), ERpcErrorKind::BadParams);
			return false;
		}
		const float U = 100.f;
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject>* P = nullptr;
			if (!V->TryGetObject(P) || !P) continue;
			Out.Points.Add(FVector2D(RpcParam::GetFloat(*P, TEXT("x")) * U, RpcParam::GetFloat(*P, TEXT("y")) * U));
		}
		if (!OvReadColor(O, DefaultColor, Out.Color, E)) return false;
		Out.bFill = RpcParam::GetBool(O, TEXT("fill"), true);
		Out.Opacity = FMath::Clamp(RpcParam::GetFloat(O, TEXT("opacity"), 0.25), 0.0, 1.0);
		Out.bLine = RpcParam::GetBool(O, TEXT("line"), true);
		Out.LineWidthCm = FMath::Max(RpcParam::GetFloat(O, TEXT("lineWidth"), 0.3) * U, 1.0);
		Out.bDashed = RpcParam::GetBool(O, TEXT("dashed"), false);
		if (!Out.Prepare())
		{
			E.FailDomain(TEXT("서로 다른 점이 3개 이상 필요합니다"), ERpcErrorKind::BadParams);
			return false;
		}
		if (RpcParam::Has(O, TEXT("z")))
		{
			Out.ZCm = RpcParam::GetFloat(O, TEXT("z")) * U;
			OutZSource = TEXT("param");
		}
		else if (OvGroundZ(World, Out.Points, Out.ZCm))
		{
			OutZSource = TEXT("ground");
		}
		else
		{
			Out.ZCm = 3.f;
			OutZSource = TEXT("default");
		}
		return true;
	}

	TSharedPtr<FJsonObject> OvLotState(const ARpcOverlayActor* Ov, UWorld* World)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		if (Ov && Ov->Lot.IsSet())
		{
			const FRpcPreviewPolygon& L = Ov->Lot.GetValue();
			TSharedPtr<FJsonObject> J = ARpcOverlayActor::PolygonToJson(L);
			J->SetNumberField(TEXT("triangles"), L.Triangles.Num() / 3);
			O->SetObjectField(TEXT("lot"), J);
		}
		else
		{
			O->SetField(TEXT("lot"), MakeShared<FJsonValueNull>());
		}
		const FString Path = ARpcOverlayActor::GetLotFilePath(World);
		O->SetStringField(TEXT("file"), Path);
		O->SetBoolField(TEXT("fileExists"), FPaths::FileExists(Path));
		O->SetBoolField(TEXT("global"), true);
		return O;
	}
}

void FOverlayRpcModule::Register(URpcDispatcher& Dispatcher)
{
	// car.highlight {carNameIds[] | carNameId, on=true, color?, clear?} — 차를 바꾸지 않는 윤곽선 강조.
	Dispatcher.Register(TEXT("car.highlight"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* World = GetWorldPtr();
		ARpcOverlayActor* Ov = ARpcOverlayActor::Get(World);
		if (!Ov) { E.FailDomain(TEXT("월드 없음(맵 미로드)"), ERpcErrorKind::Busy); return nullptr; }
		FLinearColor Color;
		if (!OvReadColor(P, FLinearColor(1.f, 0.9f, 0.1f), Color, E)) return nullptr;
		const bool bOn = RpcParam::GetBool(P, TEXT("on"), true);
		if (RpcParam::GetBool(P, TEXT("clear"), false)) { Ov->CarHighlights.Reset(); }

		TArray<FString> Ids;
		if (RpcParam::Has(P, TEXT("carNameId"))) { Ids.Add(RpcParam::GetString(P, TEXT("carNameId"))); }
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		RpcParam::MarkRead(P, TEXT("carNameIds"));
		if (P.IsValid() && P->TryGetArrayField(TEXT("carNameIds"), Arr))
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr) { FString S; if (V->TryGetString(S)) Ids.Add(S); }
		}
		TSet<FString> Existing;
		for (TActorIterator<ACarActor> It(World); It; ++It) { Existing.Add(It->CarData.id); }

		int32 Changed = 0;
		TArray<FString> NotFound;
		for (const FString& Id : Ids)
		{
			if (!Existing.Contains(Id)) { NotFound.Add(Id); continue; }
			if (bOn)
			{
				const FLinearColor* Prev = Ov->CarHighlights.Find(Id);
				if (!Prev || !Prev->Equals(Color)) { ++Changed; }
				Ov->CarHighlights.Add(Id, Color);
			}
			else if (Ov->CarHighlights.Remove(Id) > 0) { ++Changed; }
		}
		Ov->Redraw();
		TSharedPtr<FJsonObject> O = OvState(Ov);
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("changed"), Changed);
		O->SetArrayField(TEXT("notFound"), OvStrings(NotFound));
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("car.highlight"), { true, false,
		TEXT("{carNameIds?:[str], carNameId?, on?=true, color?:name|#rrggbb|{r,g,b}, clear?:bool}"),
		TEXT("차량 윤곽선 강조(색·차량 불변, 저장 안 함, global). 응답 {changed, notFound, highlightedCars, …} — 조회는 view.getLabels") });

	// preset.highlight {idxs[] | idx, on=true, color?, clear?} — 프리셋 면 윤곽 강조(preset.select 와 별개, 여러 개 가능).
	Dispatcher.Register(TEXT("preset.highlight"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* World = GetWorldPtr();
		ARpcOverlayActor* Ov = ARpcOverlayActor::Get(World);
		if (!Ov) { E.FailDomain(TEXT("월드 없음(맵 미로드)"), ERpcErrorKind::Busy); return nullptr; }
		FLinearColor Color;
		if (!OvReadColor(P, FLinearColor(1.f, 0.55f, 0.1f), Color, E)) return nullptr;
		const bool bOn = RpcParam::GetBool(P, TEXT("on"), true);
		if (RpcParam::GetBool(P, TEXT("clear"), false)) { Ov->PresetHighlights.Reset(); }

		TArray<int32> Idxs;
		if (RpcParam::Has(P, TEXT("idx"))) { Idxs.Add(RpcParam::GetInt(P, TEXT("idx"))); }
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		RpcParam::MarkRead(P, TEXT("idxs"));
		if (P.IsValid() && P->TryGetArrayField(TEXT("idxs"), Arr))
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr) { Idxs.Add(static_cast<int32>(V->AsNumber())); }
		}
		AParkingPresetManager* PM = Cast<AParkingPresetManager>(UGameplayStatics::GetActorOfClass(World, AParkingPresetManager::StaticClass()));
		int32 Changed = 0;
		TArray<TSharedPtr<FJsonValue>> NotFound;
		for (const int32 Idx : Idxs)
		{
			const bool bExists = PM && PM->ResolvePresets().ContainsByPredicate([Idx](const FParkingPreset& X) { return X.PresetIdx == Idx; });
			if (!bExists) { NotFound.Add(MakeShared<FJsonValueNumber>(Idx)); continue; }
			if (bOn)
			{
				const FLinearColor* Prev = Ov->PresetHighlights.Find(Idx);
				if (!Prev || !Prev->Equals(Color)) { ++Changed; }
				Ov->PresetHighlights.Add(Idx, Color);
			}
			else if (Ov->PresetHighlights.Remove(Idx) > 0) { ++Changed; }
		}
		Ov->Redraw();
		TSharedPtr<FJsonObject> O = OvState(Ov);
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("changed"), Changed);
		O->SetArrayField(TEXT("notFound"), NotFound);
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("preset.highlight"), { true, false,
		TEXT("{idxs?:[int], idx?, on?=true, color?, clear?:bool}"),
		TEXT("프리셋 면 윤곽 강조(여러 개, preset.select 와 별개, global). 레벨 면은 대상 아님") });

	// view.setLabels {cars?, presets?, cameras?} — 주지 않은 항목은 그대로.
	Dispatcher.Register(TEXT("view.setLabels"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ARpcOverlayActor* Ov = ARpcOverlayActor::Get(GetWorldPtr());
		if (!Ov) { E.FailDomain(TEXT("월드 없음(맵 미로드)"), ERpcErrorKind::Busy); return nullptr; }
		const bool B0 = Ov->bLabelCars, B1 = Ov->bLabelPresets, B2 = Ov->bLabelCameras;
		Ov->bLabelCars = RpcParam::GetBool(P, TEXT("cars"), Ov->bLabelCars);
		Ov->bLabelPresets = RpcParam::GetBool(P, TEXT("presets"), Ov->bLabelPresets);
		Ov->bLabelCameras = RpcParam::GetBool(P, TEXT("cameras"), Ov->bLabelCameras);
		Ov->Redraw();
		TSharedPtr<FJsonObject> O = OvState(Ov);
		O->SetNumberField(TEXT("changed"), (B0 != Ov->bLabelCars) + (B1 != Ov->bLabelPresets) + (B2 != Ov->bLabelCameras));
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("view.setLabels"), { true, false, TEXT("{cars?:bool, presets?:bool, cameras?:bool}"),
		TEXT("3D 라벨 — 차량 carNameId·프리셋 P<idx>·카메라 C<camId>(바닥 번호는 preset.setView showNumbers). global(모든 시청자)") });

	Dispatcher.Register(TEXT("view.getLabels"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		return RpcDto::MakeObject(OvState(ARpcOverlayActor::Get(GetWorldPtr(), /*bCreate=*/false)));
	});
	Dispatcher.SetMethodMeta(TEXT("view.getLabels"), { false, false, TEXT(""),
		TEXT("덧그림 상태 {labels{cars,presets,cameras}, highlightedCars, highlightedPresets, previews, global}") });

	// preview.show {id, faces?:[{x,y,z?,rot,xSize,zSize}], boxes?:[{x,y,z,rot,size{x,y,z}|number}], color?} — 확인 단계 고스트.
	// 단위는 preset 과 같다: 위치 m(UE x·y 지면, z 높이), rot = 길이축 yaw(도), xSize=폭·zSize=길이(m). 같은 id 는 덮어쓴다.
	Dispatcher.Register(TEXT("preview.show"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ARpcOverlayActor* Ov = ARpcOverlayActor::Get(GetWorldPtr());
		if (!Ov) { E.FailDomain(TEXT("월드 없음(맵 미로드)"), ERpcErrorKind::Busy); return nullptr; }
		FString Id;
		if (!RpcParam::RequireString(P, TEXT("id"), Id, E)) return nullptr;
		FRpcPreviewSet Set;
		if (!OvReadColor(P, Set.Color, Set.Color, E)) return nullptr;
		const float U = 100.f;   // UE 미터 → cm(프로젝트 전역 규약 ×100)

		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		RpcParam::MarkRead(P, TEXT("faces"));
		if (P->TryGetArrayField(TEXT("faces"), Arr))
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				const TSharedPtr<FJsonObject>* F = nullptr;
				if (!V->TryGetObject(F) || !F) continue;
				FRpcPreviewFace Face;
				Face.Center = OvPoint(*F, U);
				Face.YawDeg = RpcParam::GetFloat(*F, TEXT("rot"));
				Face.WidthCm = RpcParam::GetFloat(*F, TEXT("xSize"), 2.5) * U;
				Face.LengthCm = RpcParam::GetFloat(*F, TEXT("zSize"), 5.0) * U;
				Set.Faces.Add(Face);
			}
		}
		RpcParam::MarkRead(P, TEXT("boxes"));
		if (P->TryGetArrayField(TEXT("boxes"), Arr))
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				const TSharedPtr<FJsonObject>* B = nullptr;
				if (!V->TryGetObject(B) || !B) continue;
				FRpcPreviewBox Box;
				Box.Center = OvPoint(*B, U);
				Box.YawDeg = RpcParam::GetFloat(*B, TEXT("rot"));
				double Uni = 0.0;
				Box.HalfSizeCm = (*B)->TryGetNumberField(TEXT("size"), Uni)
					? FVector(Uni * U * 0.5)
					: RpcParam::GetVec3(*B, TEXT("size"), FVector(2.0, 4.5, 1.5)) * U * 0.5;
				Set.Boxes.Add(Box);
			}
		}
		// polygons — 바닥 다각형(채움+윤곽, 보드 #1168). 색 생략 = 묶음 color.
		TArray<TSharedPtr<FJsonValue>> PolyInfo;
		RpcParam::MarkRead(P, TEXT("polygons"));
		if (P->TryGetArrayField(TEXT("polygons"), Arr))
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				const TSharedPtr<FJsonObject>* G = nullptr;
				if (!V->TryGetObject(G) || !G) continue;
				FRpcPreviewPolygon Poly;
				FString ZSource;
				if (!OvReadPolygon(*G, GetWorldPtr(), Set.Color, Poly, ZSource, E)) return nullptr;
				TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
				Info->SetNumberField(TEXT("points"), Poly.Points.Num());
				Info->SetNumberField(TEXT("triangles"), Poly.Triangles.Num() / 3);
				Info->SetBoolField(TEXT("filled"), Poly.bFill && Poly.Triangles.Num() > 0);
				Info->SetNumberField(TEXT("z"), Poly.ZCm / 100.0);
				Info->SetStringField(TEXT("zSource"), ZSource);
				PolyInfo.Add(MakeShared<FJsonValueObject>(Info));
				Set.Polygons.Add(MoveTemp(Poly));
			}
		}
		if (Set.Faces.Num() + Set.Boxes.Num() + Set.Polygons.Num() == 0)
		{
			E.FailDomain(TEXT("faces·boxes·polygons 중 하나가 필요합니다"), ERpcErrorKind::BadParams);
			return nullptr;
		}
		Ov->Previews.Add(Id, Set);
		Ov->Redraw();
		TSharedPtr<FJsonObject> O = OvState(Ov);
		O->SetStringField(TEXT("id"), Id);
		O->SetNumberField(TEXT("faces"), Set.Faces.Num());
		O->SetNumberField(TEXT("boxes"), Set.Boxes.Num());
		O->SetArrayField(TEXT("polygons"), PolyInfo);
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("preview.show"), { true, false,
		TEXT("{id:str, faces?:[{x,y,z?,rot,xSize,zSize}], boxes?:[{x,y,z?,rot,size:{x,y,z}|number}], polygons?:[{points:[{x,y}], z?, fill?=true, opacity?=0.25, line?=true, lineWidth?=0.3, dashed?, color?}], color?}"),
		TEXT("확인용 반투명 고스트(m, rot=길이축 yaw). polygons = 바닥 다각형 채움+윤곽(z 생략 = 노면 자동). car/preset 목록·저장에 안 들어간다. 같은 id 덮어씀, global") });

	// lot.set {points, z?, fill?, opacity?, line?, lineWidth?, dashed?, color?, save?=true} — 주차장 영역(보드 #1168).
	// 미리보기와 달리 레벨별 파일(Save/3D/Lot/Lot_<레벨>.json)에 남아 재기동·레벨 전환 뒤 GameMode 가 다시 그린다.
	Dispatcher.Register(TEXT("lot.set"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* World = GetWorldPtr();
		ARpcOverlayActor* Ov = ARpcOverlayActor::Get(World);
		if (!Ov) { E.FailDomain(TEXT("월드 없음(맵 미로드)"), ERpcErrorKind::Busy); return nullptr; }
		FRpcPreviewPolygon Poly;
		FString ZSource;
		if (!OvReadPolygon(P, World, Poly.Color, Poly, ZSource, E)) return nullptr;
		const bool bSave = RpcParam::GetBool(P, TEXT("save"), true);
		Ov->Lot = Poly;
		Ov->Redraw();
		FString Path;
		const bool bSaved = bSave && Ov->SaveLot(Path);
		if (bSave && !bSaved) { UE_LOG(LogTemp, Warning, TEXT("[Lot] 주차장 영역 저장 실패: %s"), *Path); }
		TSharedPtr<FJsonObject> O = OvLotState(Ov, World);
		O->SetBoolField(TEXT("ok"), true);
		O->SetBoolField(TEXT("saved"), bSaved);
		O->SetStringField(TEXT("zSource"), ZSource);
		O->SetBoolField(TEXT("filled"), Poly.bFill && Poly.Triangles.Num() > 0);
		O->SetNumberField(TEXT("changed"), 1);
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("lot.set"), { true, false,
		TEXT("{points:[{x,y}], z?, fill?=true, opacity?=0.25, line?=true, lineWidth?=0.3, dashed?, color?='#ff9f1c', save?=true}"),
		TEXT("주차장 영역 다각형(m) — 채움+윤곽, 레벨별 파일에 저장돼 재기동 후 복원. 하나만(덮어씀). 응답 {lot, file, saved, zSource, filled}") });

	Dispatcher.Register(TEXT("lot.get"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* World = GetWorldPtr();
		return RpcDto::MakeObject(OvLotState(ARpcOverlayActor::Get(World, /*bCreate=*/false), World));
	});
	Dispatcher.SetMethodMeta(TEXT("lot.get"), { false, false, TEXT(""),
		TEXT("주차장 영역 {lot:{points,z,fill,opacity,line,lineWidth,dashed,color,triangles}|null, file, fileExists}") });

	Dispatcher.Register(TEXT("lot.clear"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* World = GetWorldPtr();
		ARpcOverlayActor* Ov = ARpcOverlayActor::Get(World, /*bCreate=*/false);
		const bool bHad = Ov && Ov->Lot.IsSet();
		const bool bKeepFile = RpcParam::GetBool(P, TEXT("keepFile"), false);
		bool bFileRemoved = false;
		if (Ov)
		{
			Ov->Lot.Reset();
			Ov->Redraw();
		}
		const FString Path = ARpcOverlayActor::GetLotFilePath(World);
		if (!bKeepFile && FPaths::FileExists(Path)) { bFileRemoved = IFileManager::Get().Delete(*Path); }
		TSharedPtr<FJsonObject> O = OvLotState(Ov, World);
		O->SetBoolField(TEXT("ok"), true);
		O->SetBoolField(TEXT("fileRemoved"), bFileRemoved);
		O->SetNumberField(TEXT("changed"), (bHad || bFileRemoved) ? 1 : 0);
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("lot.clear"), { true, false, TEXT("{keepFile?:bool=false}"),
		TEXT("주차장 영역 지우기 — 화면과 레벨 파일(keepFile:true 면 화면만, 재기동 시 복원)") });

	Dispatcher.Register(TEXT("preview.clear"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ARpcOverlayActor* Ov = ARpcOverlayActor::Get(GetWorldPtr(), /*bCreate=*/false);
		int32 Cleared = 0;
		if (Ov)
		{
			if (RpcParam::Has(P, TEXT("id"))) { Cleared = Ov->Previews.Remove(RpcParam::GetString(P, TEXT("id"))); }
			else { Cleared = Ov->Previews.Num(); Ov->Previews.Reset(); }
			Ov->Redraw();
		}
		TSharedPtr<FJsonObject> O = OvState(Ov);
		O->SetNumberField(TEXT("cleared"), Cleared);
		O->SetNumberField(TEXT("changed"), Cleared);
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("preview.clear"), { true, false, TEXT("{id?:str}"), TEXT("고스트 지우기(id 생략 = 전부). 없는 id 는 cleared:0") });

	Dispatcher.Register(TEXT("preview.list"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		return RpcDto::MakeObject(OvState(ARpcOverlayActor::Get(GetWorldPtr(), /*bCreate=*/false)));
	});
	Dispatcher.SetMethodMeta(TEXT("preview.list"), { false, false, TEXT(""), TEXT("view.getLabels 와 같은 상태") });
}
