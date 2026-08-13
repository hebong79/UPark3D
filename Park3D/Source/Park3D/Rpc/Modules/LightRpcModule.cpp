// Copyright Epic Games, Inc. All Rights Reserved.

#include "LightRpcModule.h"
#include "../RpcDispatcher.h"
#include "../RpcParamUtil.h"
#include "../../Light/LightControlManager.h"
#include "../../Light/LightControlLibrary.h"
#include "Misc/Paths.h"

namespace
{
	/** fullPath 우선, 없으면 Save/3D/Light + fileName + ".json"(패널 저장 위치와 같은 폴더). */
	FString ResolveLightPath(const TSharedPtr<FJsonObject>& P)
	{
		const FString FullPath = RpcParam::GetString(P, TEXT("fullPath"));
		if (!FullPath.IsEmpty())
		{
			return FullPath;
		}
		FString FileName = RpcParam::GetString(P, TEXT("fileName"), TEXT("LightSettings"));
		if (!FileName.EndsWith(TEXT(".json")))
		{
			FileName += TEXT(".json");
		}
		return FPaths::Combine(ULightControlLibrary::GetLightDir(), FileName);
	}

	TSharedPtr<FJsonObject> SettingsToDto(const FLightSettings& S)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("exposureEV100"), S.ExposureEV100);
		O->SetNumberField(TEXT("sunIntensity"), S.SunIntensity);
		O->SetNumberField(TEXT("sunAltitudeDeg"), S.SunAltitudeDeg);
		O->SetNumberField(TEXT("sunAzimuthDeg"), S.SunAzimuthDeg);
		O->SetNumberField(TEXT("skyIntensity"), S.SkyIntensity);
		O->SetObjectField(TEXT("sunColor"), RpcDto::Vec3(S.SunColor.R, S.SunColor.G, S.SunColor.B));
		return O;
	}

	/** 전달된 키만 덮는다(부분 수정). sunColor 는 {x,y,z} = RGB(0~1). */
	void ApplyOptionalFields(const TSharedPtr<FJsonObject>& P, FLightSettings& S)
	{
		if (RpcParam::Has(P, TEXT("exposureEV100")))  S.ExposureEV100 = RpcParam::GetFloat(P, TEXT("exposureEV100"), S.ExposureEV100);
		if (RpcParam::Has(P, TEXT("sunIntensity")))   S.SunIntensity = RpcParam::GetFloat(P, TEXT("sunIntensity"), S.SunIntensity);
		if (RpcParam::Has(P, TEXT("sunAltitudeDeg"))) S.SunAltitudeDeg = RpcParam::GetFloat(P, TEXT("sunAltitudeDeg"), S.SunAltitudeDeg);
		if (RpcParam::Has(P, TEXT("sunAzimuthDeg")))  S.SunAzimuthDeg = RpcParam::GetFloat(P, TEXT("sunAzimuthDeg"), S.SunAzimuthDeg);
		if (RpcParam::Has(P, TEXT("skyIntensity")))   S.SkyIntensity = RpcParam::GetFloat(P, TEXT("skyIntensity"), S.SkyIntensity);
		if (RpcParam::Has(P, TEXT("sunColor")))
		{
			const FVector C = RpcParam::GetVec3(P, TEXT("sunColor"), FVector(S.SunColor.R, S.SunColor.G, S.SunColor.B));
			S.SunColor = FLinearColor(static_cast<float>(C.X), static_cast<float>(C.Y), static_cast<float>(C.Z), 1.f);
		}
	}
}

void FLightRpcModule::Register(URpcDispatcher& Dispatcher)
{
	// 현재 레벨 조명을 되읽는다. 태양을 못 찾으면 마지막 적용값으로 답한다.
	Dispatcher.Register(TEXT("light.get"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ALightControlManager* Mgr = ALightControlManager::GetOrSpawn(GetWorldPtr());
		if (!Mgr) { E.FailDomain(TEXT("조명 매니저 없음(월드 미로드)")); return nullptr; }

		FLightSettings S;
		const bool bFromWorld = Mgr->CaptureCurrent(S);
		if (!bFromWorld) { S = Mgr->GetLastApplied(); }

		TSharedPtr<FJsonObject> O = SettingsToDto(S);
		O->SetBoolField(TEXT("fromWorld"), bFromWorld);
		return RpcDto::MakeObject(O);
	});

	// 전달한 항목만 바꿔 즉시 적용한다(가림률 측정 중 노출을 고정하는 용도).
	Dispatcher.Register(TEXT("light.set"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ALightControlManager* Mgr = ALightControlManager::GetOrSpawn(GetWorldPtr());
		if (!Mgr) { E.FailDomain(TEXT("조명 매니저 없음(월드 미로드)")); return nullptr; }

		// 기준은 현재 월드 상태다 — 마지막 적용값에서 출발하면 패널로 바꾼 값을 되돌려 버린다.
		FLightSettings S;
		if (!Mgr->CaptureCurrent(S)) { S = Mgr->GetLastApplied(); }
		ApplyOptionalFields(P, S);
		Mgr->ApplySettings(S);

		return RpcDto::MakeObject(SettingsToDto(Mgr->GetLastApplied()));
	});

	Dispatcher.Register(TEXT("light.save"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ALightControlManager* Mgr = ALightControlManager::GetOrSpawn(GetWorldPtr());
		if (!Mgr) { E.FailDomain(TEXT("조명 매니저 없음(월드 미로드)")); return nullptr; }

		FLightSettings S;
		if (!Mgr->CaptureCurrent(S)) { S = Mgr->GetLastApplied(); }

		const FString Path = ResolveLightPath(P);
		if (!ULightControlLibrary::SaveToFile(Path, S))
		{
			E.FailDomain(FString::Printf(TEXT("조명 저장 실패: %s"), *Path));
			return nullptr;
		}
		TSharedPtr<FJsonObject> O = SettingsToDto(S);
		O->SetBoolField(TEXT("ok"), true);
		O->SetStringField(TEXT("path"), Path);
		O->SetStringField(TEXT("fileName"), FPaths::GetCleanFilename(Path));
		return RpcDto::MakeObject(O);
	});

	// 파일을 읽어 즉시 적용한다. apply=false 면 값만 돌려주고 월드는 건드리지 않는다.
	Dispatcher.Register(TEXT("light.load"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ALightControlManager* Mgr = ALightControlManager::GetOrSpawn(GetWorldPtr());
		if (!Mgr) { E.FailDomain(TEXT("조명 매니저 없음(월드 미로드)")); return nullptr; }

		const FString Path = ResolveLightPath(P);
		FLightSettings S;
		if (!ULightControlLibrary::LoadFromFile(Path, S))
		{
			E.FailDomain(FString::Printf(TEXT("조명 로드 실패: %s"), *Path));
			return nullptr;
		}
		const bool bApply = RpcParam::GetBool(P, TEXT("apply"), true);
		if (bApply) { Mgr->ApplySettings(S); }

		TSharedPtr<FJsonObject> O = SettingsToDto(bApply ? Mgr->GetLastApplied() : S);
		O->SetBoolField(TEXT("ok"), true);
		O->SetBoolField(TEXT("applied"), bApply);
		O->SetStringField(TEXT("fileName"), FPaths::GetCleanFilename(Path));
		return RpcDto::MakeObject(O);
	});

	// 기동 시 적용되는 기본 파일 포인터를 갱신한다(다음 실행부터 이 설정으로 뜬다).
	Dispatcher.Register(TEXT("light.setDefault"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		const FString Path = ResolveLightPath(P);
		if (!ULightControlLibrary::SetDefaultFile(Path))
		{
			E.FailDomain(FString::Printf(TEXT("기본 조명 파일 지정 실패: %s"), *Path));
			return nullptr;
		}
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetStringField(TEXT("path"), Path);
		return RpcDto::MakeObject(O);
	});
}
