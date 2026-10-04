// Copyright Epic Games, Inc. All Rights Reserved.

#include "RpcHelp.h"
#include "RpcDispatcher.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/CondensedJsonPrintPolicy.h"

namespace Park3DRpcHelp
{
namespace
{
	FString Compact(const TSharedPtr<FJsonObject>& O)
	{
		FString Out;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(O.ToSharedRef(), W);
		return Out;
	}

	FString CompactValue(const TSharedPtr<FJsonValue>& V)
	{
		if (!V.IsValid() || V->Type == EJson::Null) return TEXT("null");
		FString Out;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(V, FString(), W);
		return Out;
	}

	TArray<TSharedPtr<FJsonValue>> ParseArray(const TCHAR* Json)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		if (Json && *Json)
		{
			TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Json);
			FJsonSerializer::Deserialize(R, Arr);
		}
		return Arr;
	}

	TSharedPtr<FJsonObject> ParseObject(const TCHAR* Json)
	{
		TSharedPtr<FJsonObject> O;
		if (Json && *Json)
		{
			TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Json);
			FJsonSerializer::Deserialize(R, O);
		}
		return O.IsValid() ? O : MakeShared<FJsonObject>();
	}

	/** markdown 표 칸 — 줄바꿈·파이프를 칸을 깨지 않는 형태로. */
	FString Cell(const FString& S)
	{
		return S.Replace(TEXT("|"), TEXT("\\|")).Replace(TEXT("\r"), TEXT("")).Replace(TEXT("\n"), TEXT(" "));
	}

	/** 비ASCII → \uXXXX(서로게이트 쌍 포함). JSON 문자열 안에서만 쓰므로 그대로 유효한 JSON 이다. */
	FString EscapeNonAscii(const FString& S)
	{
		FString Out;
		Out.Reserve(S.Len());
		for (TCHAR C : S)
		{
			if (static_cast<uint32>(C) < 0x80) { Out.AppendChar(C); }
			else { Out += FString::Printf(TEXT("\\u%04x"), static_cast<uint32>(C) & 0xFFFF); }
		}
		return Out;
	}

	void ReplacePlaceholders(TSharedPtr<FJsonValue>& V, const FLiveContext& Ctx, TSet<FString>& Unfilled)
	{
		if (!V.IsValid()) return;
		if (V->Type == EJson::String)
		{
			const FString S = V->AsString();
			if (S.StartsWith(TEXT("<")) && S.EndsWith(TEXT(">")))
			{
				if (const TSharedPtr<FJsonValue>* Live = Ctx.Placeholders.Find(S)) { V = *Live; }
				else { Unfilled.Add(S); }
			}
		}
		else if (V->Type == EJson::Object)
		{
			TSharedPtr<FJsonObject> O = V->AsObject();
			for (auto& KV : O->Values) { ReplacePlaceholders(KV.Value, Ctx, Unfilled); }
		}
		else if (V->Type == EJson::Array)
		{
			TArray<TSharedPtr<FJsonValue>> Arr = V->AsArray();
			for (TSharedPtr<FJsonValue>& E : Arr) { ReplacePlaceholders(E, Ctx, Unfilled); }
			V = MakeShared<FJsonValueArray>(Arr);
		}
	}

	FString FlagsText(bool bMutating, bool bMoves, bool bDestructive, bool bImplemented)
	{
		TArray<FString> F;
		F.Add(bMutating ? TEXT("mutating") : TEXT("read-only"));
		if (bMoves) F.Add(TEXT("movesCamera"));
		if (bDestructive) F.Add(TEXT("destructive"));
		if (!bImplemented) F.Add(TEXT("NOT IMPLEMENTED"));
		return FString::Join(F, TEXT(" · "));
	}

	/** 그룹 → 정렬된 method 목록. */
	TMap<FString, TArray<FString>> Groups(const URpcDispatcher& D)
	{
		TMap<FString, TArray<FString>> G;
		for (const FString& M : D.GetMethods()) { G.FindOrAdd(GroupOf(M)).Add(M); }
		G.KeySort(TLess<FString>());
		return G;
	}

	struct FErrorRow { int32 Code; const TCHAR* Meaning; };
	const FErrorRow GErrors[] = {
		{ -32700, TEXT("body is not JSON") },
		{ -32601, TEXT("unknown method, or `method` missing") },
		{ -32602, TEXT("bad params — used by newer methods (unknown key / bad enum value, the message names the key)") },
		{ -32000, TEXT("domain error — the handler refused. error.data.kind says why: bad_params · not_found · busy · internal. error.message is Korean, quote it verbatim") },
		{ -32001, TEXT("unauthorized (HTTP 401) — X-Park3D-Token header missing or wrong") },
		{ -32004, TEXT("registered but not supported by this backend (data.kind = unsupported)") },
	};
}

const TCHAR* SchemaVersion() { return TEXT("park3d-help/1"); }

const FMethodDoc* FindDoc(const FString& Method)
{
	for (const FMethodDoc& Doc : GetTable())
	{
		if (Method.Equals(Doc.Method, ESearchCase::CaseSensitive)) return &Doc;
	}
	return nullptr;
}

FString GroupOf(const FString& Method)
{
	FString L, R;
	return Method.Split(TEXT("."), &L, &R) ? L : Method;
}

TSharedPtr<FJsonObject> FillExample(const FMethodDoc* Doc, const FLiveContext& Ctx, TArray<FString>& OutFill)
{
	TSharedPtr<FJsonValue> V = MakeShared<FJsonValueObject>(ParseObject(Doc ? Doc->Example : nullptr));
	TSet<FString> Unfilled;
	ReplacePlaceholders(V, Ctx, Unfilled);
	OutFill = Unfilled.Array();
	OutFill.Sort();
	return V->AsObject();
}

FString CurlFor(const FLiveContext& Ctx, const FString& Method, const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Req = MakeShared<FJsonObject>();
	Req->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	Req->SetNumberField(TEXT("id"), 1);
	Req->SetStringField(TEXT("method"), Method);
	Req->SetObjectField(TEXT("params"), Params.IsValid() ? Params : MakeShared<FJsonObject>());
	const FString Body = EscapeNonAscii(Compact(Req)).Replace(TEXT("'"), TEXT("'\\''"));
	const FString TokenHeader = Ctx.Auth == TEXT("token") ? TEXT(" -H 'X-Park3D-Token: <token>'") : TEXT("");
	return FString::Printf(TEXT("curl -s %s/rpc -H 'content-type: application/json'%s -d '%s'"), *Ctx.BaseUrl, *TokenHeader, *Body);
}

TSharedPtr<FJsonObject> MethodJson(const URpcDispatcher& D, const FString& Method, const FLiveContext& Ctx)
{
	const FMethodDoc* Doc = FindDoc(Method);
	const FRpcMethodMeta* Meta = D.FindMethodMeta(Method);

	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("method"), Method);
	O->SetStringField(TEXT("group"), GroupOf(Method));
	O->SetStringField(TEXT("title"), Doc ? Doc->Title : TEXT(""));
	O->SetArrayField(TEXT("params"), ParseArray(Doc ? Doc->Params : nullptr));
	O->SetStringField(TEXT("returns"), Doc ? Doc->Returns : TEXT(""));
	// mutating 은 레지스트리 판정 그대로 — requestId 중복 제거·revision 이 따르는 값과 같아야 한다.
	O->SetBoolField(TEXT("mutating"), D.IsMutating(Method));
	O->SetBoolField(TEXT("movesCamera"), Doc ? Doc->bMovesCamera : false);
	O->SetBoolField(TEXT("destructive"), (Doc && Doc->bDestructive) || (Meta && Meta->bDestructive));
	O->SetBoolField(TEXT("implemented"), Doc ? Doc->bImplemented : true);
	if (Doc && !Doc->bImplemented) { O->SetStringField(TEXT("reason"), Doc->Reason ? Doc->Reason : TEXT("")); }
	O->SetStringField(TEXT("notes"), Doc ? Doc->Notes : TEXT(""));
	if (Meta && !Meta->Doc.IsEmpty()) { O->SetStringField(TEXT("docKo"), Meta->Doc); }   // 등록부의 한국어 설명(있을 때만)
	O->SetBoolField(TEXT("persistent"), D.IsPersistent(Method));
	O->SetBoolField(TEXT("documented"), Doc != nullptr);

	TArray<FString> Fill;
	TSharedPtr<FJsonObject> Params = FillExample(Doc, Ctx, Fill);
	TSharedPtr<FJsonObject> Ex = MakeShared<FJsonObject>();
	Ex->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	Ex->SetNumberField(TEXT("id"), 1);
	Ex->SetStringField(TEXT("method"), Method);
	Ex->SetObjectField(TEXT("params"), Params);
	O->SetObjectField(TEXT("example"), Ex);
	TArray<TSharedPtr<FJsonValue>> FillArr;
	for (const FString& F : Fill) FillArr.Add(MakeShared<FJsonValueString>(F));
	O->SetArrayField(TEXT("fill"), FillArr);
	O->SetStringField(TEXT("curl"), CurlFor(Ctx, Method, Params));
	O->SetStringField(TEXT("help"), FString::Printf(TEXT("%s/help/rpc/%s"), *Ctx.BaseUrl, *Method));
	return O;
}

namespace
{
	TSharedPtr<FJsonObject> StateJson(const FLiveContext& Ctx, int32 NumMethods)
	{
		TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
		S->SetNumberField(TEXT("rpcPort"), Ctx.RpcPort);
		S->SetStringField(TEXT("auth"), Ctx.Auth);
		S->SetStringField(TEXT("exeBuilt"), Ctx.ExeBuilt);
		S->SetStringField(TEXT("level"), Ctx.Level);
		S->SetStringField(TEXT("levelName"), Ctx.LevelName);
		S->SetNumberField(TEXT("mainViewPort"), Ctx.MainViewPort);
		S->SetNumberField(TEXT("camPortMin"), Ctx.CamPortMin);
		S->SetNumberField(TEXT("camPortMax"), Ctx.CamPortMax);
		S->SetStringField(TEXT("camPortFormula"), TEXT("camPortMin + (camId - 1)"));
		S->SetNumberField(TEXT("cars"), Ctx.Cars);
		S->SetNumberField(TEXT("visibleCars"), Ctx.VisibleCars);
		S->SetNumberField(TEXT("cameras"), Ctx.Cameras);
		S->SetNumberField(TEXT("presets"), Ctx.Presets);
		S->SetNumberField(TEXT("rpcMethods"), NumMethods);
		return S;
	}
}

TSharedPtr<FJsonObject> IndexJson(const URpcDispatcher& D, const FLiveContext& Ctx)
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("name"), TEXT("park3d-unreal"));
	O->SetStringField(TEXT("version"), FString::Printf(TEXT("%s · exe %s · %d methods"), SchemaVersion(), *Ctx.ExeBuilt, D.NumMethods()));
	O->SetStringField(TEXT("schema"), SchemaVersion());
	O->SetStringField(TEXT("summary"), TEXT("Unreal Engine 5 parking-lot simulator (Park3D) — PTZ cameras, cars, parking presets/bays, lighting, parking simulation, MJPEG streams. Driven over JSON-RPC 2.0."));
	O->SetStringField(TEXT("prose"), Ctx.BaseUrl + TEXT("/help"));
	O->SetStringField(TEXT("units"), Ctx.BaseUrl + TEXT("/help/units"));

	TSharedPtr<FJsonObject> Rpc = MakeShared<FJsonObject>();
	Rpc->SetStringField(TEXT("endpoint"), FString::Printf(TEXT("POST %s/rpc"), *Ctx.BaseUrl));
	TSharedPtr<FJsonObject> Env = MakeShared<FJsonObject>();
	Env->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	Env->SetNumberField(TEXT("id"), 1);
	Env->SetStringField(TEXT("method"), TEXT("system.health"));
	Env->SetObjectField(TEXT("params"), MakeShared<FJsonObject>());
	Rpc->SetObjectField(TEXT("envelope"), Env);
	Rpc->SetStringField(TEXT("catalogMethod"), TEXT("system.catalog"));
	Rpc->SetStringField(TEXT("auth"), Ctx.Auth);
	TArray<TSharedPtr<FJsonValue>> Errs;
	for (const FErrorRow& E : GErrors)
	{
		TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetNumberField(TEXT("code"), E.Code);
		R->SetStringField(TEXT("meaning"), E.Meaning);
		Errs.Add(MakeShared<FJsonValueObject>(R));
	}
	Rpc->SetArrayField(TEXT("errors"), Errs);
	O->SetObjectField(TEXT("rpc"), Rpc);
	O->SetObjectField(TEXT("state"), StateJson(Ctx, D.NumMethods()));

	TArray<TSharedPtr<FJsonValue>> Methods;
	for (const FString& M : D.GetMethods()) { Methods.Add(MakeShared<FJsonValueObject>(MethodJson(D, M, Ctx))); }
	O->SetArrayField(TEXT("methods"), Methods);
	TArray<TSharedPtr<FJsonValue>> Undoc;
	for (const FString& M : UndocumentedMethods(D)) Undoc.Add(MakeShared<FJsonValueString>(M));
	O->SetArrayField(TEXT("undocumented"), Undoc);
	return O;
}

FString IndexMarkdown(const URpcDispatcher& D, const FLiveContext& Ctx)
{
	const FString& B = Ctx.BaseUrl;
	FString S;
	S += TEXT("# Park3D (Unreal) — parking-lot simulator\n\n");
	S += TEXT("An Unreal Engine 5 simulator of real parking lots (서신지구대, 객리단길, …) that stands in for the field: PTZ cameras with ")
		 TEXT("MJPEG streams, parked cars with Korean plates, parking-slot presets and level bays, lighting, and a parking/exit ")
		 TEXT("simulation. SettingManager, TourAgent and AI agents drive it over one JSON-RPC 2.0 endpoint.\n\n");
	S += FString::Printf(TEXT("Machine index: `GET %s/help?format=json` — every RPC method with params, flags and a ready-to-run example.\n\n"), *B);

	S += TEXT("## Topics\n\n| Topic | What it covers |\n|---|---|\n");
	S += TEXT("| [rpc](/help/rpc) | every method — title and flags. `?group=car` / `?q=preset` filter; `/help/rpc/<method>` shows one in full with a curl that runs as-is |\n");
	S += TEXT("| [units](/help/units) | units and axes: metres vs cm, Z-up vs Unity Y-up save files, PTZ degrees, pitch/tilt signs |\n\n");

	S += TEXT("## The contract\n\n");
	S += FString::Printf(TEXT("- **One door: `POST %s/rpc`, JSON-RPC 2.0.** `params` is an object. A JSON array of requests is answered with an array; `system.batch {calls:[…], atomic?}` runs several calls in one frame.\n"), *B);
	S += TEXT("- **Ask before you guess.** `/help/rpc/<method>` (or `system.help {method}`) gives params with type/unit/default. `system.catalog` lists names; `system.catalog {detail:true}` returns the same objects as `/help?format=json`.\n");
	S += TEXT("- **Unknown params are not silently accepted.** A success response carries `warnings:[{kind:\"unknownParam\",key}]` for every top-level key the handler never read — check it.\n");
	S += TEXT("- **Mutating calls** return `revision{cars,presets,cameras,view,env,total}` and `frameId` next to `result`; pass `requestId` to make a retry idempotent for 60 s (not on `light.*`, which rejects every unknown key including `requestId`). `view.waitFrame {after:frameId}` waits until a stream shows that change.\n");
	S += TEXT("- **`movesCamera: true`** re-aims a simulated PTZ camera or the main view. Other tools (SettingManager, TourAgent, a person) may be watching that stream — do not call those unless your user asked.\n");
	S += TEXT("- **Units: positions in RPC are UE metres with z = height** (a few env/bay calls take cm — each method says). Save files under `Save/3D/` are not always the same frame. See [units](/help/units).\n");
	S += TEXT("- **Text from this server is Korean** — error messages, place names, car model names. Quote them verbatim; do not translate.\n\n");

	S += TEXT("## First call\n\n```bash\n");
	S += CurlFor(Ctx, TEXT("system.health"), MakeShared<FJsonObject>()) + TEXT("\n");
	S += CurlFor(Ctx, TEXT("car.list"), MakeShared<FJsonObject>()) + TEXT("\n```\n\n");
	S += TEXT("Every method page has a ready-to-run call with this instance's live ids filled in (`fill` lists what is still `<…>`).\n\n");

	S += TEXT("## RPC error codes\n\n| Code | Meaning |\n|---|---|\n");
	for (const FErrorRow& E : GErrors) { S += FString::Printf(TEXT("| `%d` | %s |\n"), E.Code, E.Meaning); }
	S += TEXT("\n");

	S += FString::Printf(TEXT("## RPC methods at a glance\n\n%d methods. Full table: [/help/rpc](/help/rpc) · one method: `/help/rpc/<method>`.\n\n| Group | # | Methods |\n|---|---|---|\n"), D.NumMethods());
	for (const auto& KV : Groups(D))
	{
		TArray<FString> Shown;
		for (int32 i = 0; i < KV.Value.Num() && i < 6; ++i) Shown.Add(FString::Printf(TEXT("`%s`"), *KV.Value[i]));
		FString Line = FString::Join(Shown, TEXT(" "));
		if (KV.Value.Num() > 6) Line += FString::Printf(TEXT(" … (+%d)"), KV.Value.Num() - 6);
		S += FString::Printf(TEXT("| [%s](/help/rpc?group=%s) | %d | %s |\n"), *KV.Key, *KV.Key, KV.Value.Num(), *Line);
	}
	S += TEXT("\n");

	S += TEXT("## Plumbing routes\n\n| Route | What it does |\n|---|---|\n");
	S += TEXT("| `GET /help` | this document — `?format=json` machine index, `/help/rpc`, `/help/rpc/<method>`, `/help/units` |\n");
	S += TEXT("| `POST /rpc` | JSON-RPC 2.0 (single object or array) |\n");
	S += TEXT("| `GET /health` | liveness `{ok:true}` — ports are in RPC `system.health` |\n");
	S += TEXT("| `GET /rpc/catalog` | method names only (same as `system.catalog`) |\n");
	S += TEXT("| `GET /stream?camId=&fps=&quality=&maxSec=` | MJPEG of one camera on the RPC port (camId 0 = selected camera; fps 1..30 default 5; max 4 open) |\n");
	S += FString::Printf(TEXT("| `http://<host>:%d/` | MJPEG of the main view |\n"), Ctx.MainViewPort);
	S += FString::Printf(TEXT("| `http://<host>:<camPortMin + camId - 1>/` | MJPEG per camera (ports %d..%d, `cam.list` gives `streamPort`) |\n\n"), Ctx.CamPortMin, Ctx.CamPortMax);

	S += TEXT("## Before you start work\n\nSearch the team board (baro_memo) — someone may already have hit your problem: ")
		 TEXT("`GET http://gobackdev.iptime.org:22030/memo/api/memos?q=<error string>` (per-user token in header `x-memo-token`; posts are English). ")
		 TEXT("Rules: `GET http://gobackdev.iptime.org:22030/memo/api/help`.\n\n");

	S += TEXT("---\n\n## This instance, right now\n\nComputed per request.\n\n");
	S += FString::Printf(TEXT("- **RPC**: %s/rpc · auth `%s` · exe built %s · help schema `%s`\n"), *B, *Ctx.Auth, *Ctx.ExeBuilt, SchemaVersion());
	S += FString::Printf(TEXT("- **Level**: %s%s\n"), Ctx.LevelName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("%s — "), *Ctx.LevelName), *Ctx.Level);
	S += FString::Printf(TEXT("- **Streams**: main view port %d · cameras %d..%d (`camPortMin + camId - 1`)\n"), Ctx.MainViewPort, Ctx.CamPortMin, Ctx.CamPortMax);
	S += FString::Printf(TEXT("- **Scene**: %d cars (%d visible) · %d cameras · %d parking presets\n"), Ctx.Cars, Ctx.VisibleCars, Ctx.Cameras, Ctx.Presets);
	const TArray<FString> Undoc = UndocumentedMethods(D);
	S += FString::Printf(TEXT("- **RPC methods**: %d (%d without a help entry%s)\n"), D.NumMethods(), Undoc.Num(),
		Undoc.Num() ? *FString::Printf(TEXT(": %s"), *FString::Join(Undoc, TEXT(", "))) : TEXT(""));
	return S;
}

FString RpcListMarkdown(const URpcDispatcher& D, const FLiveContext& Ctx, const FString& Group, const FString& Query)
{
	FString S = TEXT("# RPC methods\n\n");
	S += FString::Printf(TEXT("`POST %s/rpc` · one method in full: `/help/rpc/<method>` · machine index: `/help?format=json`"), *Ctx.BaseUrl);
	if (!Group.IsEmpty()) S += FString::Printf(TEXT(" · group `%s`"), *Group);
	if (!Query.IsEmpty()) S += FString::Printf(TEXT(" · q `%s`"), *Query);
	S += TEXT("\n\n| Method | Title | Flags |\n|---|---|---|\n");
	int32 N = 0;
	for (const FString& M : D.GetMethods())
	{
		if (!Group.IsEmpty() && GroupOf(M) != Group) continue;
		const FMethodDoc* Doc = FindDoc(M);
		const FString Title = Doc ? Doc->Title : TEXT("(no help entry)");
		if (!Query.IsEmpty() && !M.Contains(Query) && !Title.Contains(Query) && !(Doc && FString(Doc->Notes).Contains(Query))) continue;
		const FRpcMethodMeta* Meta = D.FindMethodMeta(M);
		S += FString::Printf(TEXT("| [`%s`](/help/rpc/%s) | %s | %s |\n"), *M, *M, *Cell(Title),
			*FlagsText(D.IsMutating(M), Doc && Doc->bMovesCamera, (Doc && Doc->bDestructive) || (Meta && Meta->bDestructive), Doc ? Doc->bImplemented : true));
		++N;
	}
	S += FString::Printf(TEXT("\n%d methods.\n"), N);
	return S;
}

FString MethodMarkdown(const URpcDispatcher& D, const FString& Method, const FLiveContext& Ctx)
{
	if (!D.HasMethod(Method)) return FString();
	const TSharedPtr<FJsonObject> J = MethodJson(D, Method, Ctx);
	const FMethodDoc* Doc = FindDoc(Method);

	FString S = FString::Printf(TEXT("# %s\n\n"), *Method);
	if (Doc) S += FString(Doc->Title) + TEXT("\n\n");
	else S += TEXT("_No help entry yet — registered on this server but not documented._\n\n");
	S += FString::Printf(TEXT("**Flags**: %s\n\n"), *FlagsText(J->GetBoolField(TEXT("mutating")), J->GetBoolField(TEXT("movesCamera")),
		J->GetBoolField(TEXT("destructive")), J->GetBoolField(TEXT("implemented"))));
	if (Doc && !Doc->bImplemented) S += FString::Printf(TEXT("**Not implemented**: %s\n\n"), Doc->Reason ? Doc->Reason : TEXT(""));

	const TArray<TSharedPtr<FJsonValue>> Params = ParseArray(Doc ? Doc->Params : nullptr);
	S += TEXT("## Params\n\n");
	if (Params.Num() == 0) S += TEXT("none\n\n");
	else
	{
		S += TEXT("| Name | Type | Req | Default | Unit | Meaning |\n|---|---|---|---|---|---|\n");
		for (const TSharedPtr<FJsonValue>& PV : Params)
		{
			const TSharedPtr<FJsonObject> P = PV->AsObject();
			if (!P.IsValid()) continue;
			auto Str = [&P](const TCHAR* K) { FString V; return P->TryGetStringField(K, V) ? V : FString(); };
			bool bReq = false; P->TryGetBoolField(TEXT("required"), bReq);
			const TSharedPtr<FJsonValue> Def = P->TryGetField(TEXT("default"));
			S += FString::Printf(TEXT("| `%s` | %s | %s | %s | %s | %s |\n"), *Str(TEXT("name")), *Cell(Str(TEXT("type"))), bReq ? TEXT("yes") : TEXT(""),
				(Def.IsValid() && Def->Type != EJson::Null) ? *Cell(FString::Printf(TEXT("`%s`"), *CompactValue(Def))) : TEXT(""),
				*Str(TEXT("unit")), *Cell(Str(TEXT("desc"))));
		}
		S += TEXT("\n");
	}
	if (Doc && *Doc->Returns) S += FString::Printf(TEXT("## Returns\n\n%s\n\n"), Doc->Returns);
	if (Doc && *Doc->Notes) S += FString::Printf(TEXT("## Notes\n\n%s\n\n"), Doc->Notes);
	FString Ko;
	if (J->TryGetStringField(TEXT("docKo"), Ko)) S += FString::Printf(TEXT("Registry note (Korean, verbatim): %s\n\n"), *Ko);

	S += TEXT("## Run it\n\n```bash\n") + J->GetStringField(TEXT("curl")) + TEXT("\n```\n\n");
	const TArray<TSharedPtr<FJsonValue>>& Fill = J->GetArrayField(TEXT("fill"));
	if (Fill.Num())
	{
		TArray<FString> F;
		for (const TSharedPtr<FJsonValue>& V : Fill) F.Add(FString::Printf(TEXT("`%s`"), *V->AsString()));
		S += FString::Printf(TEXT("Still to fill: %s (no live value on this instance right now).\n\n"), *FString::Join(F, TEXT(", ")));
	}
	if (J->GetBoolField(TEXT("movesCamera"))) S += TEXT("> Moves a camera or the main view that someone may be watching.\n\n");
	if (J->GetBoolField(TEXT("destructive"))) S += TEXT("> Destructive — deletes something the caller cannot get back.\n\n");
	S += FString::Printf(TEXT("JSON: `%s/help?format=json` → methods[] where method == `%s`. Back: [/help/rpc?group=%s](/help/rpc?group=%s)\n"),
		*Ctx.BaseUrl, *Method, *GroupOf(Method), *GroupOf(Method));
	return S;
}

FString UnitsMarkdown()
{
	// 근거는 코드다(UnityUnrealCoordinateConverter.cpp·CameraControlLibrary.cpp·ViewRpcModule.cpp) — 바꾸면 여기도.
	return TEXT(
		"# Units and axes\n\n"
		"## RPC frame\n\n"
		"- **Positions are UE world metres, no axis swap: x, y = ground plane, z = height.** 1 m = 100 UE units (cm). "
		"Applies to car.*, cam.*, preset.*, bay.*, sim.*, view.*, env.create/update.\n"
		"- **Exceptions in centimetres:** `env.list` and `env.hide` (`pos`, `size`, `distance`, `near`, `radius` are raw UE cm). Each method page states its unit.\n"
		"- **Send all three of x, y, z.** Several older calls (`car.create`, `car.setPosition`, `car.update`, `car.createLine`, `cam.setPosition`, "
		"`cam.setPreset`, `preset.create`, `bay.create`, `env.create`) use a Unity-era reader that *requires x and z* and silently uses **y = 0** when y is missing. "
		"`{x,y}` alone is rejected there; `{x,z}` puts the object on the y = 0 line.\n"
		"- Cars are snapped to the ground after every placement — their z is overwritten.\n\n"
		"## Angles (degrees)\n\n"
		"| Quantity | Convention |\n|---|---|\n"
		"| yaw, `rotY`, `pan` | UE yaw: 0 = +X, 90 = +Y |\n"
		"| PTZ `tilt` (cam.*) | **positive = looking down** (`Pitch = -Tilt`). No clamp in RPC; `ptzmin/ptzmax` in files only limit the panel sliders |\n"
		"| view `pitch` (view.*) | **positive = looking up**, clamped -89..89. `view.topDown` uses -89. Opposite sign to cam tilt |\n"
		"| PTZ `zoom` | optical factor 1..36 (config `max_zoom`), not FOV. hfov = 2·atan(tan(56.5°/2)/zoom) |\n"
		"| `fov` (cam.setFOV, view.set) | horizontal FOV in degrees. view.set fov ≠ 0 locks it, 0 unlocks |\n"
		"| car `rotY` | yaw of the car's logical front; actor yaw = rotY + 270 (+180 when `isFront` false = rear-in) |\n"
		"| preset `faceRot` / `groupRot` | faceRot turns each face about its own centre (normalised > 180° flips the step direction); groupRot turns the whole row about `offset` (centre of face 1) |\n\n"
		"There is no raw (encoder) PTZ unit anywhere in this API.\n\n"
		"## Save files (`Save/3D/**/*.json`)\n\n"
		"- Car, camera and preset files carry a root flag **`isUnreal`**. `true` = UE metres, z = height (same as RPC). Every save writes `true`.\n"
		"- Missing/false = **legacy Unity metres, y = height**, converted on load as **UE = (Unity.z, Unity.x, Unity.y)** — the two ground axes swap. Angles are not converted.\n"
		"- Camera files: on load `pan`/`tilt` are taken from `rot.y`/`rot.x`; a file with only pan/tilt loads as 0/0.\n"
		"- `car.save`/`car.load` with a bare `fileName` resolve to `Saved/CarData/`, not `Save/3D/CarPos/` — use `fullPath` for the latter.\n\n"
		"## Pixels\n\n"
		"- `view.pick {x,y}` is in the main-view stream render target (default 960×540), origin top-left; `{u,v}` is the same normalised 0..1.\n"
		"- Camera captures (`cam.captureJPG`) default to the stream size 1280×720; `width`/`height` request another size.\n");
}

TArray<FString> UndocumentedMethods(const URpcDispatcher& D)
{
	TArray<FString> Out;
	for (const FString& M : D.GetMethods()) { if (!FindDoc(M)) Out.Add(M); }
	return Out;
}

TArray<FString> StaleDocs(const URpcDispatcher& D)
{
	TArray<FString> Out;
	for (const FMethodDoc& Doc : GetTable()) { if (!D.HasMethod(Doc.Method)) Out.Add(Doc.Method); }
	return Out;
}
}
