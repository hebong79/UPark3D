// Copyright Epic Games, Inc. All Rights Reserved.

#include "RpcAuth.h"
#include "Misc/Parse.h"

namespace Park3DRpcAuth
{
	const TCHAR* const HeaderKeyLower   = TEXT("x-park3d-token");
	const TCHAR* const OriginKeyLower   = TEXT("origin");
	const TCHAR* const HeaderKeyDisplay = TEXT("X-Park3D-Token");

	const TCHAR* ToLogString(EAuthResult Result)
	{
		switch (Result)
		{
		case EAuthResult::Allowed:             return TEXT("Allowed");
		case EAuthResult::DeniedBadToken:      return TEXT("DeniedBadToken");
		case EAuthResult::DeniedNotLoopback:   return TEXT("DeniedNotLoopback");
		case EAuthResult::DeniedBrowserOrigin: return TEXT("DeniedBrowserOrigin");
		default:                               return TEXT("Unknown");
		}
	}

	bool TokensMatch(const FString& Configured, const FString& Presented)
	{
		const FString A = Configured.TrimStartAndEnd();
		const FString B = Presented.TrimStartAndEnd();
		if (A.IsEmpty() || B.IsEmpty())
		{
			return false;
		}
		return A.Equals(B, ESearchCase::CaseSensitive);
	}

	bool IsTokenCharsetValid(const FString& Token)
	{
		if (Token.IsEmpty())
		{
			return false;   // '+' 규약: 빈 토큰은 "미설정"이며 호출 전에 걸러야 한다.
		}
		for (const TCHAR C : Token)
		{
			const bool bAllowed =
				(C >= TEXT('A') && C <= TEXT('Z')) ||
				(C >= TEXT('a') && C <= TEXT('z')) ||
				(C >= TEXT('0') && C <= TEXT('9')) ||
				(C == TEXT('_')) || (C == TEXT('-'));
			if (!bAllowed)
			{
				return false;
			}
		}
		return true;
	}

	bool IsLoopbackRawIp(const TArray<uint8>& RawIp)
	{
		// IPv4: 127.0.0.0/8
		if (RawIp.Num() == 4)
		{
			return RawIp[0] == 127;
		}

		if (RawIp.Num() == 16)
		{
			// ::1
			bool bAllZeroPrefix = true;
			for (int32 i = 0; i < 15; ++i)
			{
				if (RawIp[i] != 0) { bAllZeroPrefix = false; break; }
			}
			if (bAllZeroPrefix && RawIp[15] == 1)
			{
				return true;
			}

			// ::ffff:127.x.x.x (IPv4-mapped 루프백)
			for (int32 i = 0; i < 10; ++i)
			{
				if (RawIp[i] != 0) { return false; }
			}
			return RawIp[10] == 0xFF && RawIp[11] == 0xFF && RawIp[12] == 127;
		}

		// 길이 불명/빈 배열 → fail-closed
		return false;
	}

	EAuthResult Authorize(const FString& ConfiguredToken,
	                      const FString& PresentedToken,
	                      const TArray<uint8>& PeerRawIp,
	                      bool bHasOriginHeader)
	{
		// ① Origin 헤더 존재 → 토큰 설정 여부와 무관하게 무조건 거부(D11).
		//    비브라우저 클라이언트(curl/urllib 브리지/외부 툴/Automation)는 Origin 을 보내지 않으므로 무영향.
		if (bHasOriginHeader)
		{
			return EAuthResult::DeniedBrowserOrigin;
		}

		// ② 토큰이 설정돼 있으면 루프백이어도 토큰을 검사한다.
		if (!ConfiguredToken.TrimStartAndEnd().IsEmpty())
		{
			return TokensMatch(ConfiguredToken, PresentedToken)
				? EAuthResult::Allowed
				: EAuthResult::DeniedBadToken;
		}

		// ③ 토큰 미설정 → 루프백 피어만 허용(기존 로컬 워크플로 보존). 외부 호스트는 차단.
		return IsLoopbackRawIp(PeerRawIp)
			? EAuthResult::Allowed
			: EAuthResult::DeniedNotLoopback;
	}

	FString ResolveBindAddress(const TArray<FString>& ListenerOverrides,
	                           int32 Port,
	                           const FString& DefaultBindAddress)
	{
		FString Resolved = DefaultBindAddress;

		for (const FString& Entry : ListenerOverrides)
		{
			FString S = Entry;
			S.TrimStartAndEndInline();
			S.ReplaceInline(TEXT("("), TEXT(""));
			S.ReplaceInline(TEXT(")"), TEXT(""));

			// 엔진과 동일: Port 를 명시하지 않은 항목은 건너뛴다.
			uint32 ConfiguredPort = 0;
			if (!FParse::Value(*S, TEXT("Port="), ConfiguredPort))
			{
				continue;
			}

			// 엔진과 동일: 첫 매칭에서 종료(뒤에 같은 포트가 또 있어도 무시).
			if (Port >= 0 && static_cast<uint32>(Port) == ConfiguredPort)
			{
				FParse::Value(*S, TEXT("BindAddress="), Resolved);
				break;
			}
		}

		return Resolved;
	}
}
