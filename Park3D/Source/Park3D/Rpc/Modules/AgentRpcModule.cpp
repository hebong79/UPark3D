// Copyright Epic Games, Inc. All Rights Reserved.

#include "AgentRpcModule.h"
#include "../RpcDispatcher.h"
#include "../RpcParamUtil.h"
#include "../../Sim/ParkAgentManager.h"
#include "../../Sim/ParkAgentActor.h"

namespace
{
	/** {x,y} 객체 → 점. */
	bool AgPoint(const TSharedPtr<FJsonValue>& V, FVector2D& Out)
	{
		const TSharedPtr<FJsonObject>* O = nullptr;
		double X = 0.0, Y = 0.0;
		if (!V.IsValid() || !V->TryGetObject(O) || !O || !(*O)->TryGetNumberField(TEXT("x"), X) || !(*O)->TryGetNumberField(TEXT("y"), Y)) { return false; }
		Out = FVector2D(X, Y);
		return true;
	}

	/** id | ids[] | all:true. 셋 다 없으면 false. */
	bool AgTargets(const TSharedPtr<FJsonObject>& P, TArray<FString>& OutIds, bool& bOutAll)
	{
		bOutAll = RpcParam::GetBool(P, TEXT("all"), false);
		const FString One = RpcParam::GetString(P, TEXT("id"));
		if (!One.IsEmpty()) { OutIds.Add(One); }
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		RpcParam::MarkRead(P, TEXT("ids"));
		if (P.IsValid() && P->TryGetArrayField(TEXT("ids"), Arr))
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr) { OutIds.Add(V->AsString()); }
		}
		return bOutAll || OutIds.Num() > 0;
	}

	TArray<TSharedPtr<FJsonValue>> AgStrings(const TArray<FString>& S)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FString& X : S) { Out.Add(MakeShared<FJsonValueString>(X)); }
		return Out;
	}

	/** 요청한 모델 이름이 그 종류에 없으면 {param, requested, available[]} 한 줄을 더한다. */
	void AgModelFallback(ParkAgent::EKind Kind, const FString& Requested, TArray<TSharedPtr<FJsonValue>>& Out, const TCHAR* Param = TEXT("model"))
	{
		if (Requested.IsEmpty()) { return; }
		const TArray<FString> Names = AParkAgentActor::ModelNames(Kind);
		for (const FString& N : Names) { if (N.Equals(Requested, ESearchCase::IgnoreCase)) { return; } }
		TSharedPtr<FJsonObject> F = MakeShared<FJsonObject>();
		F->SetStringField(TEXT("param"), Param);
		F->SetStringField(TEXT("requested"), Requested);
		F->SetArrayField(TEXT("available"), AgStrings(Names));
		Out.Add(MakeShared<FJsonValueObject>(F));
	}
}

void FAgentRpcModule::Register(URpcDispatcher& Dispatcher)
{
	Dispatcher.Register(TEXT("agent.create"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		FString KindS;
		if (!RpcParam::RequireString(P, TEXT("kind"), KindS, E)) return nullptr;
		ParkAgent::EKind Kind;
		if (!ParkAgent::ParseKind(KindS, Kind))
		{
			E.FailDomain(FString::Printf(TEXT("kind 는 pedestrian | bicycle | motorcycle 중 하나입니다: %s"), *KindS), ERpcErrorKind::BadParams);
			return nullptr;
		}
		const TSharedPtr<FJsonObject>* PosObj = nullptr;
		RpcParam::MarkRead(P, TEXT("pos"));
		double X = 0.0, Y = 0.0;
		if (!P->TryGetObjectField(TEXT("pos"), PosObj) || !PosObj || !(*PosObj)->TryGetNumberField(TEXT("x"), X) || !(*PosObj)->TryGetNumberField(TEXT("y"), Y))
		{
			E.FailDomain(TEXT("필수 파라미터 누락: pos {x,y} (UE 미터, 지면 — 높이는 지면에 맞춘다)"), ERpcErrorKind::BadParams);
			return nullptr;
		}
		const double Yaw = RpcParam::GetFloat(P, TEXT("yaw"), 0.0);
		const bool bRider = RpcParam::GetBool(P, TEXT("rider"), true);
		const FString Model = RpcParam::GetString(P, TEXT("model"));
		const FString RiderModel = RpcParam::GetString(P, TEXT("riderModel"));
		const bool bAllowOverlap = RpcParam::GetBool(P, TEXT("allowOverlap"), false);

		// 옷: 걷는 사람은 outfit·outfitSeed, 탑승자는 riderOutfit·riderOutfitSeed. 해당 없는 쪽은 읽지 않아 unknownParam 경고로 알린다.
		const bool bVehicle = Kind != ParkAgent::EKind::Pedestrian;
		FParkAgentOutfit Outfit;
		const bool bPerson = !bVehicle || bRider;
		if (bPerson)
		{
			const TCHAR* ObjKey = bVehicle ? TEXT("riderOutfit") : TEXT("outfit");
			const TCHAR* SeedKey = bVehicle ? TEXT("riderOutfitSeed") : TEXT("outfitSeed");
			RpcParam::MarkRead(P, ObjKey);
			const TSharedPtr<FJsonObject>* OverPtr = nullptr;
			const bool bHasOver = P->TryGetObjectField(ObjKey, OverPtr) && OverPtr;
			FString FitErr;
			if (!ParkAgentOutfit::Resolve(bHasOver ? *OverPtr : TSharedPtr<FJsonObject>(), RpcParam::GetInt(P, SeedKey, 0), Outfit, FitErr))
			{
				E.FailDomain(FitErr, ERpcErrorKind::BadParams);
				return nullptr;
			}
		}

		AParkAgentManager* Mgr = AParkAgentManager::GetOrSpawn(GetWorldPtr());
		if (!Mgr) { E.FailDomain(TEXT("월드 없음(맵 미로드)"), ERpcErrorKind::Busy); return nullptr; }
		TArray<FString> Ids;
		FString Err;
		if (!Mgr->Create(Kind, FVector2D(X, Y), Yaw, bRider, Model, RiderModel, Ids, Err, bPerson ? &Outfit : nullptr, bAllowOverlap))
		{
			E.FailDomain(Err, ERpcErrorKind::BadParams);
			return nullptr;
		}
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetArrayField(TEXT("ids"), AgStrings(Ids));
		O->SetArrayField(TEXT("agents"), Mgr->ListJson(nullptr, Ids));
		O->SetArrayField(TEXT("relations"), Mgr->RelationsJson(Ids[0]));
		O->SetNumberField(TEXT("changed"), Ids.Num());
		// 없는 모델 이름은 id 로 고른 모델로 대신한다 — 조용히 넘기지 않고 고를 수 있는 이름을 알린다.
		TArray<TSharedPtr<FJsonValue>> Fallback;
		AgModelFallback(Kind, Model, Fallback);
		if (bRider && Kind != ParkAgent::EKind::Pedestrian) { AgModelFallback(ParkAgent::EKind::Pedestrian, RiderModel, Fallback, TEXT("riderModel")); }
		if (Fallback.Num() > 0) { O->SetArrayField(TEXT("modelFallback"), Fallback); }
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("agent.create"), { true, false,
		TEXT("{kind:\"pedestrian\"|\"bicycle\"|\"motorcycle\", pos{x,y} (UE m), yaw?=0, rider?=true, model?, riderModel?, outfit?, outfitSeed?, riderOutfit?, riderOutfitSeed?, allowOverlap?=false}"),
		TEXT("사람·자전거·오토바이 생성. 이동수단 + rider 면 사람·이동수단 각자 id + riding 관계. {ids, agents, relations, changed}") });

	Dispatcher.Register(TEXT("agent.move"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		FString Id;
		if (!RpcParam::RequireString(P, TEXT("id"), Id, E)) return nullptr;
		AParkAgentManager* Mgr = AParkAgentManager::Find(GetWorldPtr());
		if (!Mgr || !Mgr->Exists(Id)) { E.FailDomain(FString::Printf(TEXT("에이전트 없음: %s"), *Id), ERpcErrorKind::NotFound); return nullptr; }

		const TArray<TSharedPtr<FJsonValue>>* PathArr = nullptr;
		RpcParam::MarkRead(P, TEXT("path"));
		RpcParam::MarkRead(P, TEXT("to"));
		const bool bHasPath = P->TryGetArrayField(TEXT("path"), PathArr);
		const bool bHasTo = P->HasField(TEXT("to"));
		if (bHasPath == bHasTo)
		{
			E.FailDomain(TEXT("path ([{x,y},...]) 와 to ({x,y}) 중 하나만 주세요"), ERpcErrorKind::BadParams);
			return nullptr;
		}
		TArray<FVector2D> Path;
		FVector2D To = FVector2D::ZeroVector;
		if (bHasPath)
		{
			if (PathArr->Num() == 0) { E.FailDomain(TEXT("path 가 비었습니다"), ERpcErrorKind::BadParams); return nullptr; }
			for (int32 i = 0; i < PathArr->Num(); ++i)
			{
				FVector2D Pt;
				if (!AgPoint((*PathArr)[i], Pt)) { E.FailDomain(FString::Printf(TEXT("path[%d] 는 {x,y} 가 필수입니다"), i), ERpcErrorKind::BadParams); return nullptr; }
				Path.Add(Pt);
			}
		}
		else if (!AgPoint(P->TryGetField(TEXT("to")), To))
		{
			E.FailDomain(TEXT("to 는 {x,y} 입니다"), ERpcErrorKind::BadParams);
			return nullptr;
		}
		const double Speed = RpcParam::GetFloat(P, TEXT("speedMps"), 0.0);
		const bool bAvoid = RpcParam::GetBool(P, TEXT("avoid"), true);
		const double BlockAfter = RpcParam::GetFloat(P, TEXT("blockAfterSec"), 20.0);
		if (BlockAfter <= 0.0) { E.FailDomain(TEXT("blockAfterSec 는 0 보다 커야 합니다"), ERpcErrorKind::BadParams); return nullptr; }

		TSharedPtr<FJsonObject> Info;
		FString Err;
		bool bBad = false;
		if (!Mgr->StartMove(Id, bHasPath ? &Path : nullptr, bHasPath ? nullptr : &To, Speed, bAvoid, BlockAfter, Info, Err, bBad))
		{
			E.FailDomain(Err, bBad ? ERpcErrorKind::BadParams : ERpcErrorKind::Internal);
			return nullptr;
		}
		Info->SetNumberField(TEXT("changed"), 1);
		return RpcDto::MakeObject(Info);
	});
	Dispatcher.SetMethodMeta(TEXT("agent.move"), { true, false,
		TEXT("{id, path?:[{x,y}] | to?:{x,y}, speedMps?, avoid?=true, blockAfterSec?=20}"),
		TEXT("경로(path) 또는 목적지(to)로 이동. 탑승자 id 면 이동수단이 움직인다. {moveId, mover, source, pts, lengthM, speedMps, etaSec, laneRoute?, notes}") });

	Dispatcher.Register(TEXT("agent.stop"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		TArray<FString> Ids;
		bool bAll = false;
		if (!AgTargets(P, Ids, bAll)) { E.FailDomain(TEXT("id, ids[] 또는 all:true 가 필요합니다"), ERpcErrorKind::BadParams); return nullptr; }
		AParkAgentManager* Mgr = AParkAgentManager::Find(GetWorldPtr());
		TArray<FString> NotFound;
		const int32 N = Mgr ? Mgr->Stop(Ids, bAll, NotFound) : 0;
		if (!Mgr && !bAll) { NotFound = Ids; }
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("stopped"), N);
		O->SetNumberField(TEXT("changed"), N);
		O->SetArrayField(TEXT("notFound"), AgStrings(NotFound));
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("agent.stop"), { true, false, TEXT("{id? | ids?:[str] | all?:true}"),
		TEXT("그 자리에 선다(state stopped). 탑승자 id 면 이동수단이 선다. {stopped, changed, notFound}") });

	Dispatcher.Register(TEXT("agent.setPose"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
		RpcParam::MarkRead(P, TEXT("items"));
		if (!P.IsValid() || !P->TryGetArrayField(TEXT("items"), Items) || Items->Num() == 0)
		{
			E.FailDomain(TEXT("필수 파라미터 누락: items ([{id, pos{x,y,z?}, yawDeg, speedMps?, activity?}])"), ERpcErrorKind::BadParams);
			return nullptr;
		}
		// 항목 하나라도 틀리면 아무것도 옮기지 않는다.
		TArray<FParkAgentPose> Poses;
		for (int32 i = 0; i < Items->Num(); ++i)
		{
			const auto Bad = [&E, i](const FString& What) -> TSharedPtr<FJsonValue>
			{
				E.FailDomain(FString::Printf(TEXT("items[%d]: %s"), i, *What), ERpcErrorKind::BadParams);
				return nullptr;
			};
			const TSharedPtr<FJsonObject>* Item = nullptr;
			if (!(*Items)[i].IsValid() || !(*Items)[i]->TryGetObject(Item) || !Item) { return Bad(TEXT("객체여야 합니다")); }
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Kv : (*Item)->Values)
			{
				if (Kv.Key != TEXT("id") && Kv.Key != TEXT("pos") && Kv.Key != TEXT("yawDeg") && Kv.Key != TEXT("speedMps") && Kv.Key != TEXT("activity"))
				{
					return Bad(FString::Printf(TEXT("모르는 키 %s"), *Kv.Key));
				}
			}
			FParkAgentPose Pose;
			if (!(*Item)->TryGetStringField(TEXT("id"), Pose.Id) || Pose.Id.IsEmpty()) { return Bad(TEXT("id 가 필요합니다")); }
			if (!AgPoint((*Item)->TryGetField(TEXT("pos")), Pose.PosM)) { return Bad(TEXT("pos {x,y} 가 필요합니다")); }
			const TSharedPtr<FJsonObject> PosObj = (*Item)->GetObjectField(TEXT("pos"));
			if (PosObj->HasField(TEXT("z")))
			{
				if (!PosObj->TryGetNumberField(TEXT("z"), Pose.ZM)) { return Bad(TEXT("pos.z 는 숫자입니다")); }
				Pose.bHasZ = true;
			}
			if (!(*Item)->TryGetNumberField(TEXT("yawDeg"), Pose.YawDeg)) { return Bad(TEXT("yawDeg 가 필요합니다")); }
			if ((*Item)->HasField(TEXT("speedMps")) && (!(*Item)->TryGetNumberField(TEXT("speedMps"), Pose.SpeedMps) || Pose.SpeedMps < 0.0))
			{
				return Bad(TEXT("speedMps 는 0 이상의 숫자입니다"));
			}
			if ((*Item)->HasField(TEXT("activity"))
				&& (!(*Item)->TryGetStringField(TEXT("activity"), Pose.Activity) || (Pose.Activity != TEXT("walking") && Pose.Activity != TEXT("standing"))))
			{
				return Bad(TEXT("activity 는 walking | standing 입니다"));
			}
			Poses.Add(MoveTemp(Pose));
		}

		AParkAgentManager* Mgr = AParkAgentManager::Find(GetWorldPtr());
		int32 Applied = 0;
		TArray<FString> NotFound;
		if (Mgr)
		{
			FString Err;
			if (!Mgr->SetPoses(Poses, Applied, NotFound, Err))
			{
				E.FailDomain(Err, ERpcErrorKind::BadParams);
				return nullptr;
			}
		}
		else
		{
			for (const FParkAgentPose& Pose : Poses) { NotFound.Add(Pose.Id); }
		}
		if (Applied == 0)
		{
			E.FailDomain(FString::Printf(TEXT("에이전트 없음: %s"), *FString::Join(NotFound, TEXT(", "))), ERpcErrorKind::NotFound);
			return nullptr;
		}
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("applied"), Applied);
		O->SetNumberField(TEXT("changed"), Applied);
		O->SetArrayField(TEXT("notFound"), AgStrings(NotFound));
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("agent.setPose"), { true, false,
		TEXT("{items:[{id, pos{x,y,z?} (UE m), yawDeg, speedMps?=0, activity?:\"walking\"|\"standing\"}]}"),
		TEXT("검사·회피 없이 그 자세로 바로 옮긴다(여러 개 한 번에). 돌던 이동은 끝난다. speedMps 는 표시용으로 걷기 애니메이션 빠르기를 정한다. {applied, changed, notFound}") });

	Dispatcher.Register(TEXT("agent.delete"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		TArray<FString> Ids;
		bool bAll = false;
		if (!AgTargets(P, Ids, bAll)) { E.FailDomain(TEXT("id, ids[] 또는 all:true 가 필요합니다"), ERpcErrorKind::BadParams); return nullptr; }
		AParkAgentManager* Mgr = AParkAgentManager::Find(GetWorldPtr());
		TArray<FString> NotFound;
		const int32 N = Mgr ? Mgr->Delete(Ids, bAll, NotFound) : 0;
		if (!Mgr && !bAll) { NotFound = Ids; }
		if (!bAll && N == 0 && NotFound.Num() > 0)
		{
			E.FailDomain(FString::Printf(TEXT("에이전트 없음: %s"), *FString::Join(NotFound, TEXT(", "))), ERpcErrorKind::NotFound);
			return nullptr;
		}
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("deleted"), N);
		O->SetNumberField(TEXT("changed"), N);
		O->SetArrayField(TEXT("notFound"), AgStrings(NotFound));
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("agent.delete"), { true, true, TEXT("{id? | ids?:[str] | all?:true}"),
		TEXT("삭제. 탑승 중인 이동수단을 지우면 사람은 오른쪽 0.8 m 에 내려 선다(관계 종료). 모르는 id 만 주면 not_found. {deleted, changed, notFound}") });

	Dispatcher.Register(TEXT("agent.list"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		const FString KindS = RpcParam::GetString(P, TEXT("kind"));
		ParkAgent::EKind Kind;
		const bool bKind = !KindS.IsEmpty();
		if (bKind && !ParkAgent::ParseKind(KindS, Kind))
		{
			E.FailDomain(FString::Printf(TEXT("kind 는 pedestrian | bicycle | motorcycle 중 하나입니다: %s"), *KindS), ERpcErrorKind::BadParams);
			return nullptr;
		}
		TArray<FString> Ids;
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		RpcParam::MarkRead(P, TEXT("ids"));
		if (P.IsValid() && P->TryGetArrayField(TEXT("ids"), Arr)) { for (const TSharedPtr<FJsonValue>& V : *Arr) { Ids.Add(V->AsString()); } }
		const AParkAgentManager* Mgr = AParkAgentManager::Find(GetWorldPtr());
		const TArray<TSharedPtr<FJsonValue>> List = Mgr ? Mgr->ListJson(bKind ? &Kind : nullptr, Ids) : TArray<TSharedPtr<FJsonValue>>();
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("count"), List.Num());
		O->SetArrayField(TEXT("agents"), List);
		O->SetNumberField(TEXT("timeMs"), Park3DRpc::NowMs());
		O->SetNumberField(TEXT("frameId"), static_cast<double>(Park3DRpc::SceneSeq()));
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("agent.list"), { false, false, TEXT("{kind?, ids?:[str]}"),
		TEXT("{count, agents[{id, objectType, model, pos, directionDeg, speedMps, motionType, activityType?, outfit?, gripGapCm?, pedalGapCm?, state, moveId?, remainingM?, waitingFor?, endReason?, ridingWith, relationId?, plate?, plateVisible?}], timeMs, frameId}") });

	Dispatcher.Register(TEXT("agent.relations"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		const FString Id = RpcParam::GetString(P, TEXT("id"));
		const AParkAgentManager* Mgr = AParkAgentManager::Find(GetWorldPtr());
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetArrayField(TEXT("relations"), Mgr ? Mgr->RelationsJson(Id) : TArray<TSharedPtr<FJsonValue>>());
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("agent.relations"), { false, false, TEXT("{id?}"),
		TEXT("{relations[{relationId, relationType:\"riding\", fromObjectId(사람), toObjectId(이동수단), validFromMs, validToMs|null, active}]}") });

	Dispatcher.Register(TEXT("agent.grid"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkAgentManager* Mgr = AParkAgentManager::GetOrSpawn(GetWorldPtr());
		if (!Mgr) { E.FailDomain(TEXT("월드 없음(맵 미로드)"), ERpcErrorKind::Busy); return nullptr; }
		TArray<FVector2D> Probe;
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		RpcParam::MarkRead(P, TEXT("probe"));
		if (P.IsValid() && P->TryGetArrayField(TEXT("probe"), Arr))
		{
			for (int32 i = 0; i < Arr->Num(); ++i)
			{
				FVector2D Pt;
				if (!AgPoint((*Arr)[i], Pt)) { E.FailDomain(FString::Printf(TEXT("probe[%d] 는 {x,y} 입니다"), i), ERpcErrorKind::BadParams); return nullptr; }
				Probe.Add(Pt);
			}
		}
		return RpcDto::MakeObject(Mgr->GridJson(RpcParam::GetBool(P, TEXT("rebuild"), false), RpcParam::GetBool(P, TEXT("withCells"), false), Probe));
	});
	Dispatcher.SetMethodMeta(TEXT("agent.grid"), { false, false, TEXT("{rebuild?, withCells?, probe?:[{x,y}]}"),
		TEXT("보행 격자 진단. {built, cell, w, h, origin, groundZ, counts{blocked,ground,raised,bay}, probe[{cls,heightM,dzM}], cells?}") });
}
