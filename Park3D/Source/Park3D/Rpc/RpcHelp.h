// Copyright Epic Games, Inc. All Rights Reserved.
// RpcHelp : GET /help 문서(보드 #1156). method 목록은 디스패처 레지스트리(system.catalog 와 같은 것)에서 오고,
// 사람이 쓴 설명·파라미터·예시는 RpcHelpTable.cpp 의 표에서 온다. 둘이 어긋나면 자동화 테스트가 잡는다
// (Park3D.Rpc.Help.TableMatchesRegistry) — 새 method 를 등록하면 표에도 한 줄 넣을 것.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

class URpcDispatcher;

namespace Park3DRpcHelp
{
	/** 표 한 줄. Params 는 JSON 배열 [{name,type,required,default,unit,desc}], Example 은 params JSON 객체. */
	struct FMethodDoc
	{
		const TCHAR* Method;
		const TCHAR* Title;
		const TCHAR* Params;
		const TCHAR* Returns;
		bool bMovesCamera;
		bool bDestructive;
		bool bImplemented;
		const TCHAR* Reason;
		const TCHAR* Notes;
		const TCHAR* Example;
	};

	/** RpcHelpTable.cpp 의 표 전체. */
	PARK3D_API TConstArrayView<FMethodDoc> GetTable();
	PARK3D_API const FMethodDoc* FindDoc(const FString& Method);

	/** "이 인스턴스 지금" — 요청마다 새로 채운다(RpcServerSubsystem::BuildHelpContext). */
	struct FLiveContext
	{
		FString BaseUrl;          // http://<Host 헤더> — 예시 curl 이 그대로 돌도록
		int32 RpcPort = 0;
		FString Auth;             // anonymous | token | loopback-only
		FString ExeBuilt;         // 실행 파일 수정 시각(ISO8601) — 배포본이 언제 빌드됐나
		FString Level;            // 레벨 경로
		FString LevelName;        // config levels[] 의 장소 이름(없으면 빈 값)
		int32 MainViewPort = 0, CamPortMin = 0, CamPortMax = 0;
		int32 Cars = -1, VisibleCars = -1, Cameras = -1, Presets = -1;
		/** "<carNameId>" → 실제 값. 표의 예시에서 같은 문자열 값을 이것으로 바꾼다. */
		TMap<FString, TSharedPtr<FJsonValue>> Placeholders;
	};

	/** 표 버전 — 표의 형식이나 의미가 바뀌면 올린다. */
	PARK3D_API const TCHAR* SchemaVersion();

	/** 도메인 = 첫 '.' 앞(car.list → car). */
	PARK3D_API FString GroupOf(const FString& Method);

	/** 예시 params 에 자리표시자를 실제 값으로 채운다. 못 채운 것은 OutFill 에. */
	PARK3D_API TSharedPtr<FJsonObject> FillExample(const FMethodDoc* Doc, const FLiveContext& Ctx, TArray<FString>& OutFill);

	/** methods[] 의 한 원소 {method,title,group,params[],returns,mutating,movesCamera,destructive,implemented,reason?,notes,documented,example,fill}. */
	PARK3D_API TSharedPtr<FJsonObject> MethodJson(const URpcDispatcher& D, const FString& Method, const FLiveContext& Ctx);

	/** GET /help?format=json 본문. */
	PARK3D_API TSharedPtr<FJsonObject> IndexJson(const URpcDispatcher& D, const FLiveContext& Ctx);

	/** GET /help (markdown). */
	PARK3D_API FString IndexMarkdown(const URpcDispatcher& D, const FLiveContext& Ctx);

	/** GET /help/rpc[?group=&q=] (markdown 표). */
	PARK3D_API FString RpcListMarkdown(const URpcDispatcher& D, const FLiveContext& Ctx, const FString& Group, const FString& Query);

	/** GET /help/rpc/<method> (markdown). 미등록이면 빈 문자열. */
	PARK3D_API FString MethodMarkdown(const URpcDispatcher& D, const FString& Method, const FLiveContext& Ctx);

	/** GET /help/units (markdown) — 단위·축 규약. */
	PARK3D_API FString UnitsMarkdown();

	/** 레지스트리에 있는데 표에 없는 method / 표에 있는데 등록 안 된 method(테스트·로그용). */
	PARK3D_API TArray<FString> UndocumentedMethods(const URpcDispatcher& D);
	PARK3D_API TArray<FString> StaleDocs(const URpcDispatcher& D);

	/** curl 한 줄. 비ASCII 는 \uXXXX 로 — Windows 셸이 한글 인자를 깨뜨리지 않게. */
	PARK3D_API FString CurlFor(const FLiveContext& Ctx, const FString& Method, const TSharedPtr<FJsonObject>& Params);
}
