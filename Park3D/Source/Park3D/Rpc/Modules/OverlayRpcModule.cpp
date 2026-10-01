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
		if (Set.Faces.Num() + Set.Boxes.Num() == 0)
		{
			E.FailDomain(TEXT("faces 또는 boxes 가 필요합니다"), ERpcErrorKind::BadParams);
			return nullptr;
		}
		Ov->Previews.Add(Id, Set);
		Ov->Redraw();
		TSharedPtr<FJsonObject> O = OvState(Ov);
		O->SetStringField(TEXT("id"), Id);
		O->SetNumberField(TEXT("faces"), Set.Faces.Num());
		O->SetNumberField(TEXT("boxes"), Set.Boxes.Num());
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("preview.show"), { true, false,
		TEXT("{id:str, faces?:[{x,y,z?,rot,xSize,zSize}], boxes?:[{x,y,z?,rot,size:{x,y,z}|number}], color?}"),
		TEXT("확인용 반투명 고스트(m, rot=길이축 yaw). car/preset 목록·저장에 안 들어간다. 같은 id 덮어씀, global") });

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
