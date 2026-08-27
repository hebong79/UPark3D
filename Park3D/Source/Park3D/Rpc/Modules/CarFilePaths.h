// Copyright Epic Games, Inc. All Rights Reserved.
// CarFilePaths : car.* 파일 RPC 가 **지워도 되는 자리**를 정하는 순수 규칙.
//
// car.save / car.load 는 fullPath 를 주면 그대로 쓴다(요청자가 쓰기·읽기를 책임진다). 그러나
// 삭제는 대칭이 아니다 — RPC 서버는 AllowAnonymous 라(Docs/20260804_185500) 경로를 그대로 믿으면
// 네트워크에 닿는 누구나 Config·Content·Saved 아래 아무 파일이나 지울 수 있다. 그래서 삭제만은
// **폴더와 확장자로 가둔다.**
//
// 규칙을 헤더의 순수 함수로 빼 둔 이유는 자동화 테스트가 실제 프로젝트 폴더 없이도(가짜 루트를
// 주입해) 이 판정을 시험할 수 있어야 하기 때문이다 — Tests/CarFilePathsTest.cpp.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Paths.h"
#include "../../Park3DDataPaths.h"

namespace CarFilePaths
{
	/**
	 * 비교에 쓸 한 가지 모양으로 고친다: 슬래시 통일 → 절대 경로 → `..` 접기 → 중복 슬래시 제거.
	 *
	 * ⚠ 접기를 **비교 전에** 해야 한다. `.../CarData/../../Config/x.json` 은 접기 전에는
	 * 허용 폴더로 시작하는 문자열이라 그냥 StartsWith 로는 통과해 버린다.
	 */
	inline FString Normalize(const FString& InPath)
	{
		FString Path = InPath;
		FPaths::NormalizeFilename(Path);                 // \ → /
		Path = FPaths::ConvertRelativePathToFull(Path);  // 상대 경로는 프로세스 기준 절대 경로로
		FPaths::CollapseRelativeDirectories(Path);       // `..` 을 실제로 접는다
		FPaths::RemoveDuplicateSlashes(Path);
		while (Path.EndsWith(TEXT("/")))
		{
			Path.LeftChopInline(1);
		}
		return Path;
	}

	/**
	 * 삭제가 허용되는 폴더.
	 *
	 * - `Saved/CarData` — `car.save` 가 fileName 만 받았을 때 쓰는 기본 자리(ResolveCarPath).
	 *   TourAgent 의 `touragent-scene-*.json` 이 쌓이는 곳이 여기다.
	 * - `Save/3D/CarPos` — 시뮬 UI 가 배치를 저장하는 자리(SettingMain 이 사본을 갖고 있다).
	 */
	inline TArray<FString> DefaultRoots()
	{
		TArray<FString> Roots;
		Roots.Add(Normalize(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CarData"))));
		Roots.AddUnique(Normalize(FPaths::GetPath(Park3DDataPaths::GetDataFilePath(TEXT("CarPos"), TEXT("x.json")))));
		return Roots;
	}

	/** 정규화된 경로가 정규화된 폴더 **안**인가. 폴더 자기 자신은 파일이 아니므로 거짓이다. */
	inline bool IsInside(const FString& NormalizedPath, const FString& NormalizedRoot)
	{
		// 대소문자 무시 — 윈도우 파일시스템이 그렇게 다루고, 이 서버는 거기서 돈다.
		return NormalizedPath.StartsWith(NormalizedRoot + TEXT("/"), ESearchCase::IgnoreCase);
	}

	/**
	 * 지워도 되는 파일인가.
	 *
	 * @param OutPath    통과했을 때 실제로 지울 정규화 경로(실패해도 채워 둔다 — 사유에 싣는다).
	 * @param OutReason  거절 사유. **문장 그대로 RPC 응답에 실린다** — 「안 됩니다」만 돌려주면
	 *                   부른 쪽은 경로가 틀렸는지 폴더가 틀렸는지 알 수 없다.
	 */
	inline bool CanDelete(const FString& InPath, const TArray<FString>& Roots, FString& OutPath, FString& OutReason)
	{
		OutPath = Normalize(InPath);
		OutReason.Reset();

		if (!OutPath.EndsWith(TEXT(".json"), ESearchCase::IgnoreCase))
		{
			OutReason = FString::Printf(TEXT("배치 파일이 아닙니다(.json 만 지울 수 있습니다): %s"), *OutPath);
			return false;
		}
		for (const FString& Root : Roots)
		{
			if (IsInside(OutPath, Root))
			{
				return true;
			}
		}
		OutReason = FString::Printf(TEXT("배치 파일 폴더 밖입니다: %s (허용: %s)"),
			*OutPath, *FString::Join(Roots, TEXT(" · ")));
		return false;
	}
}
