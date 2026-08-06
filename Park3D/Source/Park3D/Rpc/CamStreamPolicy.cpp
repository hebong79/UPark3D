// Copyright Epic Games, Inc. All Rights Reserved.

#include "CamStreamPolicy.h"

namespace Park3DCamStream
{
	int32 ResolvePort(int32 BasePort, int32 CamId, int32 MaxCameras)
	{
		if (CamId <= 0 || CamId > MaxCameras || BasePort <= 0)
		{
			return 0;
		}
		return BasePort + CamId;
	}

	bool PortRangeToBase(int32 MinPort, int32 MaxPort, int32& OutBasePort, int32& OutMaxCameras)
	{
		if (MinPort < 2 || MaxPort < MinPort || MaxPort > 65535)
		{
			return false;
		}
		OutBasePort = MinPort - 1;
		OutMaxCameras = MaxPort - MinPort + 1;
		return true;
	}

	int32 ExtendedMaxPort(int32 MinPort, int32 CamCount)
	{
		if (MinPort < 2 || MinPort > 65535 || CamCount <= 0)
		{
			return 0;
		}
		const int64 Wanted = static_cast<int64>(MinPort) + CamCount - 1;
		return static_cast<int32>(FMath::Min<int64>(Wanted, 65535));
	}

	void DiffChannels(int32 OldCount, int32 NewCount, int32 MaxCameras,
	                  TArray<int32>& OutToOpen, TArray<int32>& OutToClose)
	{
		OutToOpen.Reset();
		OutToClose.Reset();

		const int32 OldEff = FMath::Clamp(OldCount, 0, MaxCameras);
		const int32 NewEff = FMath::Clamp(NewCount, 0, MaxCameras);

		for (int32 CamId = OldEff + 1; CamId <= NewEff; ++CamId)
		{
			OutToOpen.Add(CamId);
		}
		for (int32 CamId = OldEff; CamId > NewEff; --CamId)
		{
			OutToClose.Add(CamId);
		}
	}

	double ScoreCandidate(const FSlotCandidate& C, double Now, float PtzRecentSeconds)
	{
		// 가중치 설계(중요): 기본 동작은 '공평한 순환'이고, 우선순위는 그 위에 얹는 예외다.
		// 절대 우위(pin·PTZ)만 순환을 멈출 수 있고, 나머지는 굶은 시간(초당 +1)에 밀리도록
		// 작게 잡는다. 선택 카메라에 큰 값을 주면 그 카메라가 슬롯을 영구 독점해
		// 나머지가 한 프레임도 못 받는다(실측으로 확인한 결함 — 20초간 순환 0회).
		double S = 0.0;
		if (C.bPinned)
		{
			S += 4000.0;   // 사용자가 "이 카메라 계속"으로 고정 — 순환 중지가 의도된 유일한 경우
		}
		if ((Now - C.LastPtzTime) <= static_cast<double>(PtzRecentSeconds))
		{
			S += 2000.0;   // 조작 중 화면이 안 움직이면 제어 자체가 불가능하다(시간 제한 우위)
		}
		if (C.bSelected)
		{
			S += 2.0;      // 뷰어가 보는 화면에 살짝 가산(≈2초치) — 순환을 막지는 못한다
		}
		S += 0.1 * FMath::Min(C.ClientCount, 10);
		// 기아 방지: 굶은 시간이 곧 점수(초당 +1). 60초에서 포화. 이 항이 지배적이라 순환이 보장된다.
		S += FMath::Clamp(Now - C.LastServedTime, 0.0, 60.0);
		return S;
	}

	void SelectSlots(const TArray<FSlotCandidate>& Candidates, int32 Slots, double Now,
	                 float MinHoldSeconds, float PtzRecentSeconds, TArray<int32>& OutHolders)
	{
		OutHolders.Reset();
		if (Slots <= 0)
		{
			return;
		}

		// (1) 후보 = 클라이언트가 붙은 채널뿐. 10대가 있어도 보는 사람이 없으면 캡처 비용은 0이다.
		TArray<const FSlotCandidate*> Pool;
		for (const FSlotCandidate& C : Candidates)
		{
			if (C.ClientCount > 0)
			{
				Pool.Add(&C);
			}
		}
		if (Pool.Num() == 0)
		{
			return;
		}

		// 점수 내림차순, 동점은 camId 오름차순(결정적 — 테스트 가능).
		auto ByScore = [Now, PtzRecentSeconds](const FSlotCandidate& A, const FSlotCandidate& B)
		{
			const double SA = ScoreCandidate(A, Now, PtzRecentSeconds);
			const double SB = ScoreCandidate(B, Now, PtzRecentSeconds);
			if (SA != SB)
			{
				return SA > SB;
			}
			return A.CamId < B.CamId;
		};

		// (2) 최소 점유 시간이 지나지 않은 보유자를 먼저 확정한다.
		//     이게 없으면 슬롯이 매 틱 옮겨다녀 각 카메라가 프레임 한 장씩만 받고,
		//     결과적으로 모든 화면이 정지화면처럼 보인다(순수 라운드로빈의 함정).
		TArray<const FSlotCandidate*> Locked;
		TArray<const FSlotCandidate*> Rest;
		for (const FSlotCandidate* C : Pool)
		{
			const bool bHoldLocked = C->bHoldsSlot && (Now - C->SlotSince) < static_cast<double>(MinHoldSeconds);
			(bHoldLocked ? Locked : Rest).Add(C);
		}

		// 슬롯을 줄인 직후엔 잠긴 보유자가 슬롯 수보다 많을 수 있다 → 점수 높은 쪽부터 남긴다.
		// TArray<T*>::Sort 는 포인터를 역참조해 술어에 넘긴다(TDereferenceWrapper).
		Locked.Sort(ByScore);
		Rest.Sort(ByScore);

		for (const FSlotCandidate* C : Locked)
		{
			if (OutHolders.Num() >= Slots) { break; }
			OutHolders.Add(C->CamId);
		}
		for (const FSlotCandidate* C : Rest)
		{
			if (OutHolders.Num() >= Slots) { break; }
			OutHolders.Add(C->CamId);
		}

		OutHolders.Sort();
	}

	float ResolveChannelFps(float TotalFps, int32 Slots, bool bShare)
	{
		const float Base = bShare ? (TotalFps / static_cast<float>(FMath::Max(1, Slots))) : TotalFps;
		return FMath::Clamp(Base, 0.1f, 60.f);
	}

	FString FormatChannelStatus(int32 CamId, int32 Port, int32 ClientCount, bool bHoldsSlot, float MeasuredFps)
	{
		if (ClientCount <= 0)
		{
			return FString::Printf(TEXT("cam%d  :%d  — 시청자 없음"), CamId, Port);
		}
		if (!bHoldsSlot)
		{
			return FString::Printf(TEXT("cam%d  :%d  — 대기(슬롯 없음)  클라이언트 %d"), CamId, Port, ClientCount);
		}
		return FString::Printf(TEXT("cam%d  :%d  ▶ %.1f fps  클라이언트 %d  [슬롯]"),
			CamId, Port, MeasuredFps, ClientCount);
	}
}
