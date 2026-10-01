// Copyright Epic Games, Inc. All Rights Reserved.

#include "RpcParamUtil.h"
#include <atomic>

namespace RpcParam
{
	namespace
	{
		// RPC 는 게임 스레드에서만 디스패치된다(HTTP 라우터 콜백). 중첩 디스패치는 Prev 로 쌓는다.
		FReadTracker* GActiveTracker = nullptr;
	}

	FReadTracker::FReadTracker(const TSharedPtr<FJsonObject>& InRoot)
		: Root(InRoot.Get()), Prev(GActiveTracker)
	{
		GActiveTracker = this;
	}

	FReadTracker::~FReadTracker()
	{
		GActiveTracker = Prev;
	}

	TArray<FString> FReadTracker::UnreadKeys() const
	{
		TArray<FString> Out;
		if (Root)
		{
			for (const auto& KV : Root->Values)
			{
				const FString Key(KV.Key);
				if (!Read.Contains(Key)) { Out.Add(Key); }
			}
		}
		Out.Sort();
		return Out;
	}

	void MarkRead(const TSharedPtr<FJsonObject>& P, const FString& Key)
	{
		// 같은 Root 를 보는 바깥 추적기까지 모두 기록한다(핸들러가 다른 핸들러에 P 를 그대로 넘기는 경우).
		for (FReadTracker* T = GActiveTracker; T; T = T->Prev)
		{
			if (P.IsValid() && T->Root == P.Get()) { T->Read.Add(Key); }
		}
	}

	bool Has(const TSharedPtr<FJsonObject>& P, const FString& Key)
	{
		MarkRead(P, Key);
		return P.IsValid() && P->HasField(Key);
	}

	int32 GetInt(const TSharedPtr<FJsonObject>& P, const FString& Key, int32 Default)
	{
		MarkRead(P, Key);
		if (!P.IsValid()) return Default;
		double V = Default;
		return P->TryGetNumberField(Key, V) ? static_cast<int32>(FMath::RoundToDouble(V)) : Default;
	}

	double GetFloat(const TSharedPtr<FJsonObject>& P, const FString& Key, double Default)
	{
		MarkRead(P, Key);
		if (!P.IsValid()) return Default;
		double V = Default;
		return P->TryGetNumberField(Key, V) ? V : Default;
	}

	bool GetBool(const TSharedPtr<FJsonObject>& P, const FString& Key, bool Default)
	{
		MarkRead(P, Key);
		if (!P.IsValid()) return Default;
		bool V = Default;
		return P->TryGetBoolField(Key, V) ? V : Default;
	}

	FString GetString(const TSharedPtr<FJsonObject>& P, const FString& Key, const FString& Default)
	{
		MarkRead(P, Key);
		if (!P.IsValid()) return Default;
		FString V;
		return P->TryGetStringField(Key, V) ? V : Default;
	}

	FVector GetVec3(const TSharedPtr<FJsonObject>& P, const FString& Key, const FVector& Default)
	{
		MarkRead(P, Key);
		if (!P.IsValid()) return Default;
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!P->TryGetObjectField(Key, Obj) || !Obj || !Obj->IsValid())
		{
			return Default;
		}
		FVector Out = Default;
		double C = 0.0;
		if ((*Obj)->TryGetNumberField(TEXT("x"), C)) Out.X = C;
		if ((*Obj)->TryGetNumberField(TEXT("y"), C)) Out.Y = C;
		if ((*Obj)->TryGetNumberField(TEXT("z"), C)) Out.Z = C;
		return Out;
	}

	bool RequireInt(const TSharedPtr<FJsonObject>& P, const FString& Key, int32& Out, FRpcError& OutError)
	{
		MarkRead(P, Key);
		double V = 0.0;
		if (!P.IsValid() || !P->TryGetNumberField(Key, V))
		{
			OutError.FailDomain(FString::Printf(TEXT("필수 파라미터 누락: %s"), *Key), ERpcErrorKind::BadParams);
			return false;
		}
		Out = static_cast<int32>(FMath::RoundToDouble(V));
		return true;
	}

	bool RequireFloat(const TSharedPtr<FJsonObject>& P, const FString& Key, double& Out, FRpcError& OutError)
	{
		MarkRead(P, Key);
		if (!P.IsValid() || !P->TryGetNumberField(Key, Out))
		{
			OutError.FailDomain(FString::Printf(TEXT("필수 파라미터 누락: %s"), *Key), ERpcErrorKind::BadParams);
			return false;
		}
		return true;
	}

	bool RequireBool(const TSharedPtr<FJsonObject>& P, const FString& Key, bool& Out, FRpcError& OutError)
	{
		MarkRead(P, Key);
		if (!P.IsValid() || !P->TryGetBoolField(Key, Out))
		{
			OutError.FailDomain(FString::Printf(TEXT("필수 파라미터 누락: %s"), *Key), ERpcErrorKind::BadParams);
			return false;
		}
		return true;
	}

	bool RequireString(const TSharedPtr<FJsonObject>& P, const FString& Key, FString& Out, FRpcError& OutError)
	{
		MarkRead(P, Key);
		if (!P.IsValid() || !P->TryGetStringField(Key, Out))
		{
			OutError.FailDomain(FString::Printf(TEXT("필수 파라미터 누락: %s"), *Key), ERpcErrorKind::BadParams);
			return false;
		}
		return true;
	}

	bool RequirePosXZ(const TSharedPtr<FJsonObject>& P, const FString& Key, FVector& Out, FRpcError& OutError, double DefaultY)
	{
		MarkRead(P, Key);
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!P.IsValid() || !P->TryGetObjectField(Key, Obj) || !Obj || !Obj->IsValid())
		{
			OutError.FailDomain(FString::Printf(TEXT("필수 파라미터 누락: %s"), *Key), ERpcErrorKind::BadParams);
			return false;
		}
		double X = 0.0, Z = 0.0;
		if (!(*Obj)->TryGetNumberField(TEXT("x"), X) || !(*Obj)->TryGetNumberField(TEXT("z"), Z))
		{
			OutError.FailDomain(FString::Printf(TEXT("%s 는 x,z 가 필수입니다"), *Key), ERpcErrorKind::BadParams);
			return false;
		}
		double Y = DefaultY;
		(*Obj)->TryGetNumberField(TEXT("y"), Y);
		Out = FVector(X, Y, Z);
		return true;
	}

	bool RequireVec3(const TSharedPtr<FJsonObject>& P, const FString& Key, FVector& Out, FRpcError& OutError)
	{
		MarkRead(P, Key);
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!P.IsValid() || !P->TryGetObjectField(Key, Obj) || !Obj || !Obj->IsValid())
		{
			OutError.FailDomain(FString::Printf(TEXT("필수 파라미터 누락: %s"), *Key), ERpcErrorKind::BadParams);
			return false;
		}
		double X = 0.0, Y = 0.0, Z = 0.0;
		if (!(*Obj)->TryGetNumberField(TEXT("x"), X) || !(*Obj)->TryGetNumberField(TEXT("y"), Y) || !(*Obj)->TryGetNumberField(TEXT("z"), Z))
		{
			OutError.FailDomain(FString::Printf(TEXT("%s 는 x,y,z 가 필수입니다"), *Key), ERpcErrorKind::BadParams);
			return false;
		}
		Out = FVector(X, Y, Z);
		return true;
	}
}

namespace Park3DRpc
{
	namespace { std::atomic<int64> GSceneSeq{ 0 }; }
	int64 SceneSeq() { return GSceneSeq.load(); }
	int64 BumpSceneSeq() { return ++GSceneSeq; }

	const TCHAR* ErrorKindName(ERpcErrorKind Kind)
	{
		switch (Kind)
		{
		case ERpcErrorKind::BadParams:   return TEXT("bad_params");
		case ERpcErrorKind::NotFound:    return TEXT("not_found");
		case ERpcErrorKind::Busy:        return TEXT("busy");
		case ERpcErrorKind::Unsupported: return TEXT("unsupported");
		default:                         return TEXT("internal");
		}
	}

	ERpcErrorKind ClassifyErrorKind(const FRpcError& Err)
	{
		if (Err.Kind != ERpcErrorKind::None) return Err.Kind;
		switch (Err.Code)
		{
		case MethodNotFound: return ERpcErrorKind::Unsupported;
		case InvalidParams:  return ERpcErrorKind::BadParams;
		case NotImplemented: return ERpcErrorKind::Unsupported;
		case ParseError:     return ERpcErrorKind::BadParams;
		default: break;
		}

		// -32000 도메인 오류: 핸들러가 kind 를 안 줬으면 메시지로 가른다. 순서가 곧 우선순위다
		// (예: "필수 파라미터 누락: camId" 는 '없' 이 없어도 bad_params, "월드 없음" 은 not_found 가 아니라 busy).
		auto Any = [&Err](std::initializer_list<const TCHAR*> Words)
		{
			for (const TCHAR* W : Words) { if (Err.Message.Contains(W)) return true; }
			return false;
		};
		if (Any({ TEXT("미구현"), TEXT("not implemented"), TEXT("실RHI") }))
			return ERpcErrorKind::Unsupported;
		if (Any({ TEXT("필수"), TEXT("누락"), TEXT("허용되지 않은"), TEXT("잘못"), TEXT("범위"), TEXT("형식이 아닙니다"),
				  TEXT("이어야"), TEXT("여야"), TEXT("해석할 수 없"), TEXT("필요"), TEXT("비었다"), TEXT("비어 있다"),
				  TEXT("양수"), TEXT("보다 커야"), TEXT("동일합니다"), TEXT("중 하나"), TEXT("밖입니다"), TEXT("알 수 없는"),
				  TEXT("이미 있는 파일"), TEXT("파일이 아닙니다") }))
			return ERpcErrorKind::BadParams;
		if (Any({ TEXT("월드"), TEXT("로드된 상태"), TEXT("플레이어"), TEXT("서브시스템 없음") }))
			return ERpcErrorKind::Busy;
		if (Any({ TEXT("UI"), TEXT("패널이 없") }))
			return ERpcErrorKind::Unsupported;
		if (Any({ TEXT("실패"), TEXT("찾거나 생성할 수 없"), TEXT("매니저를 만들 수 없"), TEXT("디스패처"), TEXT("카탈로그가 비어") }))
			return ERpcErrorKind::Internal;
		if (Any({ TEXT("없음"), TEXT("없습니다"), TEXT("없다"), TEXT("찾을 수 없"), TEXT("찾지 못"), TEXT("없어") }))
			return ERpcErrorKind::NotFound;
		// 남은 것은 요청이 지금 상태에서 성립하지 않는 경우(카메라가 슬롯 바로 위 등) — 호출자가 고칠 문제다.
		return ERpcErrorKind::BadParams;
	}
}
