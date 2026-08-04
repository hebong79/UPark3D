// Copyright Epic Games, Inc. All Rights Reserved.
// Park3DDataPaths : 참조 데이터(Save/3D/...)의 실제 위치를 실행 형태와 무관하게 해석한다.
//
// 에디터에서는 Save/ 가 프로젝트 폴더 안(ProjectDir()/Save)에 있지만,
// 패키지(스테이징) 산출물에서는 ProjectDir() 이 <Stage>/Park3D/ 이고 Save/ 는 그 한 단계 위인
// 스테이지 루트(<Stage>/Save)에 놓인다. ProjectDir()/Save 만 보면 패키지에서 존재하지 않는 폴더를
// 가리키게 되어, 파일 대화상자가 엉뚱한 위치에서 열리고 사용자가 다른 종류의 JSON 을 고르게 된다.

#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"

namespace Park3DDataPaths
{
	/**
	 * 참조 데이터 루트(Save 폴더)를 반환한다.
	 * 1) ProjectDir()/Save 가 있으면 그것(에디터·정상 스테이징)
	 * 2) 없으면 ProjectDir()/../Save (패키지 스테이지 루트)
	 * 둘 다 없으면 1)을 반환해 기존 동작(쓰기 시 생성)을 유지한다.
	 */
	inline FString GetSaveRootDir()
	{
		IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();

		const FString InProject = FPaths::Combine(FPaths::ProjectDir(), TEXT("Save"));
		if (PF.DirectoryExists(*InProject))
		{
			return InProject;
		}

		const FString StageRoot = FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("Save"));
		if (PF.DirectoryExists(*StageRoot))
		{
			return FPaths::ConvertRelativePathToFull(StageRoot);
		}

		return InProject;
	}

	/** Save/3D/<SubDir>/<FileName> 절대 경로. */
	inline FString GetDataFilePath(const TCHAR* SubDir, const TCHAR* FileName)
	{
		return FPaths::Combine(GetSaveRootDir(), TEXT("3D"), SubDir, FileName);
	}
}
