// Copyright Epic Games, Inc. All Rights Reserved.
// MjpegStreamTest : /stream 순수 로직 검증(HTTP/RHI/월드 비의존).
// 프레이밍·파라미터·토큰 경로·세그먼트 정책만 다루며, 실제 캡처는 실RHI 통합 테스트 영역이다.

#include "Misc/AutomationTest.h"
#include "../Rpc/MjpegStream.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** 파트 바이트를 검사용 문자열로(헤더는 ASCII 범위). */
	FString PartHeaderString(const TArray<uint8>& Part, int32 Len)
	{
		const int32 N = FMath::Min(Len, Part.Num());
		FUTF8ToTCHAR Conv(reinterpret_cast<const ANSICHAR*>(Part.GetData()), N);
		return FString(Conv.Length(), Conv.Get());
	}

	TArray<uint8> FakeJpeg(int32 N)
	{
		TArray<uint8> Out;
		Out.Reserve(N);
		for (int32 i = 0; i < N; ++i)
		{
			Out.Add(static_cast<uint8>(i % 256));
		}
		return Out;
	}
}

// ===== U-1 / U-7: multipart 파트 프레이밍 =====
// 바운더리 표기가 Content-Type 헤더와 어긋나면 브라우저는 아무 프레임도 그리지 않으면서
// HTTP 는 200 으로 성공한다 — 조용한 실패라 반드시 문자열로 못 박는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMjpegFramingTest,
	"Park3D.Rpc.Mjpeg.Framing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMjpegFramingTest::RunTest(const FString& Parameters)
{
	const FString ContentType = Park3DMjpeg::ContentTypeValue();
	TestEqual(TEXT("Content-Type"), ContentType,
		FString(TEXT("multipart/x-mixed-replace; boundary=park3dframe")));

	// Content-Type 의 boundary 와 파트 구분자가 같은 토큰이어야 한다.
	TestTrue(TEXT("Content-Type 에 바운더리 포함"), ContentType.Contains(Park3DMjpeg::BoundaryToken));

	const TArray<uint8> Jpeg = FakeJpeg(1000);
	TArray<uint8> Part;
	Park3DMjpeg::BuildPart(Jpeg, Part);

	const FString Header = PartHeaderString(Part, 96);
	TestTrue(TEXT("선행 CRLF + 바운더리"), Header.StartsWith(TEXT("\r\n--park3dframe\r\n")));
	TestTrue(TEXT("Content-Type: image/jpeg"), Header.Contains(TEXT("Content-Type: image/jpeg\r\n")));
	TestTrue(TEXT("Content-Length 정확"), Header.Contains(TEXT("Content-Length: 1000\r\n")));
	TestTrue(TEXT("헤더 종료 빈 줄"), Header.Contains(TEXT("\r\n\r\n")));

	return true;
}

// ===== U-2: 본문 무손상 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMjpegPayloadIntegrityTest,
	"Park3D.Rpc.Mjpeg.PayloadIntegrity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMjpegPayloadIntegrityTest::RunTest(const FString& Parameters)
{
	const TArray<uint8> Jpeg = FakeJpeg(777);
	TArray<uint8> Part;
	Park3DMjpeg::BuildPart(Jpeg, Part);

	// 파트 = 헤더 + 본문. 본문은 항상 파트의 마지막 777 바이트다.
	TestTrue(TEXT("파트가 본문보다 큼"), Part.Num() > Jpeg.Num());

	const int32 Offset = Part.Num() - Jpeg.Num();
	bool bSame = true;
	for (int32 i = 0; i < Jpeg.Num(); ++i)
	{
		if (Part[Offset + i] != Jpeg[i]) { bSame = false; break; }
	}
	TestTrue(TEXT("본문 바이트 무손상"), bSame);

	// 빈 프레임도 크래시 없이 유효한 파트를 만든다(인코딩 실패 경로 방어).
	TArray<uint8> EmptyPart;
	Park3DMjpeg::BuildPart(TArray<uint8>(), EmptyPart);
	TestTrue(TEXT("빈 본문도 헤더 생성"), PartHeaderString(EmptyPart, 96).Contains(TEXT("Content-Length: 0")));

	return true;
}

// ===== U-3 / U-4: 파라미터 파싱과 clamp =====
// 뷰어 URL 오타(fps=0 등)로 스트림이 죽거나 초당 수천 프레임을 시도하지 않게 한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMjpegParamsTest,
	"Park3D.Rpc.Mjpeg.Params",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMjpegParamsTest::RunTest(const FString& Parameters)
{
	using Park3DMjpeg::FStreamParams;

	{	// 기본값 — 빈 쿼리
		const FStreamParams P = Park3DMjpeg::ParseParams(TMap<FString, FString>());
		TestEqual(TEXT("기본 camId"), P.CamId, 0);
		// 기본 5fps 는 게임 스레드 비용 계측(1프레임 ≈ 48ms)에서 나온 값이다. 무심코 올리면 앱 틱이 반토막 난다.
		TestEqual(TEXT("기본 fps"), P.Fps, 5.f);
		TestEqual(TEXT("기본 quality"), P.Quality, 70);
		TestEqual(TEXT("기본 maxSec"), P.MaxSec, 0);
	}

	{	// 정상 지정
		TMap<FString, FString> Q;
		Q.Add(TEXT("camId"), TEXT("2"));
		Q.Add(TEXT("fps"), TEXT("15"));
		Q.Add(TEXT("quality"), TEXT("50"));
		Q.Add(TEXT("maxSec"), TEXT("120"));
		const FStreamParams P = Park3DMjpeg::ParseParams(Q);
		TestEqual(TEXT("camId"), P.CamId, 2);
		TestEqual(TEXT("fps"), P.Fps, 15.f);
		TestEqual(TEXT("quality"), P.Quality, 50);
		TestEqual(TEXT("maxSec"), P.MaxSec, 120);
	}

	{	// 하한 clamp
		TMap<FString, FString> Q;
		Q.Add(TEXT("camId"), TEXT("-5"));
		Q.Add(TEXT("fps"), TEXT("0"));
		Q.Add(TEXT("quality"), TEXT("0"));
		Q.Add(TEXT("maxSec"), TEXT("-1"));
		const FStreamParams P = Park3DMjpeg::ParseParams(Q);
		TestEqual(TEXT("camId 하한"), P.CamId, 0);
		TestEqual(TEXT("fps 하한"), P.Fps, 1.f);
		TestEqual(TEXT("quality 하한"), P.Quality, 1);
		TestEqual(TEXT("maxSec 하한"), P.MaxSec, 0);
	}

	{	// 상한 clamp
		TMap<FString, FString> Q;
		Q.Add(TEXT("fps"), TEXT("999"));
		Q.Add(TEXT("quality"), TEXT("200"));
		Q.Add(TEXT("maxSec"), TEXT("99999"));
		const FStreamParams P = Park3DMjpeg::ParseParams(Q);
		TestEqual(TEXT("fps 상한"), P.Fps, 30.f);
		TestEqual(TEXT("quality 상한"), P.Quality, 100);
		TestEqual(TEXT("maxSec 상한"), P.MaxSec, 3600);
	}

	{	// 숫자가 아니면 기본값으로 폴백(요청을 죽이지 않는다)
		TMap<FString, FString> Q;
		Q.Add(TEXT("fps"), TEXT("abc"));
		Q.Add(TEXT("quality"), TEXT(""));
		const FStreamParams P = Park3DMjpeg::ParseParams(Q);
		TestEqual(TEXT("fps 폴백"), P.Fps, 5.f);
		TestEqual(TEXT("quality 폴백"), P.Quality, 70);
	}

	return true;
}

// ===== U-5: 토큰 운반 경로 =====
// 헤더가 항상 우선이어야 한다. 쿼리가 헤더를 덮으면 URL 로 인증을 우회할 여지가 생긴다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMjpegTokenPickTest,
	"Park3D.Rpc.Mjpeg.TokenPick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMjpegTokenPickTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("헤더 우선"),
		Park3DMjpeg::PickToken(TEXT("HEADERTOK"), TEXT("QUERYTOK")), FString(TEXT("HEADERTOK")));
	TestEqual(TEXT("헤더 없으면 쿼리"),
		Park3DMjpeg::PickToken(FString(), TEXT("QUERYTOK")), FString(TEXT("QUERYTOK")));
	TestEqual(TEXT("둘 다 없으면 빈 문자열"),
		Park3DMjpeg::PickToken(FString(), FString()), FString());
	TestEqual(TEXT("쿼리 없으면 헤더"),
		Park3DMjpeg::PickToken(TEXT("HEADERTOK"), FString()), FString(TEXT("HEADERTOK")));

	return true;
}

// ===== U-6: 세그먼트 롤오버 정책 =====
// 롤오버가 걸리지 않으면 엔진이 보낸 바이트를 Response->Body 에서 지우지 않아 메모리가 단조 증가한다
// (HttpConnectionResponseWriteContext.cpp:78,86). 이 판정이 유일한 상한이다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMjpegSegmentPolicyTest,
	"Park3D.Rpc.Mjpeg.SegmentPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMjpegSegmentPolicyTest::RunTest(const FString& Parameters)
{
	Park3DMjpeg::FSegmentPolicy Policy;
	Policy.MaxBytes = 1000;
	Policy.MaxFrames = 10;

	TestFalse(TEXT("둘 다 미달"), Policy.ShouldRollover(999, 9));
	TestTrue(TEXT("바이트 경계"), Policy.ShouldRollover(1000, 0));
	TestTrue(TEXT("프레임 경계"), Policy.ShouldRollover(0, 10));
	TestTrue(TEXT("둘 다 초과"), Policy.ShouldRollover(5000, 50));
	TestFalse(TEXT("0/0"), Policy.ShouldRollover(0, 0));

	// 기본값이 "무제한"으로 잘못 설정되면 메모리 상한이 사라진다 — 회귀 방지.
	const Park3DMjpeg::FSegmentPolicy Default;
	TestTrue(TEXT("기본 MaxBytes 유한"), Default.MaxBytes > 0 && Default.MaxBytes <= 16 * 1024 * 1024);
	TestTrue(TEXT("기본 MaxFrames 유한"), Default.MaxFrames > 0 && Default.MaxFrames <= 600);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
