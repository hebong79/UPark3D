// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Park3D : ModuleRules
{
	public Park3D(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "UMG", "Slate", "SlateCore", "Json", "JsonUtilities" });

		// RPC 서버(JSON-RPC 2.0 over HTTP). Unity 99_Network/NetworkRpc 포팅.
		// HTTPServer: 내장 HTTP 리스너/라우터(포트 13510). 요청 콜백은 게임 스레드에서 처리됨.
		// ImageWrapper: cam.captureJPG/PNG 렌더타깃 픽셀 → JPEG/PNG 인코딩(Phase 5).
		// Sockets: FInternetAddr 완전 정의(PeerAddress->GetRawIp() 역참조). HttpServerRequest.h 는 전방 선언만 갖고,
		//          HTTPServer 가 Sockets 를 Private 의존으로 가져 소비자에게 전파되지 않으므로 여기서 직접 의존해야 한다.
		PrivateDependencyModuleNames.AddRange(new string[] { "HTTPServer", "ImageWrapper", "Sockets" });

		PrivateDependencyModuleNames.AddRange(new string[] {  });

		// 파일 열기/저장 네이티브 대화상자(DesktopPlatform)는 Shipping에서 제외되는 개발자 모듈이다.
		// 에디터·Development 빌드에서만 포함하고, 그 외에는 기본 경로로 폴백한다.
		if (Target.Configuration != UnrealTargetConfiguration.Shipping)
		{
			PrivateDependencyModuleNames.Add("DesktopPlatform");
			PublicDefinitions.Add("PARK3D_USE_FILE_DIALOG=1");
		}
		else
		{
			PublicDefinitions.Add("PARK3D_USE_FILE_DIALOG=0");
		}

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
