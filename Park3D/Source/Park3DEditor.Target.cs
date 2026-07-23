// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class Park3DEditorTarget : TargetRules
{
	public Park3DEditorTarget( TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
		// Park3D Editor는 VS2022 17.14.36의 MSVC 14.44.35207을 사용한다.
		WindowsPlatform.Compiler = WindowsCompiler.VisualStudio2022;
		WindowsPlatform.CompilerVersion = "14.44.35207";
		// UE 5.8 기본 빌드 설정(V7)으로 업그레이드. 설치형 엔진의 공유 빌드 환경과
		// 프로젝트 설정 차이로 빌드가 거부되는 경우를 대비해 공유환경 덮어쓰기를 허용한다. (무해)
		bOverrideBuildEnvironment = true;
		ExtraModuleNames.Add("Park3D");
	}
}
