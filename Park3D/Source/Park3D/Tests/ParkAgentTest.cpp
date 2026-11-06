// Copyright Epic Games, Inc. All Rights Reserved.
// ParkAgentTest : 사람·자전거·오토바이(agent.*) 검증.
//   Nav  — 월드 없이 합성 격자로: 벽 틈 A*, 보도(사람만), 경로 검사(벽·서 있는 차), 움직이는 차 충돌 예측.
//   Life — 에디터 월드에서 매니저로: 탑승 생성·관계, 탑승자 id 로 이동, 스냅샷 복원(이어 달리기·정지), 정지, 삭제(내려 서기).
// 도우미 이름은 Pa 접두사 — 유니티 빌드에서 다른 테스트 .cpp 와 한 TU 로 묶여도 겹치지 않게.

#include "Misc/AutomationTest.h"
#include "../Sim/ParkAgentNav.h"
#include "../Sim/ParkAgentManager.h"
#include "../Rpc/RpcDispatcher.h"
#include "../Rpc/Modules/AgentRpcModule.h"
#include "../Sim/ParkAgentActor.h"
#include "Components/PrimitiveComponent.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParkAgentOutfitTest, "Park3D.Agent.Outfit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FParkAgentOutfitTest::RunTest(const FString& Parameters)
{
	// 값 — 월드 없이.
	const FParkAgentOutfit A = ParkAgentOutfit::Draw(7), B = ParkAgentOutfit::Draw(7);
	TestTrue(TEXT("같은 seed = 같은 옷"), A == B);
	TestEqual(TEXT("seed 를 담는다"), A.Seed, 7);
	TestTrue(TEXT("무늬 둘째 색은 윗옷과 다르다"), A.Top != A.TopAlt);
	TSet<FString> Tops, Sleeves, Patterns;
	for (int32 S = 1; S <= 200; ++S)
	{
		const FParkAgentOutfit O = ParkAgentOutfit::Draw(S);
		Tops.Add(O.Top);
		Sleeves.Add(O.Sleeve);
		Patterns.Add(O.Pattern);
	}
	TestEqual(TEXT("이어진 seed 200 개에 윗옷 12색이 다 나온다"), Tops.Num(), 12);
	TestEqual(TEXT("소매 3종"), Sleeves.Num(), 3);
	TestEqual(TEXT("무늬 3종"), Patterns.Num(), 3);

	FParkAgentOutfit Out;
	FString Err;
	TSharedPtr<FJsonObject> Over = MakeShared<FJsonObject>();
	Over->SetStringField(TEXT("top"), TEXT("red"));
	TestTrue(TEXT("일부만 지정"), ParkAgentOutfit::Resolve(Over, 7, Out, Err));
	TestEqual(TEXT("지정한 값"), Out.Top, FString(TEXT("red")));
	TestEqual(TEXT("나머지는 seed 대로"), Out.BottomColor, A.BottomColor);
	Over->SetStringField(TEXT("top"), TEXT("없는색"));
	TestFalse(TEXT("없는 이름은 거절"), ParkAgentOutfit::Resolve(Over, 7, Out, Err));
	TestTrue(TEXT("오류에 고를 수 있는 이름"), Err.Contains(TEXT("skyblue")));
	TSharedPtr<FJsonObject> Bad = MakeShared<FJsonObject>();
	Bad->SetStringField(TEXT("hat"), TEXT("red"));
	TestFalse(TEXT("없는 키는 거절"), ParkAgentOutfit::Resolve(Bad, 7, Out, Err));

	// agent.list 의 outfit 을 그대로 다시 주면 같은 옷(applied 는 무시).
	TSharedPtr<FJsonObject> Back = ParkAgentOutfit::ToJson(A);
	Back->SetBoolField(TEXT("applied"), true);
	TestTrue(TEXT("목록 값 되돌리기"), ParkAgentOutfit::Resolve(Back, 0, Out, Err) && Out == A);

	// 월드 — 스냅샷 복원 뒤 같은 옷, 탑승자도 옷을 입는다.
	UWorld* World = (GEngine && GEngine->GetWorldContexts().Num() > 0) ? GWorld : nullptr;
	if (!TestNotNull(TEXT("에디터 월드"), World)) { return false; }
	AParkAgentManager* M = AParkAgentManager::GetOrSpawn(World);
	if (!TestNotNull(TEXT("매니저"), M)) { return false; }
	TArray<FString> NotFound;
	M->Delete({}, true, NotFound);
	const FVector2D Base(3000.0, 3000.0);
	TArray<FString> Ids;
	TestTrue(TEXT("걷는 사람"), M->Create(EKind::Pedestrian, Base, 0.0, true, FString(), FString(), Ids, Err, &A));
	TestTrue(TEXT("seed 생략 탑승자"), M->Create(EKind::Bicycle, Base + FVector2D(5.0, 0.0), 0.0, true, FString(), FString(), Ids, Err));
	if (!TestEqual(TEXT("id 3개"), Ids.Num(), 3)) { return false; }

	auto OutfitOf = [&](const FString& Id) { return PaFind(M->ListJson(nullptr, {}), Id)->GetObjectField(TEXT("outfit")); };
	const TSharedPtr<FJsonObject> Fit = OutfitOf(Ids[0]);
	TestEqual(TEXT("목록에 seed"), static_cast<int32>(Fit->GetNumberField(TEXT("seed"))), 7);
	TestEqual(TEXT("목록에 윗옷"), Fit->GetStringField(TEXT("top")), A.Top);
	TestTrue(TEXT("재질이 있으면 applied"), Fit->GetBoolField(TEXT("applied")));
	const TSharedPtr<FJsonObject> RiderFit = OutfitOf(Ids[2]);
	TestTrue(TEXT("탑승자 새 seed"), RiderFit->GetNumberField(TEXT("seed")) > 0.0);
	TestFalse(TEXT("이동수단에는 옷이 없다"), PaFind(M->ListJson(nullptr, {}), Ids[1])->HasField(TEXT("outfit")));

	const FParkAgentSnapshot Snap = M->Capture();
	M->Restore(Snap, true);
	TestEqual(TEXT("복원 뒤 같은 옷(걷는 사람)"), OutfitOf(Ids[0])->GetStringField(TEXT("top")), A.Top);
	TestEqual(TEXT("복원 뒤 같은 옷(탑승자)"), OutfitOf(Ids[2])->GetNumberField(TEXT("seed")), RiderFit->GetNumberField(TEXT("seed")));
	TestTrue(TEXT("복원 뒤 applied"), OutfitOf(Ids[2])->GetBoolField(TEXT("applied")));

	M->Delete({}, true, NotFound);
	M->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParkAgentGripTest, "Park3D.Agent.Grip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FParkAgentGripTest::RunTest(const FString& Parameters)
{
	UWorld* World = (GEngine && GEngine->GetWorldContexts().Num() > 0) ? GWorld : nullptr;
	if (!TestNotNull(TEXT("에디터 월드"), World)) { return false; }
	AParkAgentManager* M = AParkAgentManager::GetOrSpawn(World);
	if (!TestNotNull(TEXT("매니저"), M)) { return false; }
	TArray<FString> NotFound;
	M->Delete({}, true, NotFound);

	struct FCase { EKind Kind; const TCHAR* Model; };
	const FCase Cases[] = {
		{ EKind::Bicycle, TEXT("city") }, { EKind::Bicycle, TEXT("sport") }, { EKind::Bicycle, TEXT("simple") },
		{ EKind::Motorcycle, TEXT("scooter1") }, { EKind::Motorcycle, TEXT("evscooter2") }, { EKind::Motorcycle, TEXT("hondawave") }, { EKind::Motorcycle, TEXT("scooter3") },
	};
	const FVector2D Base(3000.0, 3000.0);
	FString Err;
	for (const FCase& C : Cases)
	{
		for (const TCHAR* Person : { TEXT("manny"), TEXT("quinn") })
		{
			TArray<FString> Ids;
			if (!TestTrue(FString::Printf(TEXT("%s 생성"), C.Model), M->Create(C.Kind, Base, 0.0, true, C.Model, Person, Ids, Err))) { continue; }
			const TSharedPtr<FJsonObject> Rider = PaFind(M->ListJson(nullptr, {}), Ids[1]);
			const TSharedPtr<FJsonObject>* Gap = nullptr;
			if (TestTrue(FString::Printf(TEXT("%s/%s gripGapCm"), C.Model, Person), Rider.IsValid() && Rider->TryGetObjectField(TEXT("gripGapCm"), Gap)))
			{
				// 손바닥이 손잡이 위 3 cm 에 놓이도록 풀었으니 뼈~손잡이 중심 거리도 그 근처여야 한다.
				const double L = (*Gap)->GetNumberField(TEXT("left")), R = (*Gap)->GetNumberField(TEXT("right"));
				AddInfo(FString::Printf(TEXT("%s/%s 좌 %.2f 우 %.2f cm"), C.Model, Person, L, R));
				TestTrue(FString::Printf(TEXT("%s/%s 왼손 %.2f cm"), C.Model, Person, L), L < 5.0);
				TestTrue(FString::Printf(TEXT("%s/%s 오른손 %.2f cm"), C.Model, Person, R), R < 5.0);
			}
			// 자전거만 페달을 밟는다. 간격 = 발볼 뼈의 바닥 높이(뼈~페달 윗면)라 IK 가 닿았으면 몇 cm 안이다.
			const bool bPedals = Rider.IsValid() && Rider->TryGetObjectField(TEXT("pedalGapCm"), Gap);
			TestTrue(FString::Printf(TEXT("%s/%s pedalGapCm 은 자전거에만"), C.Model, Person), bPedals == (C.Kind == EKind::Bicycle));
			if (bPedals)
			{
				const double L = (*Gap)->GetNumberField(TEXT("left")), R = (*Gap)->GetNumberField(TEXT("right"));
				AddInfo(FString::Printf(TEXT("%s/%s 발 좌 %.2f 우 %.2f cm"), C.Model, Person, L, R));
				TestTrue(FString::Printf(TEXT("%s/%s 왼발 %.2f cm"), C.Model, Person, L), L < 8.0);
				TestTrue(FString::Printf(TEXT("%s/%s 오른발 %.2f cm"), C.Model, Person, R), R < 8.0);
			}
			// 노면 도색 데칼이 바퀴·발에 찍히면 안 된다(사람·탑승자·차체 부품 전부).
			for (TActorIterator<AParkAgentActor> It(World); It; ++It)
			{
				TArray<UPrimitiveComponent*> Prims;
				It->GetComponents<UPrimitiveComponent>(Prims);
				TestTrue(FString::Printf(TEXT("%s/%s 부품 있음"), C.Model, Person), Prims.Num() > 0);
				for (const UPrimitiveComponent* Prim : Prims)
				{
					TestFalse(FString::Printf(TEXT("%s/%s %s 데칼 안 받음"), C.Model, Person, *Prim->GetName()), Prim->bReceivesDecals);
				}
			}
			M->Delete(Ids, false, NotFound);
		}
	}
	M->Delete({}, true, NotFound);
	M->Destroy();
	return true;
}

// ===================================================================== agent.setPose + allowOverlap

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParkAgentSetPoseTest, "Park3D.Agent.SetPose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParkAgentSetPoseTest::RunTest(const FString& Parameters)
{
	UWorld* World = (GEngine && GEngine->GetWorldContexts().Num() > 0) ? GWorld : nullptr;
	if (!TestNotNull(TEXT("에디터 월드"), World)) { return false; }
	AParkAgentManager* M = AParkAgentManager::GetOrSpawn(World);
	if (!TestNotNull(TEXT("매니저"), M)) { return false; }
	TArray<FString> NotFound;
	M->Delete({}, true, NotFound);

	URpcDispatcher* D = NewObject<URpcDispatcher>();
	FAgentRpcModule Module([World]() -> UWorld* { return World; });
	Module.Register(*D);

	TArray<FString> Unread;
	const auto Rpc = [&](const TCHAR* Method, const FString& Json, FRpcError& E) -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> Params;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		TestTrue(FString::Printf(TEXT("시험 JSON: %s"), *Json), FJsonSerializer::Deserialize(Reader, Params));
		TSharedPtr<FJsonValue> R;
		D->Dispatch(Method, Params, R, E, &Unread);
		return R.IsValid() ? R->AsObject() : nullptr;
	};
	const auto Listed = [&](const FString& Id) { return PaFind(M->ListJson(nullptr, {}), Id); };
	const auto Item = [](const FString& Id, const FVector2D& P, const TCHAR* Extra = TEXT("")) -> FString
	{
		return FString::Printf(TEXT("{\"id\":\"%s\",\"pos\":{\"x\":%.3f,\"y\":%.3f},\"yawDeg\":90%s}"), *Id, P.X, P.Y, Extra);
	};
	const auto Items = [](const FString& A, const FString& B = FString()) -> FString
	{
		return FString::Printf(TEXT("{\"items\":[%s%s%s]}"), *A, B.IsEmpty() ? TEXT("") : TEXT(","), *B);
	};

	const FVector2D Base(3000.0, 3000.0);
	FString Err;
	TArray<FString> Ids;
	TestTrue(TEXT("사람 1"), M->Create(EKind::Pedestrian, Base, 0.0, false, FString(), FString(), Ids, Err));
	const FString P1 = Ids.Num() > 0 ? Ids[0] : FString();
	Ids.Reset();
	TestTrue(TEXT("사람 2"), M->Create(EKind::Pedestrian, Base + FVector2D(0, 4), 0.0, false, FString(), FString(), Ids, Err));
	const FString P2 = Ids.Num() > 0 ? Ids[0] : FString();
	Ids.Reset();
	TestTrue(TEXT("탑승자 없는 자전거"), M->Create(EKind::Bicycle, Base + FVector2D(0, 8), 0.0, false, TEXT("city"), FString(), Ids, Err));
	const FString Bike = Ids.Num() > 0 ? Ids[0] : FString();
	Ids.Reset();
	TestTrue(TEXT("탑승 오토바이"), M->Create(EKind::Motorcycle, Base + FVector2D(0, 12), 0.0, true, FString(), FString(), Ids, Err));
	if (!TestEqual(TEXT("이동수단 + 탑승자"), Ids.Num(), 2)) { return false; }
	const FString Moto = Ids[0], Rider = Ids[1];

	// 여러 개를 한 번에, z 지정·속도 유지.
	{
		FRpcError E;
		const FString Walker = FString::Printf(TEXT("{\"id\":\"%s\",\"pos\":{\"x\":%.3f,\"y\":%.3f,\"z\":0.25},\"yawDeg\":90,\"speedMps\":1.3}"), *P1, Base.X + 5.0, Base.Y + 1.0);
		const TSharedPtr<FJsonObject> R = Rpc(TEXT("agent.setPose"), Items(Walker, Item(Bike, Base + FVector2D(8, 2))), E);
		TestFalse(TEXT("다건 성공"), E.HasError());
		TestEqual(TEXT("모르는 키 없음"), Unread.Num(), 0);
		if (R.IsValid())
		{
			TestEqual(TEXT("applied 2"), static_cast<int32>(R->GetNumberField(TEXT("applied"))), 2);
			TestEqual(TEXT("notFound 0"), R->GetArrayField(TEXT("notFound")).Num(), 0);
		}
		for (int32 i = 0; i < 10; ++i) { M->Advance(0.1f); }
		const TSharedPtr<FJsonObject> O = Listed(P1);
		TestTrue(TEXT("위치 그대로(스스로 안 움직임)"), PaPos(O).Equals(Base + FVector2D(5, 1), 1e-3));
		TestEqual(TEXT("z 지정"), O->GetObjectField(TEXT("pos"))->GetNumberField(TEXT("z")), 0.25, 1e-6);
		TestEqual(TEXT("방향"), O->GetNumberField(TEXT("directionDeg")), 90.0, 1e-6);
		TestEqual(TEXT("틱 뒤에도 속도"), O->GetNumberField(TEXT("speedMps")), 1.3, 1e-6);
		TestEqual(TEXT("state moving"), O->GetStringField(TEXT("state")), FString(TEXT("moving")));
		TestEqual(TEXT("motionType moving"), O->GetStringField(TEXT("motionType")), FString(TEXT("moving")));
		TestEqual(TEXT("activityType walking"), O->GetStringField(TEXT("activityType")), FString(TEXT("walking")));
		const TSharedPtr<FJsonObject> B = Listed(Bike);
		TestEqual(TEXT("속도 생략 = stopped"), B->GetStringField(TEXT("state")), FString(TEXT("stopped")));
		TestEqual(TEXT("속도 생략 = stationary"), B->GetStringField(TEXT("motionType")), FString(TEXT("stationary")));
	}
	// 속도 0 은 서 있다.
	{
		FRpcError E;
		Rpc(TEXT("agent.setPose"), Items(Item(P1, Base + FVector2D(5, 1), TEXT(",\"speedMps\":0"))), E);
		const TSharedPtr<FJsonObject> O = Listed(P1);
		TestEqual(TEXT("속도 0 = stopped"), O->GetStringField(TEXT("state")), FString(TEXT("stopped")));
		TestEqual(TEXT("속도 0 = standing"), O->GetStringField(TEXT("activityType")), FString(TEXT("standing")));
	}
	// 이동 중인 대상은 이동이 끝나고, 다음 이동은 새 자리에서 시작한다.
	{
		TSharedPtr<FJsonObject> Info;
		bool bBad = false;
		const TArray<FVector2D> Path = { Base + FVector2D(10, 4) };
		TestTrue(TEXT("이동 시작"), M->StartMove(P2, &Path, nullptr, 1.0, true, 20.0, Info, Err, bBad));
		M->Advance(0.5f);
		FRpcError E;
		const FVector2D Spot = Base + FVector2D(-6, 20);
		Rpc(TEXT("agent.setPose"), Items(Item(P2, Spot)), E);
		TestFalse(TEXT("이동 중 setPose"), E.HasError());
		for (int32 i = 0; i < 5; ++i) { M->Advance(0.1f); }
		TSharedPtr<FJsonObject> O = Listed(P2);
		TestTrue(TEXT("이동이 안 이어진다"), PaPos(O).Equals(Spot, 1e-3));
		TestEqual(TEXT("endReason setPose"), O->GetStringField(TEXT("endReason")), FString(TEXT("setPose")));

		const TArray<FVector2D> Path2 = { Spot + FVector2D(10, 0) };
		TestTrue(TEXT("다시 이동"), M->StartMove(P2, &Path2, nullptr, 1.0, true, 20.0, Info, Err, bBad));
		for (int32 i = 0; i < 10; ++i) { M->Advance(0.1f); }
		O = Listed(P2);
		TestTrue(TEXT("새 자리에서 출발"), FVector2D::Distance(PaPos(O), Spot) > 0.5 && FVector2D::Distance(PaPos(O), Spot) < 1.5);
		TestEqual(TEXT("다시 moving"), O->GetStringField(TEXT("state")), FString(TEXT("moving")));
		M->Stop({ P2 }, false, NotFound);
	}
	// 표시 속도는 agent.stop 과 agent.move 로 끝난다.
	{
		FRpcError E;
		Rpc(TEXT("agent.setPose"), Items(Item(P1, Base + FVector2D(5, 1), TEXT(",\"speedMps\":1.3"))), E);
		TestEqual(TEXT("표시 속도도 stopped 로 센다"), M->Stop({ P1 }, false, NotFound), 1);
		M->Advance(0.1f);
		TSharedPtr<FJsonObject> O = Listed(P1);
		TestEqual(TEXT("stop = 속도 0"), O->GetNumberField(TEXT("speedMps")), 0.0, 1e-6);
		TestEqual(TEXT("stop = stopped"), O->GetStringField(TEXT("state")), FString(TEXT("stopped")));

		Rpc(TEXT("agent.setPose"), Items(Item(P1, Base + FVector2D(5, 1), TEXT(",\"speedMps\":1.3"))), E);
		TSharedPtr<FJsonObject> Info;
		bool bBad = false;
		const TArray<FVector2D> Path = { Base + FVector2D(5, 1.5) };
		TestTrue(TEXT("표시 속도 뒤 이동"), M->StartMove(P1, &Path, nullptr, 1.0, true, 20.0, Info, Err, bBad));
		for (int32 i = 0; i < 20; ++i) { M->Advance(0.1f); }
		O = Listed(P1);
		TestEqual(TEXT("도착 = arrived"), O->GetStringField(TEXT("state")), FString(TEXT("arrived")));
		TestEqual(TEXT("도착 = 속도 0"), O->GetNumberField(TEXT("speedMps")), 0.0, 1e-6);
	}
	// 없는 id: 그 항목만 건너뛴다. 전부 없으면 not_found.
	{
		FRpcError E;
		const FVector2D Spot = Base + FVector2D(1, 1);
		const TSharedPtr<FJsonObject> R = Rpc(TEXT("agent.setPose"), Items(Item(P1, Spot), Item(TEXT("no-such"), Base)), E);
		TestFalse(TEXT("없는 id 섞여도 성공"), E.HasError());
		if (R.IsValid())
		{
			TestEqual(TEXT("applied 1"), static_cast<int32>(R->GetNumberField(TEXT("applied"))), 1);
			const TArray<TSharedPtr<FJsonValue>> Nf = R->GetArrayField(TEXT("notFound"));
			TestTrue(TEXT("notFound 에 그 id"), Nf.Num() == 1 && Nf[0]->AsString() == TEXT("no-such"));
		}
		TestTrue(TEXT("있는 항목은 옮겨졌다"), PaPos(Listed(P1)).Equals(Spot, 1e-3));

		FRpcError E2;
		Rpc(TEXT("agent.setPose"), Items(Item(TEXT("no-such"), Base)), E2);
		TestTrue(TEXT("전부 없음 = not_found"), E2.HasError() && E2.Kind == ERpcErrorKind::NotFound);
	}
	// 호출 전체를 거부하는 경우 — 앞 항목도 옮겨지면 안 된다.
	{
		const FVector2D Before = PaPos(Listed(P1));
		const FString Good = Item(P1, Base + FVector2D(50, 50));
		const struct { const TCHAR* Name; FString Bad; } Cases[] = {
			{ TEXT("탑승자"), Item(Rider, Base) },
			{ TEXT("이동수단 activity"), Item(Bike, Base, TEXT(",\"activity\":\"walking\"")) },
			{ TEXT("모르는 activity"), Item(P2, Base, TEXT(",\"activity\":\"riding\"")) },
			{ TEXT("음수 속도"), Item(P2, Base, TEXT(",\"speedMps\":-1")) },
			{ TEXT("모르는 키"), Item(P2, Base, TEXT(",\"yaw\":1")) },
			{ TEXT("yawDeg 없음"), FString::Printf(TEXT("{\"id\":\"%s\",\"pos\":{\"x\":0,\"y\":0}}"), *P2) },
			{ TEXT("pos 없음"), FString::Printf(TEXT("{\"id\":\"%s\",\"yawDeg\":0}"), *P2) },
			{ TEXT("id 없음"), TEXT("{\"pos\":{\"x\":0,\"y\":0},\"yawDeg\":0}") },
		};
		for (const auto& C : Cases)
		{
			FRpcError E;
			Rpc(TEXT("agent.setPose"), Items(Good, C.Bad), E);
			TestTrue(FString::Printf(TEXT("%s = bad_params"), C.Name), E.HasError() && E.Kind == ERpcErrorKind::BadParams);
			TestTrue(FString::Printf(TEXT("%s: 아무것도 안 옮김"), C.Name), PaPos(Listed(P1)).Equals(Before, 1e-3));
		}
		FRpcError E;
		Rpc(TEXT("agent.setPose"), TEXT("{\"items\":[]}"), E);
		TestTrue(TEXT("빈 items = bad_params"), E.HasError() && E.Kind == ERpcErrorKind::BadParams);
	}
	// activity 가 속도를 이긴다. 같은 id 는 뒤가 이긴다.
	{
		FRpcError E;
		Rpc(TEXT("agent.setPose"), Items(Item(P1, Base, TEXT(",\"speedMps\":1.3,\"activity\":\"standing\""))), E);
		TSharedPtr<FJsonObject> O = Listed(P1);
		TestEqual(TEXT("standing + 속도"), O->GetStringField(TEXT("activityType")), FString(TEXT("standing")));
		TestEqual(TEXT("표시 속도는 그대로"), O->GetStringField(TEXT("motionType")), FString(TEXT("moving")));

		Rpc(TEXT("agent.setPose"), Items(Item(P1, Base, TEXT(",\"activity\":\"walking\""))), E);
		O = Listed(P1);
		TestEqual(TEXT("walking + 속도 0"), O->GetStringField(TEXT("activityType")), FString(TEXT("walking")));
		TestEqual(TEXT("walking + 속도 0 = stationary"), O->GetStringField(TEXT("motionType")), FString(TEXT("stationary")));

		const FVector2D Last = Base + FVector2D(2, 2);
		Rpc(TEXT("agent.setPose"), Items(Item(P1, Base + FVector2D(9, 9)), Item(P1, Last)), E);
		TestFalse(TEXT("같은 id 두 번"), E.HasError());
		TestTrue(TEXT("뒤 항목"), PaPos(Listed(P1)).Equals(Last, 1e-3));
	}
	// 스냅샷: 그대로 돌아오고, paused 면 선다.
	{
		FRpcError E;
		const FVector2D Spot = Base + FVector2D(3, 3);
		Rpc(TEXT("agent.setPose"), Items(Item(P1, Spot, TEXT(",\"speedMps\":1.3"))), E);
		const FParkAgentSnapshot Snap = M->Capture();
		M->Delete({}, true, NotFound);
		TestEqual(TEXT("지웠다"), M->Num(), 0);
		M->Restore(Snap, false);
		for (int32 i = 0; i < 5; ++i) { M->Advance(0.1f); }
		TSharedPtr<FJsonObject> O = Listed(P1);
		if (TestTrue(TEXT("복원"), O.IsValid()))
		{
			TestTrue(TEXT("복원 자리"), PaPos(O).Equals(Spot, 1e-3));
			TestEqual(TEXT("복원 속도"), O->GetNumberField(TEXT("speedMps")), 1.3, 1e-6);
		}
		M->Restore(Snap, true);
		O = Listed(P1);
		if (TestTrue(TEXT("paused 복원"), O.IsValid()))
		{
			TestTrue(TEXT("paused 자리"), PaPos(O).Equals(Spot, 1e-3));
			TestEqual(TEXT("paused 속도 0"), O->GetNumberField(TEXT("speedMps")), 0.0, 1e-6);
			TestEqual(TEXT("paused stopped"), O->GetStringField(TEXT("state")), FString(TEXT("stopped")));
		}
	}
	// 탑승 중인 탈것을 옮기면 탄 사람이 함께 간다.
	{
		const auto CheckRider = [&](const TCHAR* What, const FVector2D& Spot, double Speed, const TCHAR* State)
		{
			const TSharedPtr<FJsonObject> V = Listed(Moto), O = Listed(Rider);
			if (!TestTrue(FString::Printf(TEXT("%s: 목록"), What), V.IsValid() && O.IsValid())) { return; }
			TestTrue(FString::Printf(TEXT("%s: 탈것 자리"), What), PaPos(V).Equals(Spot, 1e-3));
			TestTrue(FString::Printf(TEXT("%s: 탄 사람 자리"), What), PaPos(O).Equals(Spot, 1e-3));
			TestEqual(FString::Printf(TEXT("%s: 탄 사람 높이"), What), O->GetObjectField(TEXT("pos"))->GetNumberField(TEXT("z")), V->GetObjectField(TEXT("pos"))->GetNumberField(TEXT("z")), 1e-6);
			TestEqual(FString::Printf(TEXT("%s: 탄 사람 방향"), What), O->GetNumberField(TEXT("directionDeg")), V->GetNumberField(TEXT("directionDeg")), 1e-6);
			TestEqual(FString::Printf(TEXT("%s: 탈것 속도"), What), V->GetNumberField(TEXT("speedMps")), Speed, 1e-6);
			TestEqual(FString::Printf(TEXT("%s: 탄 사람 속도"), What), O->GetNumberField(TEXT("speedMps")), Speed, 1e-6);
			TestEqual(FString::Printf(TEXT("%s: 탄 사람 상태"), What), O->GetStringField(TEXT("state")), FString(State));
			TestEqual(FString::Printf(TEXT("%s: riding"), What), O->GetStringField(TEXT("activityType")), FString(TEXT("riding")));
			TestEqual(FString::Printf(TEXT("%s: ridingWith"), What), O->GetStringField(TEXT("ridingWith")), Moto);
		};

		// 호출 응답이 끝난 순간 목록이 맞고, 탄 사람 액터도 좌석에 붙어 따라갔다.
		const FVector2D Spot = Base + FVector2D(20, 20);
		FRpcError E;
		const TSharedPtr<FJsonObject> R = Rpc(TEXT("agent.setPose"), Items(Item(Moto, Spot, TEXT(",\"speedMps\":4"))), E);
		TestFalse(TEXT("탑승 탈것 setPose"), E.HasError());
		if (R.IsValid()) { TestEqual(TEXT("applied 는 탈것 1"), static_cast<int32>(R->GetNumberField(TEXT("applied"))), 1); }
		CheckRider(TEXT("옮긴 직후"), Spot, 4.0, TEXT("moving"));
		int32 Attached = 0;
		for (TActorIterator<AParkAgentActor> It(World); It; ++It)
		{
			if (!It->GetAttachParentActor()) { continue; }
			++Attached;
			TestTrue(TEXT("탈것 액터 자리"), FVector2D(It->GetAttachParentActor()->GetActorLocation()).Equals(Spot * 100.0, 0.1));
			TestTrue(TEXT("탄 사람 액터가 탈것 위"), FVector2D::Distance(FVector2D(It->GetActorLocation()), Spot * 100.0) < 100.0);
		}
		TestEqual(TEXT("붙어 있는 탄 사람 1"), Attached, 1);

		for (int32 i = 0; i < 5; ++i) { M->Advance(0.1f); }
		CheckRider(TEXT("틱 뒤"), Spot, 4.0, TEXT("moving"));

		// 탄 사람 id 로 멈추면 탈것 표시 속도가 끝난다.
		TestEqual(TEXT("탄 사람 id 로 정지"), M->Stop({ Rider }, false, NotFound), 1);
		CheckRider(TEXT("정지"), Spot, 0.0, TEXT("stopped"));

		// 달리던 이동이 끝나고, 다음 이동은 새 자리에서 함께 출발한다.
		TSharedPtr<FJsonObject> Info;
		bool bBad = false;
		const TArray<FVector2D> Path = { Spot + FVector2D(10, 0) };
		TestTrue(TEXT("이동 시작"), M->StartMove(Moto, &Path, nullptr, 3.0, true, 20.0, Info, Err, bBad));
		M->Advance(0.5f);
		const FVector2D Spot2 = Spot + FVector2D(-5, 5);
		Rpc(TEXT("agent.setPose"), Items(Item(Moto, Spot2)), E);
		TestFalse(TEXT("이동 중 탑승 탈것 setPose"), E.HasError());
		for (int32 i = 0; i < 5; ++i) { M->Advance(0.1f); }
		CheckRider(TEXT("이동 중 옮김"), Spot2, 0.0, TEXT("stopped"));
		TestEqual(TEXT("탄 사람 endReason setPose"), Listed(Rider)->GetStringField(TEXT("endReason")), FString(TEXT("setPose")));
		const TArray<FVector2D> Path2 = { Spot2 + FVector2D(10, 0) };
		TestTrue(TEXT("다시 이동"), M->StartMove(Moto, &Path2, nullptr, 3.0, true, 20.0, Info, Err, bBad));
		for (int32 i = 0; i < 10; ++i) { M->Advance(0.1f); }
		TestTrue(TEXT("탄 사람도 새 자리에서 출발"), PaPos(Listed(Rider)).Equals(PaPos(Listed(Moto)), 1e-3) && FVector2D::Distance(PaPos(Listed(Rider)), Spot2) > 0.5);
		M->Stop({ Moto }, false, NotFound);

		// 스냅샷: 관계와 자리·표시 속도가 돌아온다.
		Rpc(TEXT("agent.setPose"), Items(Item(Moto, Spot, TEXT(",\"speedMps\":4"))), E);
		const FParkAgentSnapshot Snap = M->Capture();
		Rpc(TEXT("agent.setPose"), Items(Item(Moto, Spot2)), E);
		M->Restore(Snap, false);
		CheckRider(TEXT("복원"), Spot, 4.0, TEXT("moving"));
		M->Restore(Snap, true);
		CheckRider(TEXT("paused 복원"), Spot, 0.0, TEXT("stopped"));

		// 탈것을 지우면 옮긴 자리 옆에 내려 선다.
		Rpc(TEXT("agent.setPose"), Items(Item(Moto, Spot2)), E);
		M->Delete({ Moto }, false, NotFound);
		const TSharedPtr<FJsonObject> Off = Listed(Rider);
		if (TestTrue(TEXT("내린 사람 남음"), Off.IsValid()))
		{
			TestEqual(TEXT("옮긴 자리에서 0.8 m"), FVector2D::Distance(PaPos(Off), Spot2), 0.8, 1e-3);
			TestEqual(TEXT("내린 사람 standing"), Off->GetStringField(TEXT("activityType")), FString(TEXT("standing")));
		}
		// 이제 사람 id 로 옮길 수 있다.
		FRpcError E2;
		Rpc(TEXT("agent.setPose"), Items(Item(Rider, Base + FVector2D(4, 4))), E2);
		TestFalse(TEXT("내린 사람"), E2.HasError());
	}
	// allowOverlap: 시험 월드엔 격자가 없어 "기본값은 겹치면 거부" 는 13530 실기로 본다.
	{
		FRpcError E;
		const TSharedPtr<FJsonObject> R = Rpc(TEXT("agent.create"), TEXT("{\"kind\":\"pedestrian\",\"pos\":{\"x\":3000,\"y\":3030},\"allowOverlap\":true}"), E);
		TestFalse(TEXT("allowOverlap 생성"), E.HasError());
		TestEqual(TEXT("allowOverlap 은 읽힌다"), Unread.Num(), 0);
		TestTrue(TEXT("응답"), R.IsValid());
	}
	M->Delete({}, true, NotFound);
	M->Destroy();
	return true;
}

namespace
{
	double PaAngleDeg(const FVector2D& A, const FVector2D& B)
	{
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector2D::DotProduct(A.GetSafeNormal(), B.GetSafeNormal()), -1.0, 1.0)));
	}

	/** 점마다 꺾이는 각의 최댓값. */
	double PaMaxVertexTurn(const TArray<FVector2D>& Pts)
	{
		double Max = 0.0;
		for (int32 i = 1; i + 1 < Pts.Num(); ++i) { Max = FMath::Max(Max, PaAngleDeg(Pts[i] - Pts[i - 1], Pts[i + 1] - Pts[i])); }
		return Max;
	}

	double PaDistToLine(const TArray<FVector2D>& Pts, const FVector2D& P)
	{
		double Min = DBL_MAX;
		for (int32 i = 0; i + 1 < Pts.Num(); ++i)
		{
			const FVector2D Near = FMath::ClosestPointOnSegment2D(P, Pts[i], Pts[i + 1]);
			Min = FMath::Min(Min, FVector2D::Distance(Near, P));
		}
		return Min;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParkAgentTurnPathTest, "Park3D.Agent.TurnPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FParkAgentTurnPathTest::RunTest(const FString& Parameters)
{
	const auto AcceptAll = [](const TArray<FVector2D>&, const TArray<FVector2D>&) { return true; };

	// 1) 막힘 없는 곳: 종류별 반경대로 둥글고, 끝 점은 그대로이고, 호 속도는 가로 가속으로 정해진다.
	for (const EKind K : { EKind::Pedestrian, EKind::Bicycle, EKind::Motorcycle })
	{
		const FTurnParams Tp = TurnParamsOf(K);
		const FString What = ObjectType(K);
		TArray<FVector2D> Pts = { FVector2D(0, 0), FVector2D(10, 0), FVector2D(10, 10) };
		TArray<FVector2D> Sharp;
		RoundPath(Pts, Tp.RadiusM, AcceptAll, Sharp);
		TestEqual(What + TEXT(": 꺾인 점 없음"), Sharp.Num(), 0);
		TestTrue(What + TEXT(": 첫 점 그대로"), Pts[0].Equals(FVector2D(0, 0), 1e-9));
		TestTrue(What + TEXT(": 끝 점 그대로"), Pts.Last().Equals(FVector2D(10, 10), 1e-9));
		const double Turn = PaMaxVertexTurn(Pts);
		TestTrue(FString::Printf(TEXT("%s: 점마다 10° 이하(%.1f)"), *What, Turn), Turn <= 10.0);
		// 90° 호가 꼭짓점에서 떨어지는 거리 = R(√2 − 1).
		const double Inset = PaDistToLine(Pts, FVector2D(10, 0));
		TestEqual(What + TEXT(": 꼭짓점 안쪽 거리"), Inset, Tp.RadiusM * (FMath::Sqrt(2.0) - 1.0), 0.03);
		TArray<double> Limits;
		CornerSpeedLimits(Pts, Tp, Limits);
		TestEqual(TEXT("제한 개수 = 점 개수"), Limits.Num(), Pts.Num());
		double Low = DBL_MAX;
		for (const double L : Limits) { Low = FMath::Min(Low, L); }
		TestEqual(What + TEXT(": 호 안 속도"), Low, FMath::Sqrt(Tp.LatAccel * Tp.RadiusM), 0.1);
	}

	// 2) 호를 넣으면 벽에 닿는 모서리: 반경을 줄여 벽을 피한다. 원래 경로 = 통과, 그대로 둥글리면 = 벽.
	{
		const FAgentGrid G = PaWallGrid();
		TArray<float> Clear;
		ClearanceField(G, EKind::Pedestrian, nullptr, Clear);
		const FFootprint Fp = FootprintOf(EKind::Pedestrian);   // 틈(폭 2 m)을 지나는 몸. 반경 2 m 호의 꼭대기는 틈 위 벽 속이다
		const TArray<FVector2D> Raw = { FVector2D(8, 8), FVector2D(10, 5), FVector2D(12, 8) };
		FVector2D At; FString Who;
		TestEqual(TEXT("원래 경로 통과"), CheckPath(G, Clear, {}, Fp, Raw, 0.2, At, Who), 0);
		TArray<FVector2D> Free = Raw;
		TArray<FVector2D> Sharp;
		RoundPath(Free, 2.0, AcceptAll, Sharp);
		TestEqual(TEXT("검사 없이 둥글리면 벽"), CheckPath(G, Clear, {}, Fp, Free, 0.2, At, Who), 1);
		TArray<FVector2D> Safe = Raw;
		const auto Check = [&](const TArray<FVector2D>& Piece, const TArray<FVector2D>&)
		{
			FVector2D A2; FString W2;
			return CheckPath(G, Clear, {}, Fp, Piece, 0.2, A2, W2) == 0;
		};
		RoundPath(Safe, 2.0, Check, Sharp);
		TestEqual(TEXT("검사하면 벽에 안 닿는다"), CheckPath(G, Clear, {}, Fp, Safe, 0.2, At, Who), 0);
		TestTrue(TEXT("그래도 호가 들어갔다"), Safe.Num() > Raw.Num());
	}

	// 3) 서 있는 차 사이: 안쪽 모서리에 차가 있으면 반경을 줄이고 차와 몸 반경 + 0.2 m 를 남긴다.
	{
		FAgentGrid G;
		G.Cell = 0.2;
		G.W = 151; G.H = 151;
		G.Cls.Init(CellGround, G.W * G.H);
		G.HeightM.Init(0.f, G.W * G.H);
		TArray<float> Clear;
		ClearanceField(G, EKind::Pedestrian, nullptr, Clear);
		const FFootprint Fp = FootprintOf(EKind::Pedestrian);
		FCarBox Car;
		Car.Center = FVector2D(13.3, 7.0);   // 꼭짓점 (15, 10) 안쪽, 차와 꼭짓점 사이 약 1 m
		Car.Half = FVector2D(1.0, 2.0);
		Car.Id = TEXT("car-in");
		TArray<FVector2D> Pts = { FVector2D(5, 10), FVector2D(15, 10), FVector2D(15, 20) };
		TArray<FVector2D> Sharp;
		const auto Check = [&](const TArray<FVector2D>& Piece, const TArray<FVector2D>&)
		{
			FVector2D A2; FString W2;
			return CheckPath(G, Clear, { Car }, Fp, Piece, 0.2, A2, W2) == 0;
		};
		RoundPath(Pts, 0.6, Check, Sharp);
		double MinGap = DBL_MAX;
		TArray<double> Cum;
		CumLengths(Pts, Cum);
		for (double S = 0.0; S <= Cum.Last(); S += 0.02)
		{
			FVector2D P, D;
			PoseAt(Pts, Cum, S, P, D);
			MinGap = FMath::Min(MinGap, Car.DistanceTo(P));
		}
		TestTrue(FString::Printf(TEXT("차까지 ≥ 몸 반경 + 0.2(%.3f)"), MinGap), MinGap >= Fp.Radius + 0.2 - 1e-3);
	}

	// 4) 자리가 전혀 없으면 꺾인 점을 그대로 두고 알린다.
	{
		TArray<FVector2D> Pts = { FVector2D(0, 0), FVector2D(10, 0), FVector2D(10, 10) };
		TArray<FVector2D> Sharp;
		RoundPath(Pts, 2.0, [](const TArray<FVector2D>&, const TArray<FVector2D>&) { return false; }, Sharp);
		TestEqual(TEXT("꺾인 점 1개"), Sharp.Num(), 1);
		TestEqual(TEXT("점 3개 그대로"), Pts.Num(), 3);
		TArray<double> Limits;
		CornerSpeedLimits(Pts, TurnParamsOf(EKind::Bicycle), Limits);
		TestEqual(TEXT("꺾인 점 = 최저 속도"), Limits[1], TurnParamsOf(EKind::Bicycle).MinCornerMps, 1e-9);
	}

	// 5) 출발 호: 지금 방향으로 나가 목표를 바라보며 끝난다. 자리가 없으면 false.
	{
		TArray<FVector2D> Arc;
		TestTrue(TEXT("호 찾음"), LeadInArc(FVector2D(0, 0), 0.0, FVector2D(0, 10), 2.0, AcceptAll, Arc));
		if (TestTrue(TEXT("호 점 여러 개"), Arc.Num() > 3))
		{
			TestTrue(TEXT("지금 방향으로 나간다"), PaAngleDeg(Arc[1] - Arc[0], FVector2D(1, 0)) < 6.0);
			const FVector2D Last = Arc.Last() - Arc[Arc.Num() - 2];
			TestTrue(TEXT("끝나면 목표를 본다"), PaAngleDeg(Last, FVector2D(0, 10) - Arc.Last()) < 12.0);
		}
		TestTrue(TEXT("방향이 맞으면 호 없음"), LeadInArc(FVector2D(0, 0), 0.0, FVector2D(10, 0), 2.0, AcceptAll, Arc) && Arc.Num() == 1);
		TestFalse(TEXT("자리가 없으면 false"), LeadInArc(FVector2D(0, 0), 0.0, FVector2D(0, 10), 2.0, [](const TArray<FVector2D>&, const TArray<FVector2D>&) { return false; }, Arc));
	}

	// 6) 몸 회전: 이동수단은 서 있으면 안 돈다. 사람은 돈다.
	TestEqual(TEXT("자전거 정지 회전 0"), MaxYawStepDeg(EKind::Bicycle, 0.0, 0.1), 0.0);
	TestEqual(TEXT("오토바이 정지 회전 0"), MaxYawStepDeg(EKind::Motorcycle, 0.0, 0.1), 0.0);
	TestTrue(TEXT("사람 정지 회전 > 0"), MaxYawStepDeg(EKind::Pedestrian, 0.0, 0.1) > 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParkAgentTurnMoveTest, "Park3D.Agent.TurnMove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParkAgentTurnMoveTest::RunTest(const FString& Parameters)
{
	UWorld* World = (GEngine && GEngine->GetWorldContexts().Num() > 0) ? GWorld : nullptr;
	if (!TestNotNull(TEXT("에디터 월드"), World)) { return false; }
	AParkAgentManager* M = AParkAgentManager::GetOrSpawn(World);
	if (!TestNotNull(TEXT("매니저"), M)) { return false; }
	TArray<FString> NotFound;
	M->Delete({}, true, NotFound);

	const FVector2D Base(3000.0, 3000.0);
	constexpr float Dt = 1.f / 30.f;
	FString Err;
	bool bBad = false;
	TSharedPtr<FJsonObject> Info;

	struct FTrack { double MaxMotionStep = 0.0; double MaxYawStep = 0.0; double MaxSlide = 0.0; double MinSpeed = DBL_MAX; double FirstHeadingOff = -1.0; };
	// 한 프레임마다 방향 변화·미끄러짐을 잰다. 위치는 mm 로 반올림되므로 2 cm 넘게 움직인 프레임만 방향을 잰다.
	const auto Drive = [&](const FString& Id, FTrack& T, double StartYaw) -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> O = PaFind(M->ListJson(nullptr, {}), Id);
		FVector2D Prev = PaPos(O);
		FVector2D PrevDir = FVector2D::ZeroVector;
		double PrevYaw = O->GetNumberField(TEXT("directionDeg"));
		for (int32 i = 0; i < 3000; ++i)
		{
			M->Advance(Dt);
			O = PaFind(M->ListJson(nullptr, {}), Id);
			const FVector2D Now = PaPos(O);
			const double Yaw = O->GetNumberField(TEXT("directionDeg"));
			T.MaxYawStep = FMath::Max(T.MaxYawStep, FMath::Abs(FRotator::NormalizeAxis(Yaw - PrevYaw)));
			PrevYaw = Yaw;
			const FVector2D Delta = Now - Prev;
			if (Delta.Size() > 0.02)
			{
				if (T.FirstHeadingOff < 0.0)
				{
					const double R = FMath::DegreesToRadians(StartYaw);
					T.FirstHeadingOff = PaAngleDeg(Delta, FVector2D(FMath::Cos(R), FMath::Sin(R)));
				}
				if (!PrevDir.IsZero()) { T.MaxMotionStep = FMath::Max(T.MaxMotionStep, PaAngleDeg(PrevDir, Delta)); }
				const double R = FMath::DegreesToRadians(Yaw);
				T.MaxSlide = FMath::Max(T.MaxSlide, PaAngleDeg(Delta, FVector2D(FMath::Cos(R), FMath::Sin(R))));
				PrevDir = Delta;
				Prev = Now;
			}
			if (O->GetStringField(TEXT("state")) != TEXT("moving")) { break; }
			T.MinSpeed = FMath::Min(T.MinSpeed, O->GetNumberField(TEXT("speedMps")));
		}
		return O;
	};

	const struct { EKind Kind; double SlideMax; } Cases[] = { { EKind::Pedestrian, 25.0 }, { EKind::Bicycle, 15.0 }, { EKind::Motorcycle, 15.0 } };
	int32 Row = 0;
	for (const auto& C : Cases)
	{
		const FTurnParams Tp = TurnParamsOf(C.Kind);
		const FString What = ObjectType(C.Kind);
		const FVector2D Start = Base + FVector2D(0.0, 100.0 * Row++);

		// 1) 90° 꺾기: 방향이 프레임마다 조금씩 바뀌고, 호에서 줄이고, 목적지에 정확히 선다.
		TArray<FString> Ids;
		TestTrue(What + TEXT(" 생성"), M->Create(C.Kind, Start, 0.0, false, FString(), FString(), Ids, Err));
		if (Ids.Num() == 0) { continue; }
		const FVector2D Goal = Start + FVector2D(12.0, 12.0);
		const TArray<FVector2D> Path = { Start + FVector2D(12.0, 0.0), Goal };
		TestTrue(What + TEXT(" 이동"), M->StartMove(Ids[0], &Path, nullptr, 0.0, true, 20.0, Info, Err, bBad));
		TestTrue(What + TEXT(": 응답 pts 는 호를 포함"), Info.IsValid() && Info->GetArrayField(TEXT("pts")).Num() > 3);
		TestTrue(What + TEXT(": lengthM 은 직각보다 짧다"), Info.IsValid() && Info->GetNumberField(TEXT("lengthM")) < 24.0);
		FTrack T;
		const TSharedPtr<FJsonObject> End = Drive(Ids[0], T, 0.0);
		TestEqual(What + TEXT(": 도착"), End->GetStringField(TEXT("state")), FString(TEXT("arrived")));
		TestTrue(What + TEXT(": 목적지"), PaPos(End).Equals(Goal, 1e-3));
		TestEqual(What + TEXT(": 끝 방향"), End->GetNumberField(TEXT("directionDeg")), 90.0, 8.0);
		TestTrue(FString::Printf(TEXT("%s: 이동 방향 프레임당 ≤ 10° (%.1f)"), *What, T.MaxMotionStep), T.MaxMotionStep <= 10.0);
		TestTrue(FString::Printf(TEXT("%s: 몸 회전 ≤ 상한 %.0f°/s (%.0f)"), *What, Tp.YawRateDeg, T.MaxYawStep / Dt), T.MaxYawStep / Dt <= Tp.YawRateDeg * 1.02);
		TestTrue(FString::Printf(TEXT("%s: 미끄러짐 ≤ %.0f° (%.1f)"), *What, C.SlideMax, T.MaxSlide), T.MaxSlide <= C.SlideMax);

		// 2) 출발 방향이 어긋나도 이동수단은 지금 방향으로 나가 호를 돈다.
		if (C.Kind != EKind::Pedestrian)
		{
			TArray<FString> Ids2;
			const FVector2D S2 = Start + FVector2D(50.0, 0.0);
			TestTrue(What + TEXT(" 2 생성"), M->Create(C.Kind, S2, 0.0, false, FString(), FString(), Ids2, Err));
			const TArray<FVector2D> Side = { S2 + FVector2D(0.0, 12.0) };
			TestTrue(What + TEXT(" 2 이동"), M->StartMove(Ids2[0], &Side, nullptr, 0.0, true, 20.0, Info, Err, bBad));
			FTrack T2;
			const TSharedPtr<FJsonObject> End2 = Drive(Ids2[0], T2, 0.0);
			TestEqual(What + TEXT(": 옆 목적지 도착"), End2->GetStringField(TEXT("state")), FString(TEXT("arrived")));
			TestTrue(What + TEXT(": 옆 목적지"), PaPos(End2).Equals(S2 + FVector2D(0.0, 12.0), 1e-3));
			TestTrue(FString::Printf(TEXT("%s: 첫 움직임 = 지금 방향(%.1f)"), *What, T2.FirstHeadingOff), T2.FirstHeadingOff >= 0.0 && T2.FirstHeadingOff <= 10.0);
			TestTrue(FString::Printf(TEXT("%s: 출발 호 미끄러짐 ≤ %.0f° (%.1f)"), *What, C.SlideMax, T2.MaxSlide), T2.MaxSlide <= C.SlideMax);
		}
	}

	// 3) setPose 는 순간 이동 그대로: 돌던 호를 끊고 그 방향·자리에 바로 선다.
	{
		TArray<FString> Ids;
		const FVector2D S3 = Base + FVector2D(100.0, 0.0);
		TestTrue(TEXT("setPose 자전거"), M->Create(EKind::Bicycle, S3, 0.0, false, FString(), FString(), Ids, Err));
		const TArray<FVector2D> Path = { S3 + FVector2D(12.0, 0.0), S3 + FVector2D(12.0, 12.0) };
		TestTrue(TEXT("setPose 전 이동"), M->StartMove(Ids[0], &Path, nullptr, 0.0, true, 20.0, Info, Err, bBad));
		for (int32 i = 0; i < 30; ++i) { M->Advance(Dt); }
		FParkAgentPose Pose;
		Pose.Id = Ids[0];
		Pose.PosM = S3 + FVector2D(5.0, 5.0);
		Pose.YawDeg = 137.0;
		int32 Applied = 0;
		TArray<FString> Missing;
		TestTrue(TEXT("setPose"), M->SetPoses({ Pose }, Applied, Missing, Err));
		const TSharedPtr<FJsonObject> O = PaFind(M->ListJson(nullptr, {}), Ids[0]);
		TestTrue(TEXT("자리 즉시"), PaPos(O).Equals(Pose.PosM, 1e-3));
		TestEqual(TEXT("방향 즉시"), O->GetNumberField(TEXT("directionDeg")), 137.0, 1e-6);
		TestEqual(TEXT("이동 끝남"), O->GetStringField(TEXT("endReason")), FString(TEXT("setPose")));
	}

	// 4) 호 도중 스냅샷 → 복원: 같은 자리에서 이어 달려 목적지에 선다.
	{
		TArray<FString> Ids;
		const FVector2D S4 = Base + FVector2D(150.0, 0.0);
		TestTrue(TEXT("복원 자전거"), M->Create(EKind::Bicycle, S4, 0.0, false, FString(), FString(), Ids, Err));
		const FVector2D Goal = S4 + FVector2D(8.0, 8.0);
		const TArray<FVector2D> Path = { S4 + FVector2D(8.0, 0.0), Goal };
		TestTrue(TEXT("복원 전 이동"), M->StartMove(Ids[0], &Path, nullptr, 0.0, true, 20.0, Info, Err, bBad));
		for (int32 i = 0; i < 120; ++i) { M->Advance(Dt); }
		const FParkAgentSnapshot Snap = M->Capture();
		M->Restore(Snap, false);
		FTrack T;
		const TSharedPtr<FJsonObject> End = Drive(Ids[0], T, 0.0);
		TestEqual(TEXT("복원 뒤 도착"), End->GetStringField(TEXT("state")), FString(TEXT("arrived")));
		TestTrue(TEXT("복원 뒤 목적지"), PaPos(End).Equals(Goal, 1e-3));
	}

	M->Delete({}, true, NotFound);
	M->Destroy();
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParkAgentPivotTest, "Park3D.Agent.Pivot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParkAgentPivotTest::RunTest(const FString& Parameters)
{
	UWorld* World = (GEngine && GEngine->GetWorldContexts().Num() > 0) ? GWorld : nullptr;
	if (!TestNotNull(TEXT("에디터 월드"), World)) { return false; }
	AParkAgentManager* M = AParkAgentManager::GetOrSpawn(World);
	if (!TestNotNull(TEXT("매니저"), M)) { return false; }
	TArray<FString> NotFound;
	M->Delete({}, true, NotFound);

	// 옆으로 0.5 m 는 어떤 반경의 호로도 바라볼 수 없다 → 출발 호 없음. 단독·탑승·끌기 모두 제자리에서 돈 뒤 간다.
	const struct { const TCHAR* Name; EKind Kind; bool bRider; bool bPusher; } Cases[] = {
		{ TEXT("자전거"), EKind::Bicycle, false, false },
		{ TEXT("오토바이"), EKind::Motorcycle, false, false },
		{ TEXT("탑승 자전거"), EKind::Bicycle, true, false },
		{ TEXT("탑승 오토바이"), EKind::Motorcycle, true, false },
		{ TEXT("끄는 자전거"), EKind::Bicycle, false, true },
	};
	int32 Row = 0;
	for (const auto& C : Cases)
	{
		const FVector2D Start(3000.0, 3400.0 + 100.0 * Row++);
		TArray<FString> Ids;
		FString Err;
		const FString What(C.Name);
		TestTrue(What + TEXT(" 생성"), M->Create(C.Kind, Start, 0.0, C.bRider, FString(TEXT("city")), FString(), Ids, Err, nullptr, true, C.bPusher));
		if (Ids.Num() == 0) { continue; }
		const TArray<FVector2D> Path = { Start + FVector2D(0.0, 0.5) };
		TSharedPtr<FJsonObject> Info;
		bool bBad = false;
		TestTrue(What + TEXT(" 이동"), M->StartMove(Ids[0], &Path, nullptr, 0.0, true, 20.0, Info, Err, bBad));
		bool bNote = false;
		if (Info.IsValid())
		{
			for (const TSharedPtr<FJsonValue>& N : Info->GetArrayField(TEXT("notes"))) { bNote |= N->AsString().Contains(TEXT("제자리에서 돈 뒤")); }
		}
		TestTrue(What + TEXT(": notes"), bNote);

		TSharedPtr<FJsonObject> O = PaFind(M->ListJson(nullptr, {}), Ids[0]);
		FVector2D Prev = PaPos(O);
		double MaxSlide = 0.0, MovedWhileOff = 0.0, MaxYawStep = 0.0, PusherSpeedInTurn = 0.0;
		double PrevYaw = O->GetNumberField(TEXT("directionDeg"));
		for (int32 i = 0; i < 900; ++i)
		{
			M->Advance(1.f / 30.f);
			O = PaFind(M->ListJson(nullptr, {}), Ids[0]);
			const FVector2D Now = PaPos(O);
			const double Yaw = O->GetNumberField(TEXT("directionDeg"));
			MaxYawStep = FMath::Max(MaxYawStep, FMath::Abs(FRotator::NormalizeAxis(Yaw - PrevYaw)));
			if (C.bPusher && O->GetNumberField(TEXT("speedMps")) < 0.01 && O->GetStringField(TEXT("state")) == TEXT("moving") && Ids.Num() > 1)
			{
				const TSharedPtr<FJsonObject> Person = PaFind(M->ListJson(nullptr, {}), Ids[1]);
				if (FMath::Abs(FRotator::NormalizeAxis(Yaw - PrevYaw)) > 0.5)
				{
					PusherSpeedInTurn = FMath::Max(PusherSpeedInTurn, Person->GetNumberField(TEXT("speedMps")));
					TestEqual(What + TEXT(": 도는 동안 사람 motionType"), Person->GetStringField(TEXT("motionType")), FString(TEXT("moving")));
				}
			}
			PrevYaw = Yaw;
			const FVector2D Delta = Now - Prev;
			if (Delta.Size() > 0.02)
			{
				const double R = FMath::DegreesToRadians(Yaw);
				const double Slide = PaAngleDeg(Delta, FVector2D(FMath::Cos(R), FMath::Sin(R)));
				MaxSlide = FMath::Max(MaxSlide, Slide);
				if (Slide > 30.0) { MovedWhileOff += Delta.Size(); }
				Prev = Now;
			}
			if (O->GetStringField(TEXT("state")) != TEXT("moving")) { break; }
		}
		TestEqual(What + TEXT(": 도착"), O->GetStringField(TEXT("state")), FString(TEXT("arrived")));
		TestTrue(What + TEXT(": 자리"), PaPos(O).Equals(Path[0], 1e-3));
		TestTrue(FString::Printf(TEXT("%s: 어긋남 ≤ 25°(%.1f)"), *What, MaxSlide), MaxSlide <= 25.0);
		TestTrue(FString::Printf(TEXT("%s: 30° 넘게 어긋난 채 간 거리 0(%.3f)"), *What, MovedWhileOff), MovedWhileOff <= 1e-6);
		TestTrue(FString::Printf(TEXT("%s: 몸이 한 프레임에 튀지 않는다(%.1f)"), *What, MaxYawStep), MaxYawStep <= 6.0);
		if (C.bPusher) { TestTrue(FString::Printf(TEXT("도는 동안 사람이 걷는다(%.2f m/s)"), PusherSpeedInTurn), PusherSpeedInTurn > 0.2); }
		AddInfo(FString::Printf(TEXT("%s 어긋남 최대 %.1f°"), *What, MaxSlide));
	}
	M->Delete({}, true, NotFound);
	M->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParkAgentPushTest, "Park3D.Agent.Push",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParkAgentPushTest::RunTest(const FString& Parameters)
{
	UWorld* World = (GEngine && GEngine->GetWorldContexts().Num() > 0) ? GWorld : nullptr;
	if (!TestNotNull(TEXT("에디터 월드"), World)) { return false; }
	AParkAgentManager* M = AParkAgentManager::GetOrSpawn(World);
	if (!TestNotNull(TEXT("매니저"), M)) { return false; }
	TArray<FString> NotFound;
	M->Delete({}, true, NotFound);

	URpcDispatcher* D = NewObject<URpcDispatcher>();
	FAgentRpcModule Module([World]() -> UWorld* { return World; });
	Module.Register(*D);

	TArray<FString> Unread;
	const auto Rpc = [&](const TCHAR* Method, const FString& Json, FRpcError& E) -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> Params;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		TestTrue(FString::Printf(TEXT("시험 JSON: %s"), *Json), FJsonSerializer::Deserialize(Reader, Params));
		TSharedPtr<FJsonValue> R;
		D->Dispatch(Method, Params, R, E, &Unread);
		return R.IsValid() ? R->AsObject() : nullptr;
	};
	const auto Ok = [&](const TCHAR* Method, const FString& Json) -> TSharedPtr<FJsonObject>
	{
		FRpcError E;
		TSharedPtr<FJsonObject> O = Rpc(Method, Json, E);
		TestFalse(FString::Printf(TEXT("%s 성공: %s"), Method, *Json), E.HasError());
		return O;
	};
	const auto Refused = [&](const TCHAR* Method, const FString& Json)
	{
		FRpcError E;
		Rpc(Method, Json, E);
		TestTrue(FString::Printf(TEXT("%s 거절: %s"), Method, *Json), E.HasError());
	};
	const auto Listed = [&](const FString& Id) { return PaFind(M->ListJson(nullptr, {}), Id); };
	/** 사람 = 자전거 + along × 앞 + side × 오른쪽. */
	const auto Expected = [&](const FString& Bike, double Side, double Along) -> FVector2D
	{
		const TSharedPtr<FJsonObject> B = Listed(Bike);
		const double R = FMath::DegreesToRadians(B->GetNumberField(TEXT("directionDeg")));
		return PaPos(B) + FVector2D(FMath::Cos(R), FMath::Sin(R)) * Along + FVector2D(-FMath::Sin(R), FMath::Cos(R)) * Side;
	};
	const auto CreatePair = [&](const FVector2D& At, double Yaw, const TCHAR* Model, const TCHAR* Side, const TCHAR* Person, FString& OutBike, FString& OutPerson) -> bool
	{
		const FString Json = FString::Printf(TEXT("{\"kind\":\"bicycle\",\"pusher\":true,\"pushSide\":\"%s\",\"model\":\"%s\",\"riderModel\":\"%s\",\"yaw\":%.1f,\"pos\":{\"x\":%.3f,\"y\":%.3f}}"), Side, Model, Person, Yaw, At.X, At.Y);
		const TSharedPtr<FJsonObject> O = Ok(TEXT("agent.create"), Json);
		if (!O.IsValid()) { return false; }
		const TArray<TSharedPtr<FJsonValue>> Ids = O->GetArrayField(TEXT("ids"));
		if (!TestEqual(TEXT("id 2개(자전거, 사람)"), Ids.Num(), 2)) { return false; }
		OutBike = Ids[0]->AsString();
		OutPerson = Ids[1]->AsString();
		return true;
	};

	const FVector2D Base(3000.0, 3000.0);
	const struct { const TCHAR* Model; double Along; } Models[] = { { TEXT("city"), 0.15 }, { TEXT("sport"), 0.25 }, { TEXT("simple"), 0.23 } };

	// 1) 모델·체형·좌우마다 간격(부호 있는 side)·필드·손 간격.
	{
		int32 Row = 0;
		for (const auto& Md : Models)
		{
			for (const TCHAR* Who : { TEXT("manny"), TEXT("quinn") })
			{
				for (const TCHAR* SideName : { TEXT("left"), TEXT("right") })
				{
					const bool bRight = FCString::Strcmp(SideName, TEXT("right")) == 0;
					const double Side = bRight ? 0.40 : -0.40;
					const FString What = FString::Printf(TEXT("%s·%s·%s"), Md.Model, Who, SideName);
					FString Bike, Person;
					if (!CreatePair(Base + FVector2D(10.0 * Row++, 0.0), 30.0, Md.Model, SideName, Who, Bike, Person)) { continue; }
					const TSharedPtr<FJsonObject> P = Listed(Person), B = Listed(Bike);
					TestEqual(What + TEXT(": pushingWith"), P->GetStringField(TEXT("pushingWith")), Bike);
					TestEqual(What + TEXT(": 자전거 pushingWith"), B->GetStringField(TEXT("pushingWith")), Person);
					TestEqual(What + TEXT(": activityType"), P->GetStringField(TEXT("activityType")), FString(TEXT("pushing")));
					TestEqual(What + TEXT(": pushSide"), P->GetStringField(TEXT("pushSide")), FString(SideName));
					const TSharedPtr<FJsonObject> Off = P->GetObjectField(TEXT("pushOffsetM"));
					TestEqual(What + TEXT(": side 부호"), Off->GetNumberField(TEXT("side")), Side, 1e-6);
					TestEqual(What + TEXT(": along"), Off->GetNumberField(TEXT("along")), Md.Along, 1e-6);
					TestTrue(What + TEXT(": 사람 자리 = 공식"), PaPos(P).Equals(Expected(Bike, Side, Md.Along), 2e-3));
					TestEqual(What + TEXT(": 같은 방향"), P->GetNumberField(TEXT("directionDeg")), B->GetNumberField(TEXT("directionDeg")), 1e-6);
					const TSharedPtr<FJsonObject>* Gap = nullptr;
					if (TestTrue(What + TEXT(": gripGapCm 있음"), P->TryGetObjectField(TEXT("gripGapCm"), Gap)))
					{
						const double L = (*Gap)->GetNumberField(TEXT("left")), R = (*Gap)->GetNumberField(TEXT("right"));
						AddInfo(FString::Printf(TEXT("%s 손 간격 좌 %.1f 우 %.1f cm"), *What, L, R));
						TestTrue(FString::Printf(TEXT("%s: 왼손 ≤ 8 cm(%.1f)"), *What, L), L <= 8.0);
						TestTrue(FString::Printf(TEXT("%s: 오른손 ≤ 8 cm(%.1f)"), *What, R), R <= 8.0);
					}
				}
			}
		}
		M->Delete({}, true, NotFound);
	}

	// 2) 잘못된 값.
	Refused(TEXT("agent.create"), TEXT("{\"kind\":\"bicycle\",\"pusher\":true,\"rider\":true,\"pos\":{\"x\":3000,\"y\":3000}}"));
	Refused(TEXT("agent.create"), TEXT("{\"kind\":\"motorcycle\",\"pusher\":true,\"pos\":{\"x\":3000,\"y\":3000}}"));
	Refused(TEXT("agent.create"), TEXT("{\"kind\":\"pedestrian\",\"pusher\":true,\"pos\":{\"x\":3000,\"y\":3000}}"));
	Refused(TEXT("agent.create"), TEXT("{\"kind\":\"bicycle\",\"pusher\":true,\"pushSide\":\"middle\",\"pos\":{\"x\":3000,\"y\":3000}}"));
	Refused(TEXT("agent.create"), TEXT("{\"kind\":\"bicycle\",\"pushSide\":\"right\",\"pos\":{\"x\":3000,\"y\":3000}}"));
	TestEqual(TEXT("거절하면 아무것도 안 생긴다"), M->Num(), 0);

	// 3) 왼쪽·오른쪽 모두: setPose·이동·호·정지.
	int32 Row3 = 0;
	for (const TCHAR* SideName : { TEXT("left"), TEXT("right") })
	{
		const bool bRight = FCString::Strcmp(SideName, TEXT("right")) == 0;
		const double Side = bRight ? 0.40 : -0.40;
		const FString What(SideName);
		const FVector2D Start = Base + FVector2D(0.0, 100.0 * Row3++);
		FString Bike, Person;
		if (!CreatePair(Start, 0.0, TEXT("city"), SideName, TEXT("manny"), Bike, Person)) { continue; }

		// 사람 id 는 호출 전체 거절, 아무것도 안 옮긴다.
		const FVector2D Before = PaPos(Listed(Bike));
		Refused(TEXT("agent.setPose"), FString::Printf(TEXT("{\"items\":[{\"id\":\"%s\",\"pos\":{\"x\":%.3f,\"y\":%.3f},\"yawDeg\":0},{\"id\":\"%s\",\"pos\":{\"x\":%.3f,\"y\":%.3f},\"yawDeg\":0}]}"), *Bike, Start.X + 5.0, Start.Y, *Person, Start.X, Start.Y));
		TestTrue(What + TEXT(": 거절 뒤 자전거 그대로"), PaPos(Listed(Bike)).Equals(Before, 1e-3));

		// 자전거 id 로 옮기면 사람이 함께 간다.
		Ok(TEXT("agent.setPose"), FString::Printf(TEXT("{\"items\":[{\"id\":\"%s\",\"pos\":{\"x\":%.3f,\"y\":%.3f},\"yawDeg\":90,\"speedMps\":1.2}]}"), *Bike, Start.X + 3.0, Start.Y + 2.0));
		{
			const TSharedPtr<FJsonObject> P = Listed(Person);
			TestTrue(What + TEXT(": setPose 자리"), PaPos(P).Equals(Expected(Bike, Side, 0.15), 2e-3));
			TestEqual(What + TEXT(": setPose 방향"), P->GetNumberField(TEXT("directionDeg")), 90.0, 1e-6);
			TestEqual(What + TEXT(": setPose 속도"), P->GetNumberField(TEXT("speedMps")), 1.2, 1e-6);
			TestEqual(What + TEXT(": setPose 뒤 pushing"), P->GetStringField(TEXT("activityType")), FString(TEXT("pushing")));
		}
		Refused(TEXT("agent.setPose"), FString::Printf(TEXT("{\"items\":[{\"id\":\"%s\",\"pos\":{\"x\":%.3f,\"y\":%.3f},\"yawDeg\":90,\"activity\":\"walking\"}]}"), *Bike, Start.X, Start.Y));

		// 사람 id 로 이동: mover = 자전거, 기본 1.3, 상한 2.0.
		const FVector2D From = PaPos(Listed(Bike));
		const TSharedPtr<FJsonObject> Mv = Ok(TEXT("agent.move"), FString::Printf(TEXT("{\"id\":\"%s\",\"path\":[{\"x\":%.3f,\"y\":%.3f}]}"), *Person, From.X, From.Y + 2.0));
		if (Mv.IsValid())
		{
			TestEqual(What + TEXT(": mover"), Mv->GetStringField(TEXT("mover")), Bike);
			TestEqual(What + TEXT(": 기본 속도"), Mv->GetNumberField(TEXT("speedMps")), 1.3, 1e-6);
		}
		const TSharedPtr<FJsonObject> Fast = Ok(TEXT("agent.move"), FString::Printf(TEXT("{\"id\":\"%s\",\"speedMps\":6.0,\"path\":[{\"x\":%.3f,\"y\":%.3f}]}"), *Bike, From.X, From.Y + 2.0));
		if (Fast.IsValid())
		{
			TestEqual(What + TEXT(": 상한 2.0"), Fast->GetNumberField(TEXT("speedMps")), 2.0, 1e-6);
			TestTrue(What + TEXT(": 상한 notes"), Fast->GetArrayField(TEXT("notes")).Num() > 0);
		}

		// 90° 모서리: 사람이 내내 자전거 옆이고, 함께 호를 돌고, 도착한다.
		const FVector2D Corner = From + FVector2D(0.0, 6.0);
		const FVector2D Goal = Corner + FVector2D(-6.0, 0.0);
		const TSharedPtr<FJsonObject> Turn = Ok(TEXT("agent.move"), FString::Printf(TEXT("{\"id\":\"%s\",\"path\":[{\"x\":%.3f,\"y\":%.3f},{\"x\":%.3f,\"y\":%.3f}]}"), *Person, Corner.X, Corner.Y, Goal.X, Goal.Y));
		TestTrue(What + TEXT(": 호를 넣었다"), Turn.IsValid() && Turn->GetArrayField(TEXT("pts")).Num() > 3);
		double MaxOffsetErr = 0.0, MaxPersonStep = 0.0;
		FVector2D PrevP = PaPos(Listed(Person)), PrevDir = FVector2D::ZeroVector;
		for (int32 i = 0; i < 1500; ++i)
		{
			M->Advance(1.f / 30.f);
			const TSharedPtr<FJsonObject> P = Listed(Person);
			MaxOffsetErr = FMath::Max(MaxOffsetErr, FVector2D::Distance(PaPos(P), Expected(Bike, Side, 0.15)));
			const FVector2D Delta = PaPos(P) - PrevP;
			if (Delta.Size() > 0.03)
			{
				if (!PrevDir.IsZero()) { MaxPersonStep = FMath::Max(MaxPersonStep, PaAngleDeg(PrevDir, Delta)); }
				PrevDir = Delta;
				PrevP = PaPos(P);
			}
			if (P->GetStringField(TEXT("state")) != TEXT("moving")) { break; }
		}
		TestTrue(FString::Printf(TEXT("%s: 간격 오차 ≤ 3 mm(%.4f)"), *What, MaxOffsetErr), MaxOffsetErr <= 3e-3);
		TestTrue(FString::Printf(TEXT("%s: 사람 이동 방향 칸당 ≤ 15°(%.1f)"), *What, MaxPersonStep), MaxPersonStep <= 15.0);
		TestEqual(What + TEXT(": 도착"), Listed(Person)->GetStringField(TEXT("state")), FString(TEXT("arrived")));
		TestTrue(What + TEXT(": 자전거 목적지"), PaPos(Listed(Bike)).Equals(Goal, 1e-3));

		// 정지는 어느 id 든.
		const FVector2D Far = PaPos(Listed(Bike)) + FVector2D(-10.0, 0.0);
		Ok(TEXT("agent.move"), FString::Printf(TEXT("{\"id\":\"%s\",\"path\":[{\"x\":%.3f,\"y\":%.3f}]}"), *Bike, Far.X, Far.Y));
		for (int32 i = 0; i < 30; ++i) { M->Advance(1.f / 30.f); }
		Ok(TEXT("agent.stop"), FString::Printf(TEXT("{\"id\":\"%s\"}"), *Person));
		M->Advance(1.f / 30.f);
		TestEqual(What + TEXT(": 사람 id 로 정지"), Listed(Bike)->GetStringField(TEXT("state")), FString(TEXT("stopped")));
		TestEqual(What + TEXT(": 사람도 정지"), Listed(Person)->GetStringField(TEXT("state")), FString(TEXT("stopped")));

		// 스냅샷: 관계·좌우가 복원 뒤에도 유지된다.
		const FParkAgentSnapshot Snap = M->Capture();
		Ok(TEXT("agent.setPose"), FString::Printf(TEXT("{\"items\":[{\"id\":\"%s\",\"pos\":{\"x\":%.3f,\"y\":%.3f},\"yawDeg\":0}]}"), *Bike, Start.X + 40.0, Start.Y));
		M->Restore(Snap, true);
		{
			const TSharedPtr<FJsonObject> P = Listed(Person);
			if (TestTrue(What + TEXT(": 복원 뒤 사람"), P.IsValid()))
			{
				TestEqual(What + TEXT(": 복원 pushingWith"), P->GetStringField(TEXT("pushingWith")), Bike);
				TestEqual(What + TEXT(": 복원 pushSide"), P->GetStringField(TEXT("pushSide")), FString(SideName));
				TestEqual(What + TEXT(": 복원 side"), P->GetObjectField(TEXT("pushOffsetM"))->GetNumberField(TEXT("side")), Side, 1e-6);
				TestTrue(What + TEXT(": 복원 자리"), PaPos(P).Equals(Expected(Bike, Side, 0.15), 2e-3));
				TestEqual(What + TEXT(": 복원 관계"), M->RelationsJson(Person).Num() > 0 ? M->RelationsJson(Person).Last()->AsObject()->GetStringField(TEXT("relationType")) : FString(), FString(TEXT("pushing")));
			}
		}
	}
	M->Delete({}, true, NotFound);

	// 4) 잇기·풀기: agent.push 오른쪽, 이미 이어진 쌍 거절, 없는 id, 해제 뒤 각자 setPose, 삭제 양쪽.
	{
		TArray<FString> Ids;
		FString Err;
		TestTrue(TEXT("사람"), M->Create(EKind::Pedestrian, Base + FVector2D(0, 300), 0.0, false, FString(), FString(), Ids, Err));
		const FString Person = Ids.Num() > 0 ? Ids[0] : FString();
		Ids.Reset();
		TestTrue(TEXT("자전거"), M->Create(EKind::Bicycle, Base + FVector2D(5, 300), 0.0, false, TEXT("sport"), FString(), Ids, Err));
		const FString Bike = Ids.Num() > 0 ? Ids[0] : FString();
		Refused(TEXT("agent.push"), FString::Printf(TEXT("{\"person\":\"%s\",\"bicycle\":\"nobody-9\"}"), *Person));
		Refused(TEXT("agent.push"), FString::Printf(TEXT("{\"person\":\"%s\",\"bicycle\":\"%s\",\"pushSide\":\"up\"}"), *Person, *Bike));
		Refused(TEXT("agent.release"), FString::Printf(TEXT("{\"id\":\"%s\"}"), *Person));
		Ok(TEXT("agent.push"), FString::Printf(TEXT("{\"person\":\"%s\",\"bicycle\":\"%s\",\"pushSide\":\"right\"}"), *Person, *Bike));
		{
			const TSharedPtr<FJsonObject> P = Listed(Person);
			TestTrue(TEXT("push: 오른쪽 자리"), PaPos(P).Equals(Expected(Bike, 0.40, 0.25), 2e-3));
			TestEqual(TEXT("push: pushSide"), P->GetStringField(TEXT("pushSide")), FString(TEXT("right")));
		}
		Refused(TEXT("agent.push"), FString::Printf(TEXT("{\"person\":\"%s\",\"bicycle\":\"%s\"}"), *Person, *Bike));
		const FVector2D PersonAt = PaPos(Listed(Person));
		Ok(TEXT("agent.release"), FString::Printf(TEXT("{\"id\":\"%s\"}"), *Bike));
		{
			const TSharedPtr<FJsonObject> P = Listed(Person), B = Listed(Bike);
			TestTrue(TEXT("해제: 사람 그 자리"), PaPos(P).Equals(PersonAt, 1e-3));
			TestEqual(TEXT("해제: standing"), P->GetStringField(TEXT("activityType")), FString(TEXT("standing")));
			TestTrue(TEXT("해제: pushingWith null"), P->HasTypedField<EJson::Null>(TEXT("pushingWith")));
			TestFalse(TEXT("해제: pushSide 없음"), P->HasField(TEXT("pushSide")));
			TestTrue(TEXT("해제: 자전거 pushingWith null"), B->HasTypedField<EJson::Null>(TEXT("pushingWith")));
			const TArray<TSharedPtr<FJsonValue>> Rel = M->RelationsJson(Person);
			TestTrue(TEXT("해제: 관계 끝남"), Rel.Num() > 0 && !Rel.Last()->AsObject()->GetBoolField(TEXT("active")));
		}
		// 따로 만든 사람·자전거를 각각 setPose — 관계 없이 지금 방식 그대로.
		const FVector2D PA = Base + FVector2D(20, 300), BA = Base + FVector2D(20.3, 300.5);
		Ok(TEXT("agent.setPose"), FString::Printf(TEXT("{\"items\":[{\"id\":\"%s\",\"pos\":{\"x\":%.3f,\"y\":%.3f},\"yawDeg\":45,\"speedMps\":1.3},{\"id\":\"%s\",\"pos\":{\"x\":%.3f,\"y\":%.3f},\"yawDeg\":45,\"speedMps\":1.3}]}"), *Person, PA.X, PA.Y, *Bike, BA.X, BA.Y));
		{
			const TSharedPtr<FJsonObject> P = Listed(Person), B = Listed(Bike);
			TestTrue(TEXT("따로: 사람 자리"), PaPos(P).Equals(PA, 1e-3));
			TestTrue(TEXT("따로: 자전거 자리"), PaPos(B).Equals(BA, 1e-3));
			TestEqual(TEXT("따로: walking"), P->GetStringField(TEXT("activityType")), FString(TEXT("walking")));
			TestTrue(TEXT("따로: 관계 없음"), P->HasTypedField<EJson::Null>(TEXT("pushingWith")));
		}
		// 삭제: 자전거를 지우면 사람은 그 자리에, 사람을 지우면 자전거가 그 자리에 남는다.
		Ok(TEXT("agent.push"), FString::Printf(TEXT("{\"person\":\"%s\",\"bicycle\":\"%s\"}"), *Person, *Bike));
		const FVector2D StandAt = PaPos(Listed(Person));
		Ok(TEXT("agent.delete"), FString::Printf(TEXT("{\"id\":\"%s\"}"), *Bike));
		{
			const TSharedPtr<FJsonObject> P = Listed(Person);
			if (TestTrue(TEXT("자전거 삭제 뒤 사람 남음"), P.IsValid()))
			{
				TestTrue(TEXT("그 자리"), PaPos(P).Equals(StandAt, 1e-3));
				TestEqual(TEXT("선다"), P->GetStringField(TEXT("activityType")), FString(TEXT("standing")));
			}
		}
		FString B2, P2;
		if (CreatePair(Base + FVector2D(0, 320), 0.0, TEXT("city"), TEXT("left"), TEXT("manny"), B2, P2))
		{
			const FVector2D BikeAt = PaPos(Listed(B2));
			Ok(TEXT("agent.delete"), FString::Printf(TEXT("{\"id\":\"%s\"}"), *P2));
			const TSharedPtr<FJsonObject> B = Listed(B2);
			if (TestTrue(TEXT("사람 삭제 뒤 자전거 남음"), B.IsValid()))
			{
				TestTrue(TEXT("자전거 그 자리"), PaPos(B).Equals(BikeAt, 1e-3));
				TestTrue(TEXT("자전거 관계 없음"), B->HasTypedField<EJson::Null>(TEXT("pushingWith")));
			}
		}
	}

	TestEqual(TEXT("안 읽은 인자 없음"), Unread.Num(), 0);
	M->Delete({}, true, NotFound);
	M->Destroy();
	return true;
}

#endif
