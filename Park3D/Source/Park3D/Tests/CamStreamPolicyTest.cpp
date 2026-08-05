// Copyright Epic Games, Inc. All Rights Reserved.
// CamStreamPolicyTest : 카메라별 스트리밍의 순수 로직 검증(소켓·RHI·월드 비의존).
// 포트 부여·채널 증감·슬롯 스케줄링·fps 배분만 다룬다. 실제 송신은 실RHI 통합 테스트 영역이다.

#include "Misc/AutomationTest.h"
#include "../Rpc/CamStreamPolicy.h"

#if WITH_DEV_AUTOMATION_TESTS

using Park3DCamStream::FSlotCandidate;

namespace
{
	FSlotCandidate MakeCandidate(int32 CamId, int32 Clients)
	{
		FSlotCandidate C;
		C.CamId = CamId;
		C.ClientCount = Clients;
		return C;
	}
}

// ===== U1: 포트 부여 =====
// 포트↔카메라 매칭이 이 함수 하나로 결정된다. 어긋나면 다른 카메라 영상이 나가는 조용한 실패다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCamStreamResolvePortTest,
	"Park3D.Rpc.CamStream.ResolvePort",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCamStreamResolvePortTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("cam1 = 13601"), Park3DCamStream::ResolvePort(13600, 1, 10), 13601);
	TestEqual(TEXT("cam2 = 13602"), Park3DCamStream::ResolvePort(13600, 2, 10), 13602);
	TestEqual(TEXT("cam10 = 13610"), Park3DCamStream::ResolvePort(13600, 10, 10), 13610);

	// 범위 밖은 0 = 채널 미개설(포트를 억지로 만들어 다른 서비스와 충돌시키지 않는다).
	TestEqual(TEXT("camId 0 거부"), Park3DCamStream::ResolvePort(13600, 0, 10), 0);
	TestEqual(TEXT("camId 음수 거부"), Park3DCamStream::ResolvePort(13600, -3, 10), 0);
	TestEqual(TEXT("MaxCameras 초과 거부"), Park3DCamStream::ResolvePort(13600, 11, 10), 0);
	TestEqual(TEXT("BasePort 0 거부"), Park3DCamStream::ResolvePort(0, 1, 10), 0);

	// 기존 서비스 포트와 겹치면 안 된다(13510 RPC / 13520 MCP 브리지).
	for (int32 CamId = 1; CamId <= 10; ++CamId)
	{
		const int32 Port = Park3DCamStream::ResolvePort(13600, CamId, 10);
		TestTrue(TEXT("13510/13520 과 무충돌"), Port != 13510 && Port != 13520);
	}
	return true;
}

// ===== U2: 채널 증감 =====
// 전체 재기동이 아니라 '변한 것만' 열고 닫아야 살아있는 스트림이 끊기지 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCamStreamDiffChannelsTest,
	"Park3D.Rpc.CamStream.DiffChannels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCamStreamDiffChannelsTest::RunTest(const FString& Parameters)
{
	TArray<int32> Open, Close;

	Park3DCamStream::DiffChannels(2, 3, 10, Open, Close);
	TestEqual(TEXT("2→3 개설 1건"), Open.Num(), 1);
	TestEqual(TEXT("2→3 개설 대상 cam3"), Open.Num() > 0 ? Open[0] : -1, 3);
	TestEqual(TEXT("2→3 정지 없음"), Close.Num(), 0);

	Park3DCamStream::DiffChannels(3, 1, 10, Open, Close);
	TestEqual(TEXT("3→1 개설 없음"), Open.Num(), 0);
	TestEqual(TEXT("3→1 정지 2건"), Close.Num(), 2);
	// 뒤에서부터 닫아야 인덱스가 유효하다.
	TestEqual(TEXT("3→1 정지 순서 cam3 먼저"), Close.Num() > 0 ? Close[0] : -1, 3);
	TestEqual(TEXT("3→1 정지 그다음 cam2"), Close.Num() > 1 ? Close[1] : -1, 2);

	Park3DCamStream::DiffChannels(0, 0, 10, Open, Close);
	TestEqual(TEXT("0→0 무변화(개설)"), Open.Num(), 0);
	TestEqual(TEXT("0→0 무변화(정지)"), Close.Num(), 0);

	// 상한 초과분은 개설 대상에서 빠진다(카메라 12대여도 채널은 10개).
	Park3DCamStream::DiffChannels(0, 12, 10, Open, Close);
	TestEqual(TEXT("12대 요청 → 10채널"), Open.Num(), 10);
	TestEqual(TEXT("마지막은 cam10"), Open.Num() == 10 ? Open[9] : -1, 10);

	return true;
}

// ===== U3: 기본 선택 — 클라이언트 없는 채널은 절대 캡처하지 않는다 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCamStreamSelectBasicTest,
	"Park3D.Rpc.CamStream.SelectSlots.Basic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCamStreamSelectBasicTest::RunTest(const FString& Parameters)
{
	TArray<FSlotCandidate> C;
	C.Add(MakeCandidate(1, 0));   // 시청자 없음
	C.Add(MakeCandidate(2, 1));
	C.Add(MakeCandidate(3, 0));   // 시청자 없음

	TArray<int32> Holders;
	Park3DCamStream::SelectSlots(C, /*Slots=*/1, /*Now=*/100.0, 2.f, 3.f, Holders);
	TestEqual(TEXT("슬롯 1개만 배정"), Holders.Num(), 1);
	TestEqual(TEXT("시청 중인 cam2 선택"), Holders.Num() > 0 ? Holders[0] : -1, 2);

	// 아무도 안 보면 아무도 캡처하지 않는다 = 유휴 비용 0.
	TArray<FSlotCandidate> None;
	None.Add(MakeCandidate(1, 0));
	Park3DCamStream::SelectSlots(None, 2, 100.0, 2.f, 3.f, Holders);
	TestEqual(TEXT("시청자 0이면 캡처 0"), Holders.Num(), 0);

	// 슬롯 0/음수 방어.
	Park3DCamStream::SelectSlots(C, 0, 100.0, 2.f, 3.f, Holders);
	TestEqual(TEXT("슬롯 0이면 배정 없음"), Holders.Num(), 0);

	return true;
}

// ===== U4: 최소 점유 시간 =====
// 이게 없으면 슬롯이 매 틱 옮겨다녀 각 카메라가 프레임 한 장씩만 받고 전부 정지화면이 된다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCamStreamMinHoldTest,
	"Park3D.Rpc.CamStream.SelectSlots.MinHold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCamStreamMinHoldTest::RunTest(const FString& Parameters)
{
	TArray<FSlotCandidate> C;
	// cam1: 방금(0.5초 전) 슬롯을 잡은 보유자. 점수는 낮다.
	FSlotCandidate A = MakeCandidate(1, 1);
	A.bHoldsSlot = true;
	A.SlotSince = 99.5;
	A.LastServedTime = 100.0;
	C.Add(A);
	// cam2: 고정(pin) 이라 점수가 훨씬 높다.
	FSlotCandidate B = MakeCandidate(2, 1);
	B.bPinned = true;
	B.LastServedTime = 0.0;
	C.Add(B);

	TArray<int32> Holders;
	Park3DCamStream::SelectSlots(C, 1, 100.0, /*MinHold=*/2.f, 3.f, Holders);
	TestEqual(TEXT("최소 점유 중이면 유지"), Holders.Num() > 0 ? Holders[0] : -1, 1);

	// 최소 점유가 지나면 더 높은 점수가 가져간다.
	Park3DCamStream::SelectSlots(C, 1, /*Now=*/102.0, 2.f, 3.f, Holders);
	TestEqual(TEXT("최소 점유 경과 후 승계"), Holders.Num() > 0 ? Holders[0] : -1, 2);

	return true;
}

// ===== U5: 기아 방지(순환) =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCamStreamStarvationTest,
	"Park3D.Rpc.CamStream.SelectSlots.Starvation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCamStreamStarvationTest::RunTest(const FString& Parameters)
{
	// 조건이 완전히 같은 두 채널. 하나는 방금 서비스받았고 하나는 오래 굶었다.
	TArray<FSlotCandidate> C;
	FSlotCandidate A = MakeCandidate(1, 1);
	A.bHoldsSlot = true;
	A.SlotSince = 90.0;          // 최소 점유는 이미 경과
	A.LastServedTime = 100.0;    // 방금까지 서비스
	C.Add(A);
	FSlotCandidate B = MakeCandidate(2, 1);
	B.LastServedTime = 70.0;     // 30초 굶음
	C.Add(B);

	TArray<int32> Holders;
	Park3DCamStream::SelectSlots(C, 1, 100.0, 2.f, 3.f, Holders);
	TestEqual(TEXT("굶은 채널이 승계"), Holders.Num() > 0 ? Holders[0] : -1, 2);

	// 슬롯이 2개면 둘 다 들어간다(순환 자체가 일어나지 않는다).
	Park3DCamStream::SelectSlots(C, 2, 100.0, 2.f, 3.f, Holders);
	TestEqual(TEXT("슬롯 2면 둘 다"), Holders.Num(), 2);

	return true;
}

// ===== U6: 우선순위 서열 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCamStreamPriorityTest,
	"Park3D.Rpc.CamStream.SelectSlots.Priority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCamStreamPriorityTest::RunTest(const FString& Parameters)
{
	const double Now = 100.0;

	// pin > 최근 PTZ > 선택 카메라 > 클라이언트 수
	FSlotCandidate Pinned = MakeCandidate(1, 1);  Pinned.bPinned = true;
	FSlotCandidate Ptz    = MakeCandidate(2, 1);  Ptz.LastPtzTime = Now - 1.0;
	FSlotCandidate Sel    = MakeCandidate(3, 1);  Sel.bSelected = true;
	FSlotCandidate Many   = MakeCandidate(4, 9);

	TestTrue(TEXT("pin > 최근 PTZ"),
		Park3DCamStream::ScoreCandidate(Pinned, Now, 3.f) > Park3DCamStream::ScoreCandidate(Ptz, Now, 3.f));
	TestTrue(TEXT("최근 PTZ > 선택 카메라"),
		Park3DCamStream::ScoreCandidate(Ptz, Now, 3.f) > Park3DCamStream::ScoreCandidate(Sel, Now, 3.f));
	TestTrue(TEXT("선택 카메라 > 클라이언트 수"),
		Park3DCamStream::ScoreCandidate(Sel, Now, 3.f) > Park3DCamStream::ScoreCandidate(Many, Now, 3.f));

	// PTZ 가산은 PtzRecentSeconds 를 넘기면 사라진다(조작이 끝나면 우선권 반납).
	FSlotCandidate Old = MakeCandidate(5, 1);
	Old.LastPtzTime = Now - 10.0;
	TestTrue(TEXT("오래된 PTZ 는 가산 없음"),
		Park3DCamStream::ScoreCandidate(Old, Now, 3.f) < Park3DCamStream::ScoreCandidate(Ptz, Now, 3.f));

	// 조작 이력이 없는 센티널(-1e9)이 "방금 조작"으로 오탐하지 않는다(월드 t≈0 방어).
	FSlotCandidate Fresh = MakeCandidate(6, 1);
	TestTrue(TEXT("센티널은 PTZ 가산 없음"),
		Park3DCamStream::ScoreCandidate(Fresh, /*Now=*/0.0, 3.f) < 2000.0);

	// 동점은 camId 오름차순(결정적 — 같은 입력이면 항상 같은 결과).
	TArray<FSlotCandidate> Tie;
	Tie.Add(MakeCandidate(7, 1));
	Tie.Add(MakeCandidate(3, 1));
	TArray<int32> Holders;
	Park3DCamStream::SelectSlots(Tie, 1, Now, 2.f, 3.f, Holders);
	TestEqual(TEXT("동점은 작은 camId"), Holders.Num() > 0 ? Holders[0] : -1, 3);

	return true;
}

// ===== U6b: 회귀 — 선택 카메라가 슬롯을 독점하면 안 된다 =====
// 실측 결함: 선택 카메라 가중치가 기아 점수(최대 60)를 압도해 20초 동안 순환이 0회였고
// 나머지 두 채널은 클라이언트가 붙어 있는데도 한 프레임도 받지 못했다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCamStreamSelectedNoMonopolyTest,
	"Park3D.Rpc.CamStream.SelectSlots.SelectedNoMonopoly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCamStreamSelectedNoMonopolyTest::RunTest(const FString& Parameters)
{
	const double Now = 100.0;

	TArray<FSlotCandidate> C;
	FSlotCandidate Sel = MakeCandidate(1, 1);
	Sel.bSelected = true;
	Sel.bHoldsSlot = true;
	Sel.SlotSince = 90.0;        // 최소 점유는 이미 경과
	Sel.LastServedTime = Now;    // 계속 서비스받는 중
	C.Add(Sel);
	FSlotCandidate Other = MakeCandidate(2, 1);
	Other.LastServedTime = 95.0; // 5초 굶음
	C.Add(Other);

	TArray<int32> Holders;
	Park3DCamStream::SelectSlots(C, 1, Now, 2.f, 3.f, Holders);
	TestEqual(TEXT("굶은 채널이 선택 카메라를 이긴다"), Holders.Num() > 0 ? Holders[0] : -1, 2);

	// 반면 pin 은 의도적으로 순환을 멈춘다 — 60초를 굶어도 고정 채널이 이긴다.
	TArray<FSlotCandidate> P;
	FSlotCandidate Pin = MakeCandidate(1, 1);
	Pin.bPinned = true;
	Pin.bHoldsSlot = true;
	Pin.SlotSince = 0.0;
	Pin.LastServedTime = Now;
	P.Add(Pin);
	FSlotCandidate Hungry = MakeCandidate(2, 1);
	Hungry.LastServedTime = 0.0;  // 100초 굶음(포화)
	P.Add(Hungry);

	Park3DCamStream::SelectSlots(P, 1, Now, 2.f, 3.f, Holders);
	TestEqual(TEXT("pin 은 순환을 멈춘다"), Holders.Num() > 0 ? Holders[0] : -1, 1);

	return true;
}

// ===== U7: fps 예산 배분 =====
// 슬롯을 늘려도 게임 스레드 부하가 일정해야 한다 — 이 나눗셈이 그 보장이다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCamStreamFpsBudgetTest,
	"Park3D.Rpc.CamStream.FpsBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCamStreamFpsBudgetTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("슬롯1 = 총예산"), Park3DCamStream::ResolveChannelFps(5.f, 1, true), 5.f);
	TestEqual(TEXT("슬롯2 = 절반"), Park3DCamStream::ResolveChannelFps(5.f, 2, true), 2.5f);
	TestEqual(TEXT("공유 끄면 고정"), Park3DCamStream::ResolveChannelFps(5.f, 2, false), 5.f);
	TestEqual(TEXT("슬롯 0 방어(1로 취급)"), Park3DCamStream::ResolveChannelFps(5.f, 0, true), 5.f);
	TestTrue(TEXT("하한 클램프(0 나눗셈/무한 루프 방지)"), Park3DCamStream::ResolveChannelFps(0.f, 4, true) > 0.f);
	TestEqual(TEXT("상한 클램프"), Park3DCamStream::ResolveChannelFps(999.f, 1, true), 60.f);

	return true;
}

// ===== U8: 상태 문자열 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCamStreamStatusLineTest,
	"Park3D.Rpc.CamStream.StatusLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCamStreamStatusLineTest::RunTest(const FString& Parameters)
{
	const FString Idle = Park3DCamStream::FormatChannelStatus(1, 13601, 0, false, 0.f);
	TestTrue(TEXT("시청자 없음 표시"), Idle.Contains(TEXT("시청자 없음")) && Idle.Contains(TEXT("13601")));

	const FString Wait = Park3DCamStream::FormatChannelStatus(2, 13602, 1, false, 0.f);
	TestTrue(TEXT("대기 표시"), Wait.Contains(TEXT("대기")));

	const FString Live = Park3DCamStream::FormatChannelStatus(3, 13603, 2, true, 4.9f);
	TestTrue(TEXT("송출 fps 표시"), Live.Contains(TEXT("4.9")) && Live.Contains(TEXT("슬롯")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
