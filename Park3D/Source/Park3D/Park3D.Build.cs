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
		// Networking: FTcpListener(카메라별 전용 포트 MJPEG 서버). Sockets 만으로는 리스너가 없다.
		// RHI/RenderCore: CamStreamSubsystem 의 메인 뷰 비동기 리드백이 FRHIGPUTextureReadback,
		//                 FRHICommandListImmediate, ENQUEUE_RENDER_COMMAND 를 직접 쓴다. 모놀리식
		//                 게임 빌드는 한 실행 파일로 링크돼 없어도 넘어가지만, 모듈러 에디터 빌드
		//                 (UAT 쿠킹이 요구한다)는 여기서 선언하지 않으면 LNK2019 로 죽는다.
		// Text3D: 번호판 번호를 압출 지오메트리(양각)로 만든다. 런타임 모듈이며 폰트 아웃라인을
		//         읽어 글자 메시를 굽고 (Font+GlyphIndex+압출값) 단위로 엔진 전역 캐시에 공유한다.
		PrivateDependencyModuleNames.AddRange(new string[] { "HTTPServer", "ImageWrapper", "Sockets", "Networking", "RHI", "RenderCore", "Text3D" });

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
