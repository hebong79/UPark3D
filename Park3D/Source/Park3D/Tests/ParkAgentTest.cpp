// Copyright Epic Games, Inc. All Rights Reserved.
// ParkAgentTest : 사람·자전거·오토바이(agent.*) 검증.
//   Nav  — 월드 없이 합성 격자로: 벽 틈 A*, 보도(사람만), 경로 검사(벽·서 있는 차), 움직이는 차 충돌 예측.
//   Life — 에디터 월드에서 매니저로: 탑승 생성·관계, 탑승자 id 로 이동, 스냅샷 복원(이어 달리기·정지), 정지, 삭제(내려 서기).
// 도우미 이름은 Pa 접두사 — 유니티 빌드에서 다른 테스트 .cpp 와 한 TU 로 묶여도 겹치지 않게.

#include "Misc/AutomationTest.h"
#include "../Sim/ParkAgentNav.h"
#include "../Sim/ParkAgentManager.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using namespace ParkAgent;

	/** 20 x 10 m. x = 10 m 에 벽(두께 0.4 m), y 4~6 m 만 틈. y < 1.5 m 는 보도(높은 칸). */
	FAgentGrid PaWallGrid()
	{
		FAgentGrid G;
		G.Cell = 0.2;
		G.Origin = FVector2D(0, 0);
		G.W = FMath::RoundToInt(20.0 / G.Cell) + 1;
		G.H = FMath::RoundToInt(10.0 / G.Cell) + 1;
		G.Cls.Init(CellGround, G.W * G.H);
		G.HeightM.Init(0.f, G.W * G.H);
		for (int32 Y = 0; Y < G.H; ++Y)
		{
			for (int32 X = 0; X < G.W; ++X)
			{
				const FVector2D P = G.CellCenter(X, Y);
				uint8& C = G.Cls[Y * G.W + X];
				if (FMath::Abs(P.X - 10.0) < 0.2 && (P.Y < 4.0 || P.Y > 6.0)) { C = CellBlocked; }
				else if (P.Y < 1.5) { C = CellRaised; }
			}
		}
		return G;
	}

	double PaMinClearAlong(const FAgentGrid& G, const TArray<float>& Clear, const TArray<FVector2D>& Pts)
	{
		double Min = DBL_MAX;
		TArray<double> Cum;
		CumLengths(Pts, Cum);
		for (double S = 0.0; S <= Cum.Last(); S += 0.05)
		{
			FVector2D P, D;
			PoseAt(Pts, Cum, S, P, D);
			int32 X, Y;
			if (G.ToCell(P, X, Y)) { Min = FMath::Min(Min, static_cast<double>(Clear[Y * G.W + X])); }
		}
		return Min;
	}

	/** 경로가 x = X 를 처음 지나는 곳의 y(못 지나면 -1). */
	double PaCrossY(const TArray<FVector2D>& Pts, double X)
	{
		for (int32 i = 0; i + 1 < Pts.Num(); ++i)
		{
			const FVector2D A = Pts[i], B = Pts[i + 1];
			if ((A.X - X) * (B.X - X) <= 0.0 && FMath::Abs(B.X - A.X) > 1e-9)
			{
				return FMath::Lerp(A.Y, B.Y, (X - A.X) / (B.X - A.X));
			}
		}
		return -1.0;
	}

	TSharedPtr<FJsonObject> PaFind(const TArray<TSharedPtr<FJsonValue>>& List, const FString& Id)
	{
		for (const TSharedPtr<FJsonValue>& V : List)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			if (O.IsValid() && O->GetStringField(TEXT("id")) == Id) { return O; }
		}
		return nullptr;
	}

	FVector2D PaPos(const TSharedPtr<FJsonObject>& O)
	{
		const TSharedPtr<FJsonObject> P = O->GetObjectField(TEXT("pos"));
		return FVector2D(P->GetNumberField(TEXT("x")), P->GetNumberField(TEXT("y")));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParkAgentNavTest, "Park3D.Agent.Nav",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FParkAgentNavTest::RunTest(const FString& Parameters)
{
	const FAgentGrid G = PaWallGrid();

	// 1) 사람: 벽 틈으로 돈다. 경로 전 구간이 벽에서 몸 반경 이상 떨어져 있고 검사도 통과한다.
	{
		TArray<float> Clear;
		ClearanceField(G, EKind::Pedestrian, nullptr, Clear);
		const FFootprint Fp = FootprintOf(EKind::Pedestrian);
		TArray<FVector2D> Pts;
		FString Err;
		TestTrue(TEXT("사람 A* 성공"), FindPath(G, Clear, Fp.Radius + 0.3, FVector2D(5, 8), FVector2D(15, 8), Pts, Err));
		const double CrossY = PaCrossY(Pts, 10.0);
		TestTrue(FString::Printf(TEXT("벽 틈(y 4~6)을 지난다(y=%.2f)"), CrossY), CrossY > 4.0 && CrossY < 6.0);
		TestTrue(TEXT("경로 여유 ≥ 몸 반경"), PaMinClearAlong(G, Clear, Pts) >= Fp.Radius);
		FVector2D At; FString Who;
		TestEqual(TEXT("찾은 경로 검사 = 통과"), CheckPath(G, Clear, {}, Fp, Pts, 0.2, At, Who), 0);
		TestEqual(TEXT("벽을 곧장 가로지르는 경로 = 벽"), CheckPath(G, Clear, {}, Fp, { FVector2D(5, 8), FVector2D(15, 8) }, 0.2, At, Who), 1);
		TestTrue(TEXT("걸린 자리 = 벽 근처"), FMath::Abs(At.X - 10.0) < 0.6);
	}

	// 2) 보도(높은 칸): 사람은 걷고, 이동수단은 못 간다.
	{
		TArray<float> PedClear, BikeClear;
		ClearanceField(G, EKind::Pedestrian, nullptr, PedClear);
		ClearanceField(G, EKind::Bicycle, nullptr, BikeClear);
		const TArray<FVector2D> Walk = { FVector2D(2, 0.7), FVector2D(8, 0.7) };
		FVector2D At; FString Who;
		TestEqual(TEXT("사람 보도 직진 = 통과"), CheckPath(G, PedClear, {}, FootprintOf(EKind::Pedestrian), Walk, 0.2, At, Who), 0);
		TestEqual(TEXT("자전거 보도 직진 = 막힘"), CheckPath(G, BikeClear, {}, FootprintOf(EKind::Bicycle), Walk, 0.2, At, Who), 1);
		TestEqual(TEXT("막힌 까닭 = 보도"), Who, FString(TEXT("보도")));
		TArray<FVector2D> Pts;
		FString Err;
		TestFalse(TEXT("자전거는 보도 안 목적지로 못 간다"), FindPath(G, BikeClear, 0.5, FVector2D(2, 8), FVector2D(5, 0.5), Pts, Err, 0.3));
	}

	// 3) 서 있는 차: 경로 검사가 차를 잡고, A* 는 차를 비켜 간다.
	{
		FCarBox Car;
		Car.Center = FVector2D(5, 8);
		Car.YawDeg = 0.0;
		Car.Half = FVector2D(2.3, 0.95);
		Car.Id = TEXT("car-A");
		FVector2D At; FString Who;
		TArray<float> Clear;
		ClearanceField(G, EKind::Pedestrian, nullptr, Clear);
		TestEqual(TEXT("차를 지나는 경로 = 차"), CheckPath(G, Clear, { Car }, FootprintOf(EKind::Pedestrian), { FVector2D(1, 8), FVector2D(9, 8) }, 0.2, At, Who), 2);
		TestEqual(TEXT("걸린 차 id"), Who, FString(TEXT("car-A")));
		TArray<uint8> Mask;
		RasterizeBoxes(G, { Car }, 0.0, Mask);
		TArray<float> WithCar;
		ClearanceField(G, EKind::Pedestrian, &Mask, WithCar);
		TArray<FVector2D> Pts;
		FString Err;
		TestTrue(TEXT("차 비켜 A*"), FindPath(G, WithCar, 0.7, FVector2D(1, 8), FVector2D(9, 8), Pts, Err));
		TestEqual(TEXT("비켜 간 경로 검사 = 통과"), CheckPath(G, Clear, { Car }, FootprintOf(EKind::Pedestrian), Pts, 0.2, At, Who), 0);
	}

	// 4) 움직이는 차 예측: 앞을 가로지르는 차 → 충돌, 뒤에서 따라오는 차·비켜 선 차 → 없음.
	{
		const TArray<FVector2D> Path = { FVector2D(0, 0), FVector2D(20, 0) };
		TArray<double> Cum;
		CumLengths(Path, Cum);
		const FFootprint Fp = FootprintOf(EKind::Pedestrian);
		FConflictParams Cp;
		double T = 0.0; FString Who;

		FCarBox Cross;
		Cross.Center = FVector2D(2.0, -5.0);
		Cross.YawDeg = 90.0;
		Cross.Vel = FVector2D(0, 3.0);
		Cross.Id = TEXT("cross");
		TestTrue(TEXT("가로지르는 차 → 충돌 예측"), PredictConflict(Path, Cum, 0.0, 1.3, Fp, { Cross }, Cp, T, Who));
		TestEqual(TEXT("상대 id"), Who, FString(TEXT("cross")));
		TestTrue(TEXT("2초 안"), T <= Cp.HorizonSec + 1e-6);

		FCarBox Behind;
		Behind.Center = FVector2D(-6.0, 0.0);
		Behind.YawDeg = 0.0;
		Behind.Vel = FVector2D(3.0, 0);
		Behind.Id = TEXT("behind");
		TestFalse(TEXT("뒤에서 같은 방향으로 오는 차는 피하지 않는다"), PredictConflict(Path, Cum, 0.0, 1.3, Fp, { Behind }, Cp, T, Who));

		FCarBox Aside;
		Aside.Center = FVector2D(2.0, 3.0);
		Aside.YawDeg = 0.0;
		Aside.Id = TEXT("aside");
		TestFalse(TEXT("옆에 서 있는 차는 길을 막지 않는다"), PredictConflict(Path, Cum, 0.0, 1.3, Fp, { Aside }, Cp, T, Who));

		FCarBox Far = Cross;
		Far.Center = FVector2D(2.0, -30.0);
		TestFalse(TEXT("멀리서 오는 차(2초 안에 못 옴)는 아직 아니다"), PredictConflict(Path, Cum, 0.0, 1.3, Fp, { Far }, Cp, T, Who));
	}

	// 5) 종류 문자열·기본 속도.
	{
		EKind K;
		TestTrue(TEXT("person = pedestrian"), ParseKind(TEXT("person"), K) && K == EKind::Pedestrian);
		TestFalse(TEXT("car 는 에이전트가 아님"), ParseKind(TEXT("car"), K));
		TestEqual(TEXT("objectType 오토바이"), FString(ObjectType(EKind::Motorcycle)), FString(TEXT("motorcycle")));
		TestEqual(TEXT("오토바이 상한 = 통로 순항 3.0"), MaxSpeed(EKind::Motorcycle), 3.0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParkAgentLifeTest, "Park3D.Agent.Life",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParkAgentLifeTest::RunTest(const FString& Parameters)
{
	UWorld* World = (GEngine && GEngine->GetWorldContexts().Num() > 0) ? GWorld : nullptr;
	if (!TestNotNull(TEXT("에디터 월드"), World)) { return false; }
	AParkAgentManager* M = AParkAgentManager::GetOrSpawn(World);
	if (!TestNotNull(TEXT("매니저"), M)) { return false; }
	TArray<FString> NotFound;
	M->Delete({}, true, NotFound);

	// 주차장에서 멀리(격자 밖) — 레벨 지오메트리와 무관하게 돈다.
	const FVector2D Base(3000.0, 3000.0);
	TArray<FString> Ids;
	FString Err;
	TestTrue(TEXT("자전거+탑승자 생성"), M->Create(EKind::Bicycle, Base, 0.0, true, FString(), FString(), Ids, Err));
	if (!TestEqual(TEXT("id 2개(이동수단, 사람)"), Ids.Num(), 2)) { return false; }
	const FString Bike = Ids[0], Rider = Ids[1];
	TestTrue(TEXT("이동수단 id"), Bike.StartsWith(TEXT("bicycle-")));
	TestTrue(TEXT("사람 id"), Rider.StartsWith(TEXT("person-")));
	{
		const TArray<TSharedPtr<FJsonValue>> Rel = M->RelationsJson(Rider);
		TestEqual(TEXT("관계 1건"), Rel.Num(), 1);
		if (Rel.Num() == 1)
		{
			const TSharedPtr<FJsonObject> R = Rel[0]->AsObject();
			TestEqual(TEXT("riding"), R->GetStringField(TEXT("relationType")), FString(TEXT("riding")));
			TestEqual(TEXT("from = 사람"), R->GetStringField(TEXT("fromObjectId")), Rider);
			TestEqual(TEXT("to = 자전거"), R->GetStringField(TEXT("toObjectId")), Bike);
			TestTrue(TEXT("진행 중(validToMs null)"), R->GetBoolField(TEXT("active")));
		}
	}

	// 탑승자 id 로 이동 → 자전거가 움직이고 사람이 따라간다.
	TSharedPtr<FJsonObject> Info;
	bool bBad = false;
	const TArray<FVector2D> Path = { Base + FVector2D(10.0, 0.0) };
	TestTrue(TEXT("이동 시작"), M->StartMove(Rider, &Path, nullptr, 4.0, true, 20.0, Info, Err, bBad));
	TestEqual(TEXT("mover = 자전거"), Info.IsValid() ? Info->GetStringField(TEXT("mover")) : FString(), Bike);
	for (int32 i = 0; i < 10; ++i) { M->Advance(0.1f); }
	{
		const TArray<TSharedPtr<FJsonValue>> L = M->ListJson(nullptr, {});
		const TSharedPtr<FJsonObject> B = PaFind(L, Bike), R = PaFind(L, Rider);
		if (TestTrue(TEXT("목록에 둘 다"), B.IsValid() && R.IsValid()))
		{
			TestEqual(TEXT("자전거 moving"), B->GetStringField(TEXT("state")), FString(TEXT("moving")));
			TestTrue(TEXT("자전거가 앞으로 갔다"), PaPos(B).X > Base.X + 0.3);
			TestTrue(TEXT("속도 > 0"), B->GetNumberField(TEXT("speedMps")) > 0.5);
			TestTrue(TEXT("사람 = 자전거 자리"), PaPos(R).Equals(PaPos(B), 1e-3));
			TestEqual(TEXT("사람 riding"), R->GetStringField(TEXT("activityType")), FString(TEXT("riding")));
			TestEqual(TEXT("사람 ridingWith"), R->GetStringField(TEXT("ridingWith")), Bike);
		}
	}

	// 스냅샷 → 더 달림 → 복원(이어 달리기): 같은 id·스냅샷 자리에서 다시 가 도착한다.
	const FParkAgentSnapshot Snap = M->Capture();
	const FVector2D SnapPos = PaPos(PaFind(M->ListJson(nullptr, {}), Bike));
	for (int32 i = 0; i < 15; ++i) { M->Advance(0.1f); }
	TestEqual(TEXT("복원 수"), M->Restore(Snap, false), 2);
	{
		const TSharedPtr<FJsonObject> B = PaFind(M->ListJson(nullptr, {}), Bike);
		if (TestTrue(TEXT("복원 뒤 같은 id"), B.IsValid()))
		{
			TestTrue(TEXT("스냅샷 자리"), PaPos(B).Equals(SnapPos, 1e-3));
			TestEqual(TEXT("계속 moving"), B->GetStringField(TEXT("state")), FString(TEXT("moving")));
		}
		TestTrue(TEXT("탑승자도 복원"), PaFind(M->ListJson(nullptr, {}), Rider).IsValid());
	}
	for (int32 i = 0; i < 100; ++i) { M->Advance(0.1f); }
	{
		const TSharedPtr<FJsonObject> B = PaFind(M->ListJson(nullptr, {}), Bike);
		TestEqual(TEXT("도착"), B->GetStringField(TEXT("state")), FString(TEXT("arrived")));
		TestTrue(TEXT("목적지"), PaPos(B).Equals(Base + FVector2D(10.0, 0.0), 1e-3));
		TestEqual(TEXT("도착 뒤 속도 0"), B->GetNumberField(TEXT("speedMps")), 0.0);
	}

	// 복원(paused): 그 자리에 선다.
	M->Restore(Snap, true);
	for (int32 i = 0; i < 10; ++i) { M->Advance(0.1f); }
	{
		const TSharedPtr<FJsonObject> B = PaFind(M->ListJson(nullptr, {}), Bike);
		TestEqual(TEXT("paused → stopped"), B->GetStringField(TEXT("state")), FString(TEXT("stopped")));
		TestTrue(TEXT("paused → 안 움직임"), PaPos(B).Equals(SnapPos, 1e-3));
	}

	// 사람 혼자 생성 → 이동 → 정지.
	TArray<FString> PedIds;
	TestTrue(TEXT("사람 생성"), M->Create(EKind::Pedestrian, Base + FVector2D(0.0, 5.0), 90.0, true, FString(), FString(), PedIds, Err));
	const FString Ped = PedIds.Num() > 0 ? PedIds[0] : FString();
	const TArray<FVector2D> Walk = { Base + FVector2D(0.0, 15.0) };
	TestTrue(TEXT("사람 이동"), M->StartMove(Ped, &Walk, nullptr, 0.0, true, 20.0, Info, Err, bBad));
	TestEqual(TEXT("기본 속도 1.3"), Info.IsValid() ? Info->GetNumberField(TEXT("speedMps")) : 0.0, 1.3);
	for (int32 i = 0; i < 10; ++i) { M->Advance(0.1f); }
	{
		const TSharedPtr<FJsonObject> P = PaFind(M->ListJson(nullptr, {}), Ped);
		TestEqual(TEXT("걷는 중"), P->GetStringField(TEXT("activityType")), FString(TEXT("walking")));
	}
	TestEqual(TEXT("정지 1건"), M->Stop({ Ped }, false, NotFound), 1);
	{
		const TSharedPtr<FJsonObject> P = PaFind(M->ListJson(nullptr, {}), Ped);
		TestEqual(TEXT("stopped"), P->GetStringField(TEXT("state")), FString(TEXT("stopped")));
		TestEqual(TEXT("서 있음"), P->GetStringField(TEXT("activityType")), FString(TEXT("standing")));
	}

	// 자전거 삭제 → 사람은 내려 서고 관계가 끝난다.
	TestEqual(TEXT("자전거 삭제"), M->Delete({ Bike }, false, NotFound), 1);
	{
		const TSharedPtr<FJsonObject> R = PaFind(M->ListJson(nullptr, {}), Rider);
		if (TestTrue(TEXT("사람은 남는다"), R.IsValid()))
		{
			TestTrue(TEXT("ridingWith null"), R->HasTypedField<EJson::Null>(TEXT("ridingWith")));
			TestEqual(TEXT("standing"), R->GetStringField(TEXT("activityType")), FString(TEXT("standing")));
		}
		const TArray<TSharedPtr<FJsonValue>> Rel = M->RelationsJson(Rider);
		TestTrue(TEXT("관계 종료"), Rel.Num() == 1 && !Rel[0]->AsObject()->GetBoolField(TEXT("active")));
	}

	M->Delete({}, true, NotFound);
	TestEqual(TEXT("전부 삭제"), M->Num(), 0);
	M->Destroy();
	return true;
}

#endif
