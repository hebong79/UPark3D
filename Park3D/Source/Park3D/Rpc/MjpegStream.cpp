// Copyright Epic Games, Inc. All Rights Reserved.

#include "MjpegStream.h"

namespace Park3DMjpeg
{
	const TCHAR* const BoundaryToken = TEXT("park3dframe");

	FString ContentTypeValue()
	{
		return FString::Printf(TEXT("multipart/x-mixed-replace; boundary=%s"), BoundaryToken);
	}

	void BuildPart(const TArray<uint8>& Jpeg, TArray<uint8>& OutPart)
	{
		OutPart.Empty(Jpeg.Num() + 128);

		const FString Header = FString::Printf(
			TEXT("\r\n--%s\r\nContent-Type: image/jpeg\r\nContent-Length: %d\r\n\r\n"),
			BoundaryToken, Jpeg.Num());

		// 헤더는 ASCII 범위지만 바이트 변환은 UTF-8 로 통일한다(엔진 헤더 직렬화와 동일 규약).
		FTCHARToUTF8 Conv(*Header);
		OutPart.Append(reinterpret_cast<const uint8*>(Conv.Get()), Conv.Length());
		OutPart.Append(Jpeg);
	}

	namespace
	{
		/** 키가 있으면 정수로, 없거나 파싱 실패면 Default. */
		int32 QueryInt(const TMap<FString, FString>& Query, const TCHAR* Key, int32 Default)
		{
			if (const FString* Found = Query.Find(Key))
			{
				const FString Trimmed = Found->TrimStartAndEnd();
				if (Trimmed.IsNumeric())
				{
					return FCString::Atoi(*Trimmed);
				}
			}
			return Default;
		}

		/** 키가 있으면 실수로, 없거나 파싱 실패면 Default. */
		float QueryFloat(const TMap<FString, FString>& Query, const TCHAR* Key, float Default)
		{
			if (const FString* Found = Query.Find(Key))
			{
				const FString Trimmed = Found->TrimStartAndEnd();
				if (Trimmed.IsNumeric())
				{
					return FCString::Atof(*Trimmed);
				}
			}
			return Default;
		}
	}

	FStreamParams ParseParams(const TMap<FString, FString>& Query)
	{
		FStreamParams P;
		P.CamId   = FMath::Max(0, QueryInt(Query, TEXT("camId"), P.CamId));
		P.Fps     = FMath::Clamp(QueryFloat(Query, TEXT("fps"), P.Fps), 1.f, 30.f);
		P.Quality = FMath::Clamp(QueryInt(Query, TEXT("quality"), P.Quality), 1, 100);
		P.MaxSec  = FMath::Clamp(QueryInt(Query, TEXT("maxSec"), P.MaxSec), 0, 3600);
		return P;
	}

	FString PickToken(const FString& HeaderToken, const FString& QueryToken)
	{
		return HeaderToken.IsEmpty() ? QueryToken : HeaderToken;
	}

	bool FSegmentPolicy::ShouldRollover(int32 Bytes, int32 Frames) const
	{
		return Bytes >= MaxBytes || Frames >= MaxFrames;
	}
}
