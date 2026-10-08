// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateRpcModule.h"
#include "../RpcDispatcher.h"
#include "../RpcParamUtil.h"
#include "../../CarPlacementManager.h"
#include "../../CarPlacementLibrary.h"
#include "../../CarActor.h"
#include "../../CarColorComponent.h"
#include "../../CameraControlManager.h"
#include "../../PTZCameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

namespace
{
	constexpr int32 StMaxSnapshots = 20;

	uint8 StParseScope(const FString& S)
	{
		const FString L = S.ToLower();
		if (L == TEXT("cars")) return FStateRpcModule::ScopeCars;
		if (L == TEXT("presets")) return FStateRpcModule::ScopePresets;
		if (L == TEXT("cameras")) return FStateRpcModule::ScopeCameras;
		if (L == TEXT("view")) return FStateRpcModule::ScopeView;
		if (L == TEXT("all") || L.IsEmpty()) return FStateRpcModule::ScopeAll;
		return 0;
	}

	TArray<TSharedPtr<FJsonValue>> StScopeNames(uint8 Scopes)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		if (Scopes & FStateRpcModule::ScopeCars) Out.Add(MakeShared<FJsonValueString>(TEXT("cars")));
		if (Scopes & FStateRpcModule::ScopePresets) Out.Add(MakeShared<FJsonValueString>(TEXT("presets")));
		if (Scopes & FStateRpcModule::ScopeCameras) Out.Add(MakeShared<FJsonValueString>(TEXT("cameras")));
		if (Scopes & FStateRpcModule::ScopeView) Out.Add(MakeShared<FJsonValueString>(TEXT("view")));
		return Out;
	}

	/** {prefabId|prefabName, pos{x,y,z?}, rotY?, isFront?, presetId?, faceSlot?} → FCarPos. 위치는 UE 미터(x·y 지면, z 높이). */
	bool StReadCar(const TSharedPtr<FJsonObject>& C, const TArray<FCarPresetEntry>& Catalog, int32 Index, FCarPos& Out, FString& OutErr)
	{
		const TSharedPtr<FJsonObject>* PosObj = nullptr;
		if (!C.IsValid() || !C->TryGetObjectField(TEXT("pos"), PosObj) || !PosObj)
		{
			OutErr = FString::Printf(TEXT("cars[%d].pos{x,y,z?} 가 필요합니다"), Index);
			return false;
		}
		int32 PrefabId = RpcParam::GetInt(C, TEXT("prefabId"), 0);
		const FString PrefabName = RpcParam::GetString(C, TEXT("prefabName"));
		if (PrefabId <= 0 && !PrefabName.IsEmpty())
		{
			for (const FCarPresetEntry& E : Catalog) { if (E.PrefabName.Equals(PrefabName, ESearchCase::IgnoreCase)) { PrefabId = E.Idx; break; } }
			if (PrefabId <= 0) { OutErr = FString::Printf(TEXT("cars[%d] 카탈로그에 없는 차종: %s"), Index, *PrefabName); return false; }
		}
		Out.prefabId = PrefabId > 0 ? PrefabId : 1;
		Out.prefabName = UCarPlacementLibrary::PrefabNameFromId(Catalog, Out.prefabId);
		Out.presetId = RpcParam::GetInt(C, TEXT("presetId"), 0);
		Out.slotId = RpcParam::GetInt(C, TEXT("faceSlot"), -1);
		Out.rotY = RpcParam::GetFloat(C, TEXT("rotY"), 180.0);
		Out.isFront = RpcParam::GetBool(C, TEXT("isFront"), true);
		Out.pos = { static_cast<float>(RpcParam::GetFloat(*PosObj, TEXT("x"))), static_cast<float>(RpcParam::GetFloat(*PosObj, TEXT("y"))),
		            static_cast<float>(RpcParam::GetFloat(*PosObj, TEXT("z"))) };
		return true;
	}
}

FString FStateRpcModule::TakeSnapshot(uint8 Scopes, FRpcError& E)
{
	UWorld* World = GetWorldPtr();
	if (!World) { E.FailDomain(TEXT("월드 없음(맵 미로드)"), ERpcErrorKind::Busy); return FString(); }

	FSnapshot S;
	S.Scopes = Scopes;
	S.Created = FDateTime::Now();
	S.Token = FString::Printf(TEXT("st%d"), NextToken++);

	if (Scopes & ScopeCars)
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return FString();
		for (int32 i = 0; i < Mgr->GetCarCount(); ++i)
		{
			ACarActor* Car = Mgr->GetCar(i);
			if (!Car) continue;
			FCarSnap C;
			C.Pos = Car->CarData;
			C.bHidden = Car->IsHidden();
			C.Plate = Car->GetPlateNumber();
			C.PlateKind = Car->GetPlateKind();
			if (Car->ColorComp) { C.bPainted = Car->ColorComp->IsPainted(); C.Color = Car->ColorComp->GetCurrentColor(); }
			S.Cars.Add(C);
		}
	}
	if (Scopes & ScopePresets)
	{
		AParkingPresetManager* PM = GetPresetManager(E); if (!PM) return FString();
		S.Presets = PM->StoredPresets;
		S.Anchors = PM->GetNumberAnchors();
	}
	if (Scopes & ScopeCameras)
	{
		ACameraControlManager* CM = GetCameraManager(E); if (!CM) return FString();
		for (int32 i = 0; i < CM->GetCameraCount(); ++i)
		{
			FCamSnap C;
			if (APTZCameraActor* Cam = CM->GetCamera(i))
			{
				C.Loc = Cam->GetActorLocation();
				Cam->GetPanTilt(C.Pan, C.Tilt);
				C.Zoom = Cam->GetZoom();
			}
			S.Cams.Add(C);
		}
	}
	if (Scopes & ScopeView)
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			PC->GetPlayerViewPoint(S.ViewLoc, S.ViewRot);
		}
	}

	Snapshots.Add(MoveTemp(S));
	while (Snapshots.Num() > StMaxSnapshots) { Snapshots.RemoveAt(0); }
	return Snapshots.Last().Token;
}

bool FStateRpcModule::RestoreSnapshot(const FString& Token, TSharedPtr<FJsonObject>& OutSummary, FRpcError& E)
{
	const FSnapshot* S = Snapshots.FindByPredicate([&Token](const FSnapshot& X) { return X.Token == Token; });
	if (!S)
	{
		E.FailDomain(FString::Printf(TEXT("스냅샷 없음: %s (최근 %d 개만 보관 — state.list 로 확인)"), *Token, StMaxSnapshots), ERpcErrorKind::NotFound);
		return false;
	}
	UWorld* World = GetWorldPtr();
	if (!World) { E.FailDomain(TEXT("월드 없음(맵 미로드)"), ERpcErrorKind::Busy); return false; }

	TSharedPtr<FJsonObject> Restored = MakeShared<FJsonObject>();
	if (S->Scopes & ScopeCars)
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return false;
		Mgr->ClearAll();
		int32 N = 0;
		for (const FCarSnap& C : S->Cars)
		{
			ACarActor* Car = Mgr->SpawnCarFromPos(C.Pos, Catalog);
			if (!Car) continue;
			++N;
			if (!C.Plate.IsEmpty()) { Car->SetPlate(C.Plate, C.PlateKind); }
			if (C.bPainted && Car->ColorComp) { Car->ColorComp->SetColor(C.Color); }
			if (C.bHidden) { Car->SetActorHiddenInGame(true); Car->SetActorEnableCollision(false); }
		}
		Restored->SetNumberField(TEXT("cars"), N);
	}
	if (S->Scopes & ScopePresets)
	{
		AParkingPresetManager* PM = GetPresetManager(E); if (!PM) return false;
		PM->StoredPresets = S->Presets;
		PM->SetNumberAnchors(S->Anchors);
		PM->RefreshView();
		Restored->SetNumberField(TEXT("presets"), S->Presets.Num());
	}
	if (S->Scopes & ScopeCameras)
	{
		ACameraControlManager* CM = GetCameraManager(E); if (!CM) return false;
		int32 N = 0;
		for (int32 i = 0; i < S->Cams.Num() && i < CM->GetCameraCount(); ++i)
		{
			if (APTZCameraActor* Cam = CM->GetCamera(i))
			{
				const FCamSnap& C = S->Cams[i];
				Cam->SetCameraWorldLocation(C.Loc.X, C.Loc.Y, C.Loc.Z);
				Cam->SetPanTilt(C.Pan, C.Tilt);
				Cam->SetZoom(C.Zoom);
				++N;
			}
		}
		Restored->SetNumberField(TEXT("cameras"), N);
	}
	if (S->Scopes & ScopeView)
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (APawn* Pawn = PC->GetPawn())
			{
				Pawn->SetActorLocation(S->ViewLoc - FVector(0.f, 0.f, Pawn->BaseEyeHeight));
				Pawn->SetActorRotation(S->ViewRot);
			}
			PC->SetControlRotation(S->ViewRot);
		}
		Restored->SetBoolField(TEXT("view"), true);
	}
	OutSummary = MakeShared<FJsonObject>();
	OutSummary->SetStringField(TEXT("token"), Token);
	OutSummary->SetObjectField(TEXT("restored"), Restored);
	return true;
}

void FStateRpcModule::Register(URpcDispatcher& Dispatcher)
{
	// ---- state.* ----
	Dispatcher.Register(TEXT("state.snapshot"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		uint8 Scopes = 0;
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		RpcParam::MarkRead(P, TEXT("scope"));
		if (P.IsValid() && P->TryGetArrayField(TEXT("scope"), Arr))
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr) { Scopes |= StParseScope(V->AsString()); }
		}
		else
		{
			const FString One = RpcParam::GetString(P, TEXT("scope"), TEXT("all"));
			Scopes = StParseScope(One);
			if (Scopes == 0) { E.FailDomain(FString::Printf(TEXT("scope 는 cars | presets | cameras | view | all 중 하나입니다: %s"), *One), ERpcErrorKind::BadParams); return nullptr; }
		}
		const FString Token = TakeSnapshot(Scopes, E);
		if (Token.IsEmpty()) return nullptr;
		const FSnapshot& S = Snapshots.Last();
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("token"), Token);
		O->SetArrayField(TEXT("scope"), StScopeNames(S.Scopes));
		O->SetNumberField(TEXT("cars"), S.Cars.Num());
		O->SetNumberField(TEXT("presets"), S.Presets.Num());
		O->SetNumberField(TEXT("cameras"), S.Cams.Num());
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("state.snapshot"), { true, false, TEXT("{scope?:\"cars\"|\"presets\"|\"cameras\"|\"view\"|\"all\"|[...] (기본 all)}"),
		TEXT("메모리 스냅샷(파일 안 씀). 최근 20개 보관 — 넘치면 오래된 것부터 버린다. {token, scope, cars, presets, cameras}") });

	Dispatcher.Register(TEXT("state.restore"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		FString Token;
		if (!RpcParam::RequireString(P, TEXT("token"), Token, E)) return nullptr;
		TSharedPtr<FJsonObject> O;
		if (!RestoreSnapshot(Token, O, E)) return nullptr;
		O->SetBoolField(TEXT("ok"), true);
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("state.restore"), { true, true, TEXT("{token}"),
		TEXT("스냅샷으로 되돌린다(cars 범위면 지금 차량을 지우고 다시 만든다 — carNameId 는 스냅샷 값). 없는/만료 토큰은 not_found. 토큰은 남는다(여러 번 되돌리기 가능)") });

	Dispatcher.Register(TEXT("state.drop"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		FString Token;
		if (!RpcParam::RequireString(P, TEXT("token"), Token, E)) return nullptr;
		const bool bDropped = DropSnapshot(Token);
		if (!bDropped) { E.FailDomain(FString::Printf(TEXT("스냅샷 없음: %s"), *Token), ERpcErrorKind::NotFound); return nullptr; }
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("changed"), 1);
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("state.drop"), { true, false, TEXT("{token}"), TEXT("스냅샷 버리기") });

	Dispatcher.Register(TEXT("state.list"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FSnapshot& S : Snapshots)
		{
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("token"), S.Token);
			O->SetArrayField(TEXT("scope"), StScopeNames(S.Scopes));
			O->SetStringField(TEXT("created"), S.Created.ToIso8601());
			O->SetNumberField(TEXT("cars"), S.Cars.Num());
			O->SetNumberField(TEXT("presets"), S.Presets.Num());
			Arr.Add(MakeShared<FJsonValueObject>(O));
		}
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetArrayField(TEXT("snapshots"), Arr);
		O->SetNumberField(TEXT("max"), StMaxSnapshots);
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("state.list"), { false, false, TEXT(""), TEXT("{snapshots:[{token,scope,created,cars,presets}], max}") });

	// ---- 컬렉션 일괄 ----
	// car.createMany {cars:[...]} / car.setAll {cars:[...]} (= 전부 지우고 이 목록으로). 한 대라도 형식이 틀리면 아무것도 바꾸지 않는다.
	auto CreateCars = [this](const TSharedPtr<FJsonObject>& P, FRpcError& E, bool bReplace) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		RpcParam::MarkRead(P, TEXT("cars"));
		if (!P.IsValid() || !P->TryGetArrayField(TEXT("cars"), Arr))
		{
			E.FailDomain(TEXT("필수 파라미터 누락: cars ([{prefabId|prefabName, pos{x,y,z?}, rotY?, isFront?, presetId?, faceSlot?, visible?, plate?, plateKind?}])"), ERpcErrorKind::BadParams);
			return nullptr;
		}
		// 먼저 전부 검사한다 — 중간에 실패해 반만 바뀐 장면을 남기지 않는다.
		TArray<FCarPos> Parsed;
		for (int32 i = 0; i < Arr->Num(); ++i)
		{
			const TSharedPtr<FJsonObject>* C = nullptr;
			FCarPos Pos; FString Err;
			if (!(*Arr)[i]->TryGetObject(C) || !C || !StReadCar(*C, Catalog, i, Pos, Err))
			{
				E.FailDomain(Err.IsEmpty() ? FString::Printf(TEXT("cars[%d] 는 객체여야 합니다"), i) : Err, ERpcErrorKind::BadParams);
				return nullptr;
			}
			Parsed.Add(Pos);
		}
		const int32 Removed = bReplace ? Mgr->GetCarCount() : 0;
		if (bReplace) { Mgr->ClearAll(); }
		// 같은 자리(같은 면·1 m 이내)에 이미 선 차는 지우고 바꾼다 — 목록 안의 중복끼리도 뒤가 이긴다(2026-10-08).
		const bool bAllowOverlap = RpcParam::GetBool(P, TEXT("allowOverlap"), false);
		TArray<FString> Replaced;
		TArray<ACarActor*> Spawned;
		for (int32 i = 0; i < Parsed.Num(); ++i)
		{
			FCarPos Pos = Parsed[i];
			if (!bAllowOverlap)
			{
				Replaced.Append(Mgr->RemoveCarsOccupying(UCarPlacementLibrary::UnrealMetersToWorld(Pos.pos, Mgr->MetersToUU)));
			}
			Pos.id = Mgr->MakeUniqueCarId(Replaced);
			ACarActor* Car = Mgr->SpawnCarFromPos(Pos, Catalog);
			if (!Car) { E.FailDomain(FString::Printf(TEXT("cars[%d] 차량 생성 실패"), i), ERpcErrorKind::Internal); return nullptr; }
			const TSharedPtr<FJsonObject>* C = nullptr;
			(*Arr)[i]->TryGetObject(C);
			const FString Plate = RpcParam::GetString(*C, TEXT("plate"));
			const FString Kind = RpcParam::GetString(*C, TEXT("plateKind"));
			if (!Plate.IsEmpty() || !Kind.IsEmpty()) { Car->SetPlate(Plate.IsEmpty() ? Car->GetPlateNumber() : Plate, Kind.IsEmpty() ? Car->GetPlateKind() : Kind); }
			if (!RpcParam::GetBool(*C, TEXT("visible"), true)) { Car->SetActorHiddenInGame(true); Car->SetActorEnableCollision(false); }
			Spawned.Add(Car);
		}
		// 이번 호출에서 만든 차가 뒤 항목에 교체됐을 수 있다 — 살아 있는 것만 돌려준다.
		TArray<TSharedPtr<FJsonValue>> Out;
		for (ACarActor* Car : Spawned)
		{
			if (IsValid(Car) && !Car->IsActorBeingDestroyed()) { Out.Add(RpcDto::CarToDtoValue(Car)); }
		}
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("count"), Out.Num());
		O->SetNumberField(TEXT("removed"), Removed);
		O->SetNumberField(TEXT("changed"), Out.Num() + Removed + Replaced.Num());
		O->SetArrayField(TEXT("cars"), Out);
		O->SetArrayField(TEXT("replaced"), RpcDto::StringArray(Replaced));
		return RpcDto::MakeObject(O);
	};
	Dispatcher.Register(TEXT("car.createMany"), [CreateCars](const TSharedPtr<FJsonObject>& P, FRpcError& E) { return CreateCars(P, E, false); });
	Dispatcher.Register(TEXT("car.setAll"), [CreateCars](const TSharedPtr<FJsonObject>& P, FRpcError& E) { return CreateCars(P, E, true); });
	const TCHAR* CarsSchema = TEXT("{cars:[{prefabId|prefabName, pos{x,y,z?} (UE m), rotY?=180, isFront?=true, presetId?, faceSlot?, visible?=true, plate?, plateKind?}]}");
	Dispatcher.SetMethodMeta(TEXT("car.createMany"), { true, false, CarsSchema, TEXT("여러 대를 한 번에 추가(한 프레임). 형식이 하나라도 틀리면 아무것도 안 만든다. {count, changed, cars:[CarDto]}") });
	Dispatcher.SetMethodMeta(TEXT("car.setAll"), { true, true, CarsSchema, TEXT("전부 지우고 이 목록으로(한 프레임). {count, removed, changed, cars}") });

	// car.setVisible {carNameIds[] | all:true, visible}
	Dispatcher.Register(TEXT("car.setVisible"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		bool bVisible = true;
		if (!RpcParam::RequireBool(P, TEXT("visible"), bVisible, E)) return nullptr;
		TArray<ACarActor*> Targets;
		TArray<TSharedPtr<FJsonValue>> NotFound;
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		RpcParam::MarkRead(P, TEXT("carNameIds"));
		if (P->TryGetArrayField(TEXT("carNameIds"), Arr))
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				if (ACarActor* Car = Mgr->FindByNameId(V->AsString())) { Targets.Add(Car); }
				else { NotFound.Add(MakeShared<FJsonValueString>(V->AsString())); }
			}
		}
		else if (RpcParam::GetBool(P, TEXT("all"), false))
		{
			for (int32 i = 0; i < Mgr->GetCarCount(); ++i) { if (ACarActor* C = Mgr->GetCar(i)) Targets.Add(C); }
		}
		else
		{
			E.FailDomain(TEXT("carNameIds[] 또는 all:true 가 필요합니다"), ERpcErrorKind::BadParams);
			return nullptr;
		}
		int32 Changed = 0;
		for (ACarActor* Car : Targets)
		{
			if (Car->IsHidden() == bVisible) { ++Changed; }
			Car->SetActorHiddenInGame(!bVisible);
			Car->SetActorEnableCollision(bVisible);
		}
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetBoolField(TEXT("visible"), bVisible);
		O->SetNumberField(TEXT("count"), Targets.Num());
		O->SetNumberField(TEXT("changed"), Changed);
		O->SetArrayField(TEXT("notFound"), NotFound);
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("car.setVisible"), { true, false, TEXT("{carNameIds?:[str] | all?:true, visible:bool}"),
		TEXT("여러 대 표시/숨김 한 번에. {count, changed, notFound}") });

	// preset.setAll {presets:[preset.create 와 같은 객체]} — 지우고 이 목록으로. 각 항목은 preset.create 핸들러를 그대로 탄다.
	URpcDispatcher* D = &Dispatcher;
	Dispatcher.Register(TEXT("preset.setAll"), [this, D](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingPresetManager* PM = GetPresetManager(E); if (!PM) return nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		RpcParam::MarkRead(P, TEXT("presets"));
		if (!P.IsValid() || !P->TryGetArrayField(TEXT("presets"), Arr))
		{
			E.FailDomain(TEXT("필수 파라미터 누락: presets ([preset.create 와 같은 객체])"), ERpcErrorKind::BadParams);
			return nullptr;
		}
		const TArray<FParkingPreset> Before = PM->StoredPresets;
		const int32 Removed = Before.Num();
		PM->ClearPresets();
		TArray<TSharedPtr<FJsonValue>> Out;
		TArray<TSharedPtr<FJsonValue>> Warnings;
		for (int32 i = 0; i < Arr->Num(); ++i)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			TSharedPtr<FJsonValue> R; FRpcError Ei; TArray<FString> Unread;
			if (!(*Arr)[i]->TryGetObject(Obj) || !Obj || !D->Dispatch(TEXT("preset.create"), *Obj, R, Ei, &Unread))
			{
				// 하나라도 실패하면 원래 목록으로 되돌린다 — 반만 바뀐 장면을 남기지 않는다.
				PM->StoredPresets = Before;
				PM->RefreshView();
				E.FailDomain(FString::Printf(TEXT("presets[%d]: %s"), i, Ei.HasError() ? *Ei.Message : TEXT("객체여야 합니다")), ERpcErrorKind::BadParams);
				return nullptr;
			}
			for (const FString& K : Unread)
			{
				Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("presets[%d].%s"), i, *K)));
			}
			Out.Add(R);
		}
		PM->RefreshView();
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("count"), Out.Num());
		O->SetNumberField(TEXT("removed"), Removed);
		O->SetNumberField(TEXT("changed"), Out.Num() + Removed);
		O->SetArrayField(TEXT("presets"), Out);
		O->SetArrayField(TEXT("unknownParams"), Warnings);   // 항목 안의 모르는 키(최상위 warnings 는 최상위 키만 본다)
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("preset.setAll"), { true, true, TEXT("{presets:[preset.create params]}"),
		TEXT("프리셋 전부 교체(한 프레임). 항목 하나라도 실패하면 원래 목록으로 되돌리고 오류. {count, removed, changed, presets, unknownParams}") });
}
