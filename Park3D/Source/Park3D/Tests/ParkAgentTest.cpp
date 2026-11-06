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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParkAgentWallLoopsTest, "Park3D.Agent.WallLoops",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FParkAgentWallLoopsTest::RunTest(const FString& Parameters)
{
	// 20×20 m, 칸 0.2 m, 전부 노면.
	auto MakeGrid = []()
	{
		FAgentGrid G;
		G.W = G.H = 101;
		G.Cell = 0.2;
		G.Cls.Init(CellGround, G.W * G.H);
		G.HeightM.Init(0.f, G.W * G.H);
		return G;
	};
	auto ClsAt = [](const FAgentGrid& G, double X, double Y)
	{
		int32 CX, CY;
		return G.ToCell(FVector2D(X, Y), CX, CY) ? G.Cls[CY * G.W + CX] : static_cast<uint8>(255);
	};
	FWallLoop OuterL;   // ㄱ자 바깥벽: (10..18, 10..18) 이 파여 있다
	OuterL.Pts = { {2, 2}, {18, 2}, {18, 10}, {10, 10}, {10, 18}, {2, 18} };
	FWallLoop Pillar;
	Pillar.Pts = { {5, 5}, {6, 5}, {6, 6}, {5, 6} };
	FWallLoop Line;
	Line.bClosed = false;
	Line.Pts = { {12, 4}, {16, 4} };
	const TArray<FWallLoop> All = { OuterL, Pillar, Line };

	// 1) 면이 모두 바깥벽 안: 밖·기둥·선이 막히고 나머지는 그대로다.
	{
		FAgentGrid G = MakeGrid();
		int32 CX, CY;
		G.ToCell(FVector2D(4, 8), CX, CY);
		G.Cls[CY * G.W + CX] = CellBay;
		const FWallStamp R = ApplyWallLoops(G, All, { FVector2D(4, 8), FVector2D(8, 4) });
		TestEqual(TEXT("바깥벽 수"), R.OuterLoops, 1);
		TestTrue(TEXT("바깥벽 밖을 막았다"), R.bOuterApplied);
		TestEqual(TEXT("바깥벽 밖"), ClsAt(G, 1, 1), static_cast<uint8>(CellBlocked));
		TestEqual(TEXT("파인 곳도 밖"), ClsAt(G, 15, 15), static_cast<uint8>(CellBlocked));
		TestEqual(TEXT("안쪽 노면"), ClsAt(G, 4, 4), static_cast<uint8>(CellGround));
		TestEqual(TEXT("주차면 유지"), ClsAt(G, 4, 8), static_cast<uint8>(CellBay));
		TestEqual(TEXT("기둥 속"), ClsAt(G, 5.5, 5.5), static_cast<uint8>(CellBlocked));
		TestEqual(TEXT("열린 선"), ClsAt(G, 14, 4), static_cast<uint8>(CellBlocked));
		TestEqual(TEXT("선 옆"), ClsAt(G, 14, 6), static_cast<uint8>(CellGround));
		int32 Blocked = 0;
		for (const uint8 C : G.Cls) { Blocked += C == CellBlocked; }
		TestEqual(TEXT("새로 막은 칸 수"), R.Cells, Blocked);

		// 사람 경로: 기둥을 돌아가고, 바깥벽 밖으로는 못 간다.
		TArray<float> Clear;
		ClearanceField(G, EKind::Pedestrian, nullptr, Clear);
		TArray<FVector2D> Pts;
		FString Err;
		TestTrue(TEXT("기둥 건너편 경로"), FindPath(G, Clear, 0.3, FVector2D(4, 5.5), FVector2D(7, 5.5), Pts, Err));
		for (int32 k = 0; k + 1 < Pts.Num(); ++k)
		{
			TestTrue(TEXT("경로가 기둥을 안 뚫는다"), SegmentClear(G, Clear, Pts[k], Pts[k + 1], 0.3));
		}
		TestFalse(TEXT("바깥벽 밖으로 못 간다"), FindPath(G, Clear, 0.3, FVector2D(4, 4), FVector2D(15, 15), Pts, Err));
	}

	// 2) 면 하나가 바깥벽 밖: 바깥벽 밖은 그대로 두고 기둥·선만 막는다.
	{
		FAgentGrid G = MakeGrid();
		const FWallStamp R = ApplyWallLoops(G, All, { FVector2D(4, 8), FVector2D(19, 19) });
		TestEqual(TEXT("바깥벽은 찾았다"), R.OuterLoops, 1);
		TestFalse(TEXT("바깥벽 밖을 안 막는다"), R.bOuterApplied);
		TestEqual(TEXT("밖 그대로"), ClsAt(G, 1, 1), static_cast<uint8>(CellGround));
		TestEqual(TEXT("바깥벽 안쪽 그대로"), ClsAt(G, 4, 4), static_cast<uint8>(CellGround));
		TestEqual(TEXT("기둥은 막힌다"), ClsAt(G, 5.5, 5.5), static_cast<uint8>(CellBlocked));
	}

	// 3) 면이 어느 고리에도 없다(건물만 있는 야외): 큰 고리도 속을 채우고 밖은 그대로다.
	{
		FAgentGrid G = MakeGrid();
		const FWallStamp R = ApplyWallLoops(G, { OuterL }, { FVector2D(19, 19) });
		TestEqual(TEXT("바깥벽 없음"), R.OuterLoops, 0);
		TestEqual(TEXT("건물 속"), ClsAt(G, 4, 4), static_cast<uint8>(CellBlocked));
		TestEqual(TEXT("건물 밖"), ClsAt(G, 19, 19), static_cast<uint8>(CellGround));
	}
	return true;
}

#endif
