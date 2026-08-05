// Copyright Epic Games, Inc. All Rights Reserved.
// RpcAuthTest : RPC 인증 게이트 순수 판정 로직 검증(HTTP/소켓 비의존).
// 판정 함수가 TArray<uint8>·FString 만 받도록 분리되어 있어 소켓 없이 전 경로를 덮는다.

#include "Misc/AutomationTest.h"
#include "../Rpc/RpcAuth.h"

#if WITH_DEV_AUTOMATION_TESTS

using Park3DRpcAuth::EAuthResult;

namespace
{
	/** 바이트 배열 리터럴 헬퍼. */
	TArray<uint8> Bytes(std::initializer_list<uint8> In)
	{
		TArray<uint8> Out;
		Out.Append(In.begin(), static_cast<int32>(In.size()));
		return Out;
	}

	/** 16바이트 IPv6 ::1 */
	TArray<uint8> IPv6Loopback()
	{
		TArray<uint8> Out;
		Out.Init(0, 16);
		Out[15] = 1;
		return Out;
	}

	/** 16바이트 IPv4-mapped(::ffff:a.b.c.d) */
	TArray<uint8> IPv4Mapped(uint8 A, uint8 B, uint8 C, uint8 D)
	{
		TArray<uint8> Out;
		Out.Init(0, 16);
		Out[10] = 0xFF; Out[11] = 0xFF;
		Out[12] = A; Out[13] = B; Out[14] = C; Out[15] = D;
		return Out;
	}
}

// ===== T-U2: 헤더 키는 반드시 소문자 =====
// 엔진이 수신 헤더 키를 ToLower() 정규화하므로(HttpConnectionRequestReadContext.cpp:355)
// 대문자 표기로 Find() 하면 항상 nullptr → "토큰을 제대로 보냈는데 계속 401"의 유일한 원인이 된다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcAuthHeaderKeyTest,
	"Park3D.Rpc.Auth.HeaderKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcAuthHeaderKeyTest::RunTest(const FString& Parameters)
{
	const FString TokenKey(Park3DRpcAuth::HeaderKeyLower);
	const FString OriginKey(Park3DRpcAuth::OriginKeyLower);

	TestEqual(TEXT("토큰 조회 키"), TokenKey, FString(TEXT("x-park3d-token")));
	TestEqual(TEXT("Origin 조회 키"), OriginKey, FString(TEXT("origin")));

	// 대문자가 하나라도 있으면 조회가 영구 실패한다 — 표기 실수 회귀 방지.
	TestEqual(TEXT("토큰 키에 대문자 없음"), TokenKey, TokenKey.ToLower());
	TestEqual(TEXT("Origin 키에 대문자 없음"), OriginKey, OriginKey.ToLower());

	// 송신(응답) 헤더는 정규화되지 않으므로 표기용 상수는 대문자를 유지해야 한다.
	TestEqual(TEXT("응답 표기용 이름"), FString(Park3DRpcAuth::HeaderKeyDisplay), FString(TEXT("X-Park3D-Token")));

	return true;
}

// ===== T-U6: 익명 허용(AllowAnonymous) =====
// 우회 스위치는 "켜면 전부 통과 / 끄면 종전과 100% 동일" 두 가지를 모두 지켜야 한다.
// 특히 기본값이 false 라는 것 — 실수로 기본이 뒤집히면 서버가 무인증으로 열린다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcAuthAllowAnonymousTest,
	"Park3D.Rpc.Auth.AllowAnonymous",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcAuthAllowAnonymousTest::RunTest(const FString& Parameters)
{
	const TArray<uint8> External = Bytes({ 192, 168, 0, 200 });
	const TArray<uint8> Loopback = Bytes({ 127, 0, 0, 1 });

	// 켠 경우: 종전이라면 거부됐을 모든 조합이 통과한다.
	TestEqual(TEXT("익명: 외부+무토큰"),
		Park3DRpcAuth::Authorize(TEXT("SECRET"), FString(), External, false, true), EAuthResult::Allowed);
	TestEqual(TEXT("익명: 외부+틀린토큰"),
		Park3DRpcAuth::Authorize(TEXT("SECRET"), TEXT("WRONG"), External, false, true), EAuthResult::Allowed);
	TestEqual(TEXT("익명: Origin 헤더(브라우저 fetch)"),
		Park3DRpcAuth::Authorize(TEXT("SECRET"), TEXT("SECRET"), External, true, true), EAuthResult::Allowed);
	TestEqual(TEXT("익명: 토큰 미설정+외부"),
		Park3DRpcAuth::Authorize(FString(), FString(), External, false, true), EAuthResult::Allowed);
	TestEqual(TEXT("익명: peer 불명(빈 IP)"),
		Park3DRpcAuth::Authorize(FString(), FString(), TArray<uint8>(), false, true), EAuthResult::Allowed);

	// 끈 경우: 종전 판정이 그대로여야 한다(회귀 방지).
	TestEqual(TEXT("비익명: 외부+무토큰 → DeniedBadToken"),
		Park3DRpcAuth::Authorize(TEXT("SECRET"), FString(), External, false, false), EAuthResult::DeniedBadToken);
	TestEqual(TEXT("비익명: Origin → DeniedBrowserOrigin"),
		Park3DRpcAuth::Authorize(TEXT("SECRET"), TEXT("SECRET"), External, true, false), EAuthResult::DeniedBrowserOrigin);
	TestEqual(TEXT("비익명: 토큰 미설정+외부 → DeniedNotLoopback"),
		Park3DRpcAuth::Authorize(FString(), FString(), External, false, false), EAuthResult::DeniedNotLoopback);
	TestEqual(TEXT("비익명: 토큰 일치 → Allowed"),
		Park3DRpcAuth::Authorize(TEXT("SECRET"), TEXT("SECRET"), External, false, false), EAuthResult::Allowed);
	TestEqual(TEXT("비익명: 토큰 미설정+루프백 → Allowed"),
		Park3DRpcAuth::Authorize(FString(), FString(), Loopback, false, false), EAuthResult::Allowed);

	// 기본 인자가 false 인가 — 인자를 생략한 호출이 종전과 같아야 한다.
	TestEqual(TEXT("기본값 생략 = 비익명"),
		Park3DRpcAuth::Authorize(TEXT("SECRET"), FString(), External, false), EAuthResult::DeniedBadToken);

	return true;
}

// ===== T-U1e: 토큰 문자셋 — 금지 문자는 경로마다 다르게 잘려 영구 401 을 만든다 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcAuthTokenCharsetTest,
	"Park3D.Rpc.Auth.TokenCharset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcAuthTokenCharsetTest::RunTest(const FString& Parameters)
{
	// 금지 — 콤마는 FParse::Value 와 엔진 헤더 파서 양쪽에서 절단되지만 ini 경로는 원문을 보존한다(비대칭).
	TestFalse(TEXT("콤마 거부"),      Park3DRpcAuth::IsTokenCharsetValid(TEXT("ab,cd")));
	TestFalse(TEXT("공백 거부"),      Park3DRpcAuth::IsTokenCharsetValid(TEXT("a b")));
	TestFalse(TEXT("닫는괄호 거부"),  Park3DRpcAuth::IsTokenCharsetValid(TEXT("a)b")));
	TestFalse(TEXT("여는괄호 거부"),  Park3DRpcAuth::IsTokenCharsetValid(TEXT("a(b")));
	TestFalse(TEXT("탭 거부"),        Park3DRpcAuth::IsTokenCharsetValid(TEXT("a\tb")));
	TestFalse(TEXT("CR 거부"),        Park3DRpcAuth::IsTokenCharsetValid(TEXT("a\rb")));
	TestFalse(TEXT("LF 거부"),        Park3DRpcAuth::IsTokenCharsetValid(TEXT("a\nb")));
	TestFalse(TEXT("비ASCII 거부"),   Park3DRpcAuth::IsTokenCharsetValid(TEXT("토큰")));

	// 빈 값은 "미설정"이며 호출 전에 걸러야 한다('+' 규약).
	TestFalse(TEXT("빈 문자열 거부"), Park3DRpcAuth::IsTokenCharsetValid(FString()));

	// T-U1e2: 허용 문자셋
	TestTrue(TEXT("영숫자+하이픈+언더스코어 허용"), Park3DRpcAuth::IsTokenCharsetValid(TEXT("Abc-123_XY")));
	TestTrue(TEXT("단일 문자 허용"),                Park3DRpcAuth::IsTokenCharsetValid(TEXT("a")));

	return true;
}

// ===== T-U1: 토큰 비교 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcAuthTokensMatchTest,
	"Park3D.Rpc.Auth.TokensMatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcAuthTokensMatchTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("정확 일치"), Park3DRpcAuth::TokensMatch(TEXT("abc"), TEXT("abc")));

	// T-U1b: 대소문자 구분(시크릿 엔트로피를 대소문자에서 얻는다)
	TestFalse(TEXT("대소문자 다르면 불일치"), Park3DRpcAuth::TokensMatch(TEXT("Abc"), TEXT("abc")));

	// T-U1c: 양쪽 Trim (엔진은 콤마 분할 후 개별 원소를 Trim 하지 않는다)
	TestTrue(TEXT("앞뒤 공백 무시(제시값)"), Park3DRpcAuth::TokensMatch(TEXT("abc"), TEXT("  abc  ")));
	TestTrue(TEXT("앞뒤 공백 무시(설정값)"), Park3DRpcAuth::TokensMatch(TEXT(" abc "), TEXT("abc")));

	// T-U1d: 빈 문자열은 어느 쪽이든 불일치
	TestFalse(TEXT("설정값 빔"),   Park3DRpcAuth::TokensMatch(FString(), TEXT("abc")));
	TestFalse(TEXT("제시값 빔"),   Park3DRpcAuth::TokensMatch(TEXT("abc"), FString()));
	TestFalse(TEXT("양쪽 빔"),     Park3DRpcAuth::TokensMatch(FString(), FString()));
	TestFalse(TEXT("공백만"),      Park3DRpcAuth::TokensMatch(TEXT("   "), TEXT("   ")));

	// 부분 일치는 불일치(절단 사고가 통과하지 않음을 확인)
	TestFalse(TEXT("절단된 토큰 불일치"), Park3DRpcAuth::TokensMatch(TEXT("ab,cd"), TEXT("ab")));

	return true;
}

// ===== T-U3: 루프백 판정(원시 바이트) =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcAuthLoopbackTest,
	"Park3D.Rpc.Auth.LoopbackRawIp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcAuthLoopbackTest::RunTest(const FString& Parameters)
{
	// T-U3: IPv4 127.0.0.0/8 전체
	TestTrue(TEXT("127.0.0.1"),  Park3DRpcAuth::IsLoopbackRawIp(Bytes({127, 0, 0, 1})));
	TestTrue(TEXT("127.0.0.53"), Park3DRpcAuth::IsLoopbackRawIp(Bytes({127, 0, 0, 53})));
	TestTrue(TEXT("127.1.2.3"),  Park3DRpcAuth::IsLoopbackRawIp(Bytes({127, 1, 2, 3})));

	// T-U3b: IPv6 ::1
	TestTrue(TEXT("::1"), Park3DRpcAuth::IsLoopbackRawIp(IPv6Loopback()));

	// T-U3c: IPv4-mapped 루프백 — 바인드를 any 로 열면 로컬 peer 가 이 형태로 올 수 있다
	TestTrue(TEXT("::ffff:127.0.0.1"),  Park3DRpcAuth::IsLoopbackRawIp(IPv4Mapped(127, 0, 0, 1)));
	TestTrue(TEXT("::ffff:127.0.0.53"), Park3DRpcAuth::IsLoopbackRawIp(IPv4Mapped(127, 0, 0, 53)));

	// T-U3d: 비루프백 / 이상 길이 → 전부 false (fail-closed)
	TestFalse(TEXT("192.168.0.10"), Park3DRpcAuth::IsLoopbackRawIp(Bytes({192, 168, 0, 10})));
	TestFalse(TEXT("10.0.0.5"),     Park3DRpcAuth::IsLoopbackRawIp(Bytes({10, 0, 0, 5})));
	TestFalse(TEXT("0.0.0.0"),      Park3DRpcAuth::IsLoopbackRawIp(Bytes({0, 0, 0, 0})));
	TestFalse(TEXT("128.0.0.1"),    Park3DRpcAuth::IsLoopbackRawIp(Bytes({128, 0, 0, 1})));
	TestFalse(TEXT("126.0.0.1"),    Park3DRpcAuth::IsLoopbackRawIp(Bytes({126, 0, 0, 1})));
	TestFalse(TEXT("빈 배열"),      Park3DRpcAuth::IsLoopbackRawIp(TArray<uint8>()));
	TestFalse(TEXT("길이 5"),       Park3DRpcAuth::IsLoopbackRawIp(Bytes({127, 0, 0, 1, 0})));
	{
		TArray<uint8> Len17; Len17.Init(0, 17); Len17[16] = 1;
		TestFalse(TEXT("길이 17"), Park3DRpcAuth::IsLoopbackRawIp(Len17));
	}

	// T-U3e: IPv4-mapped 비루프백
	TestFalse(TEXT("::ffff:192.168.0.10"), Park3DRpcAuth::IsLoopbackRawIp(IPv4Mapped(192, 168, 0, 10)));

	// IPv6 전역 주소(2001:db8::1) — ::1 판정에 걸리지 않아야 한다
	{
		TArray<uint8> Global; Global.Init(0, 16);
		Global[0] = 0x20; Global[1] = 0x01; Global[2] = 0x0D; Global[3] = 0xB8; Global[15] = 1;
		TestFalse(TEXT("2001:db8::1"), Park3DRpcAuth::IsLoopbackRawIp(Global));
	}

	// 전부 0인 16바이트(::) — [15]!=1 이므로 거부
	{
		TArray<uint8> AllZero; AllZero.Init(0, 16);
		TestFalse(TEXT("::"), Park3DRpcAuth::IsLoopbackRawIp(AllZero));
	}

	return true;
}

// ===== T-U4: 바인드 주소 해석(엔진 파서 복제) =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcAuthResolveBindTest,
	"Park3D.Rpc.Auth.ResolveBindAddress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcAuthResolveBindTest::RunTest(const FString& Parameters)
{
	const FString DefaultBind(TEXT("localhost"));

	// T-U4: 해당 포트 override 존재 → any
	{
		TArray<FString> Ov = { TEXT("(Port=13510,BindAddress=any)") };
		TestEqual(TEXT("포트 일치 → any"), Park3DRpcAuth::ResolveBindAddress(Ov, 13510, DefaultBind), FString(TEXT("any")));
	}

	// 키 순서가 반대여도 파싱된다(FParse::Value 가 콤마에서 멈추므로)
	{
		TArray<FString> Ov = { TEXT("(BindAddress=any,Port=13510)") };
		TestEqual(TEXT("키 역순 파싱"), Park3DRpcAuth::ResolveBindAddress(Ov, 13510, DefaultBind), FString(TEXT("any")));
	}

	// T-U4b: 다른 포트만 있음 → 기본값 유지
	{
		TArray<FString> Ov = { TEXT("(Port=13511,BindAddress=any)") };
		TestEqual(TEXT("포트 불일치 → 기본"), Park3DRpcAuth::ResolveBindAddress(Ov, 13510, DefaultBind), DefaultBind);
	}

	// T-U4c: 빈 배열 → 기본값
	{
		TestEqual(TEXT("빈 배열 → 기본"), Park3DRpcAuth::ResolveBindAddress(TArray<FString>(), 13510, DefaultBind), DefaultBind);
	}

	// T-U4c: Port= 누락 항목은 건너뛰고 다음 항목을 계속 본다
	{
		TArray<FString> Ov = { TEXT("(BindAddress=any)"), TEXT("(Port=13510,BindAddress=0.0.0.0)") };
		TestEqual(TEXT("Port 누락 항목 건너뜀"), Park3DRpcAuth::ResolveBindAddress(Ov, 13510, DefaultBind), FString(TEXT("0.0.0.0")));
	}

	// T-U4c: 동일 포트 2건 → 첫 매칭에서 break
	{
		TArray<FString> Ov = { TEXT("(Port=13510,BindAddress=any)"), TEXT("(Port=13510,BindAddress=127.0.0.1)") };
		TestEqual(TEXT("첫 매칭 채택"), Park3DRpcAuth::ResolveBindAddress(Ov, 13510, DefaultBind), FString(TEXT("any")));
	}

	// 괄호 없는 표기 / 앞뒤 공백도 동일하게 처리
	{
		TArray<FString> Ov = { TEXT("  Port=13510,BindAddress=any  ") };
		TestEqual(TEXT("괄호 없음+공백"), Park3DRpcAuth::ResolveBindAddress(Ov, 13510, DefaultBind), FString(TEXT("any")));
	}

	// 매칭됐지만 BindAddress= 가 없으면 기본값 유지 후 break
	{
		TArray<FString> Ov = { TEXT("(Port=13510,BufferSize=1024)"), TEXT("(Port=13510,BindAddress=any)") };
		TestEqual(TEXT("BindAddress 없는 첫 매칭에서 종료"), Park3DRpcAuth::ResolveBindAddress(Ov, 13510, DefaultBind), DefaultBind);
	}

	return true;
}

// ===== T-U5: Authorize 진리표 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcAuthAuthorizeTest,
	"Park3D.Rpc.Auth.Authorize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcAuthAuthorizeTest::RunTest(const FString& Parameters)
{
	const TArray<uint8> Local    = Bytes({127, 0, 0, 1});
	const TArray<uint8> Remote   = Bytes({192, 168, 0, 10});
	const TArray<uint8> Unknown  = TArray<uint8>();

	auto Check = [&](const TCHAR* What, EAuthResult Actual, EAuthResult Expected)
	{
		TestEqual(What, static_cast<int32>(Actual), static_cast<int32>(Expected));
	};

	// 토큰 설정됨
	Check(TEXT("토큰 일치 + 원격 → 통과"),
		Park3DRpcAuth::Authorize(TEXT("tk"), TEXT("tk"), Remote, false), EAuthResult::Allowed);
	Check(TEXT("토큰 일치 + 로컬 → 통과"),
		Park3DRpcAuth::Authorize(TEXT("tk"), TEXT("tk"), Local, false), EAuthResult::Allowed);
	Check(TEXT("토큰 불일치 + 로컬 → 거부(로컬이어도 검사)"),
		Park3DRpcAuth::Authorize(TEXT("tk"), TEXT("bad"), Local, false), EAuthResult::DeniedBadToken);
	Check(TEXT("토큰 미제출 + 로컬 → 거부"),
		Park3DRpcAuth::Authorize(TEXT("tk"), FString(), Local, false), EAuthResult::DeniedBadToken);

	// 토큰 미설정 → 루프백 폴백
	Check(TEXT("무토큰 + 로컬 IPv4 → 통과"),
		Park3DRpcAuth::Authorize(FString(), FString(), Local, false), EAuthResult::Allowed);
	Check(TEXT("무토큰 + ::1 → 통과"),
		Park3DRpcAuth::Authorize(FString(), FString(), IPv6Loopback(), false), EAuthResult::Allowed);
	Check(TEXT("무토큰 + IPv4-mapped 루프백 → 통과"),
		Park3DRpcAuth::Authorize(FString(), FString(), IPv4Mapped(127, 0, 0, 1), false), EAuthResult::Allowed);
	Check(TEXT("무토큰 + 원격 → 거부"),
		Park3DRpcAuth::Authorize(FString(), FString(), Remote, false), EAuthResult::DeniedNotLoopback);
	Check(TEXT("무토큰 + peer 불명 → 거부(fail-closed)"),
		Park3DRpcAuth::Authorize(FString(), FString(), Unknown, false), EAuthResult::DeniedNotLoopback);

	// Origin 헤더는 최우선 거부(D11)
	Check(TEXT("무토큰 + 로컬 + Origin → 거부"),
		Park3DRpcAuth::Authorize(FString(), FString(), Local, true), EAuthResult::DeniedBrowserOrigin);
	Check(TEXT("토큰 유효 + 로컬 + Origin → 거부(Origin 우선)"),
		Park3DRpcAuth::Authorize(TEXT("tk"), TEXT("tk"), Local, true), EAuthResult::DeniedBrowserOrigin);
	Check(TEXT("토큰 유효 + 원격 + Origin → 거부"),
		Park3DRpcAuth::Authorize(TEXT("tk"), TEXT("tk"), Remote, true), EAuthResult::DeniedBrowserOrigin);

	// 공백만 있는 설정 토큰은 "미설정"으로 취급되어야 한다(영구 401 방지)
	Check(TEXT("공백만 설정 + 로컬 → 무토큰 폴백"),
		Park3DRpcAuth::Authorize(TEXT("   "), FString(), Local, false), EAuthResult::Allowed);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
