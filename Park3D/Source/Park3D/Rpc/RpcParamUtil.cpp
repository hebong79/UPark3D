// Copyright Epic Games, Inc. All Rights Reserved.

#include "RpcParamUtil.h"

namespace RpcParam
{
	bool Has(const TSharedPtr<FJsonObject>& P, const FString& Key)
	{
		return P.IsValid() && P->HasField(Key);
	}

	int32 GetInt(const TSharedPtr<FJsonObject>& P, const FString& Key, int32 Default)
	{
		if (!P.IsValid()) return Default;
		double V = Default;
		return P->TryGetNumberField(Key, V) ? static_cast<int32>(FMath::RoundToDouble(V)) : Default;
	}

	double GetFloat(const TSharedPtr<FJsonObject>& P, const FString& Key, double Default)
	{
		if (!P.IsValid()) return Default;
		double V = Default;
		return P->TryGetNumberField(Key, V) ? V : Default;
	}

	bool GetBool(const TSharedPtr<FJsonObject>& P, const FString& Key, bool Default)
	{
		if (!P.IsValid()) return Default;
		bool V = Default;
		return P->TryGetBoolField(Key, V) ? V : Default;
	}

	FString GetString(const TSharedPtr<FJsonObject>& P, const FString& Key, const FString& Default)
	{
		if (!P.IsValid()) return Default;
		FString V;
		return P->TryGetStringField(Key, V) ? V : Default;
	}

	FVector GetVec3(const TSharedPtr<FJsonObject>& P, const FString& Key, const FVector& Default)
	{
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
		double V = 0.0;
		if (!P.IsValid() || !P->TryGetNumberField(Key, V))
		{
			OutError.FailDomain(FString::Printf(TEXT("필수 파라미터 누락: %s"), *Key));
			return false;
		}
		Out = static_cast<int32>(FMath::RoundToDouble(V));
		return true;
	}

	bool RequireFloat(const TSharedPtr<FJsonObject>& P, const FString& Key, double& Out, FRpcError& OutError)
	{
		if (!P.IsValid() || !P->TryGetNumberField(Key, Out))
		{
			OutError.FailDomain(FString::Printf(TEXT("필수 파라미터 누락: %s"), *Key));
			return false;
		}
		return true;
	}

	bool RequireBool(const TSharedPtr<FJsonObject>& P, const FString& Key, bool& Out, FRpcError& OutError)
	{
		if (!P.IsValid() || !P->TryGetBoolField(Key, Out))
		{
			OutError.FailDomain(FString::Printf(TEXT("필수 파라미터 누락: %s"), *Key));
			return false;
		}
		return true;
	}

	bool RequireString(const TSharedPtr<FJsonObject>& P, const FString& Key, FString& Out, FRpcError& OutError)
	{
		if (!P.IsValid() || !P->TryGetStringField(Key, Out))
		{
			OutError.FailDomain(FString::Printf(TEXT("필수 파라미터 누락: %s"), *Key));
			return false;
		}
		return true;
	}

	bool RequirePosXZ(const TSharedPtr<FJsonObject>& P, const FString& Key, FVector& Out, FRpcError& OutError, double DefaultY)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!P.IsValid() || !P->TryGetObjectField(Key, Obj) || !Obj || !Obj->IsValid())
		{
			OutError.FailDomain(FString::Printf(TEXT("필수 파라미터 누락: %s"), *Key));
			return false;
		}
		double X = 0.0, Z = 0.0;
		if (!(*Obj)->TryGetNumberField(TEXT("x"), X) || !(*Obj)->TryGetNumberField(TEXT("z"), Z))
		{
			OutError.FailDomain(FString::Printf(TEXT("%s 는 x,z 가 필수입니다"), *Key));
			return false;
		}
		double Y = DefaultY;
		(*Obj)->TryGetNumberField(TEXT("y"), Y);
		Out = FVector(X, Y, Z);
		return true;
	}

	bool RequireVec3(const TSharedPtr<FJsonObject>& P, const FString& Key, FVector& Out, FRpcError& OutError)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!P.IsValid() || !P->TryGetObjectField(Key, Obj) || !Obj || !Obj->IsValid())
		{
			OutError.FailDomain(FString::Printf(TEXT("필수 파라미터 누락: %s"), *Key));
			return false;
		}
		double X = 0.0, Y = 0.0, Z = 0.0;
		if (!(*Obj)->TryGetNumberField(TEXT("x"), X) || !(*Obj)->TryGetNumberField(TEXT("y"), Y) || !(*Obj)->TryGetNumberField(TEXT("z"), Z))
		{
			OutError.FailDomain(FString::Printf(TEXT("%s 는 x,y,z 가 필수입니다"), *Key));
			return false;
		}
		Out = FVector(X, Y, Z);
		return true;
	}
}
