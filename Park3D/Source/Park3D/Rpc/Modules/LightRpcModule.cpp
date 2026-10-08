// Copyright Epic Games, Inc. All Rights Reserved.

#include "LightRpcModule.h"
#include "CarFilePaths.h"
#include "../RpcDispatcher.h"
#include "../RpcParamUtil.h"
#include "../../Light/LightControlManager.h"
#include "../../Light/LightControlLibrary.h"
#include "../../Light/LotLampActor.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "HAL/FileManager.h"
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

	/** 조명 값 키 8개(sunColor 포함). set 의 허용 키이자 get 의 출력 키다. */
	const TCHAR* const ValueKeys[] = {
		TEXT("exposureEV100"), TEXT("sunIntensity"), TEXT("sunAltitudeDeg"), TEXT("sunAzimuthDeg"),
		TEXT("skyIntensity"), TEXT("shadowFillIntensity"), TEXT("carFillIntensity"), TEXT("sunColor"),
	};

	/** 시간대·날씨·밤 조명 키(보드 #1326 B1). FLightEnv 필드 순서와 같다. */
	const TCHAR* const EnvValueKeys[] = {
		TEXT("timeOfDay"), TEXT("nightAmbient"), TEXT("fog"), TEXT("cloudCoverage"), TEXT("rain"), TEXT("wetness"),
	};

	float* EnvValueField(FLightEnv& E, int32 i)
	{
		float* F[] = { &E.TimeOfDay, &E.NightAmbient, &E.Fog, &E.CloudCoverage, &E.Rain, &E.Wetness };
		return F[i];
	}

	bool EnvKeySupported(const FLightSupports& S, int32 i)
	{
		const bool B[] = { S.bTimeOfDay, S.bNightAmbient, S.bFog, S.bCloudCoverage, S.bRain, S.bWetness };
		return B[i];
	}

	/** 음수 = 없음(null). */
	void SetNumberOrNull(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, float V)
	{
		if (V < 0.0f) { O->SetField(Key, MakeShared<FJsonValueNull>()); }
		else          { O->SetNumberField(Key, V); }
	}

	void EnvToDto(const TSharedPtr<FJsonObject>& O, const FLightEnv& Env)
	{
		FLightEnv Copy = Env;
		for (int32 i = 0; i < UE_ARRAY_COUNT(EnvValueKeys); ++i)
		{
			SetNumberOrNull(O, EnvValueKeys[i], *EnvValueField(Copy, i));
		}
	}

	TSharedPtr<FJsonObject> SupportsDto(const FLightSupports& S)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("timeOfDay"), S.bTimeOfDay);
		O->SetBoolField(TEXT("nightAmbient"), S.bNightAmbient);
		O->SetBoolField(TEXT("fog"), S.bFog);
		O->SetBoolField(TEXT("cloudCoverage"), S.bCloudCoverage);
		O->SetBoolField(TEXT("rain"), S.bRain);
		O->SetBoolField(TEXT("wetness"), S.bWetness);
		O->SetBoolField(TEXT("lamps"), S.bLamps);
		O->SetBoolField(TEXT("carLights"), S.bCarLights);
		return O;
	}

	/** 지면 높이(cm) — lot.* 와 같은 정적 오브젝트 질의(차량·가로등은 걸리지 않는다). 못 찾으면 0. */
	double LampGroundZ(UWorld* World, double Xcm, double Ycm)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(LampGround), /*bTraceComplex=*/true);
		if (World && World->LineTraceSingleByObjectType(Hit, FVector(Xcm, Ycm, 300.0), FVector(Xcm, Ycm, -1000.0),
			FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllStaticObjects), Params))
		{
			return Hit.ImpactPoint.Z;
		}
		return 0.0;
	}

	TSharedPtr<FJsonObject> LampsDto(UWorld* World)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const ALotLampActor* L : ALotLampActor::GetAll(World)) { Arr.Add(MakeShared<FJsonValueObject>(L->ToJson())); }
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetArrayField(TEXT("lamps"), Arr);
		O->SetNumberField(TEXT("count"), Arr.Num());
		O->SetStringField(TEXT("file"), ALotLampActor::GetFilePath(World));
		return O;
	}

	/** UDS 같은 통합 하늘 시스템 레벨에서 적용되지 않는 키(ApplySettings 가 건너뛴다). */
	bool IsIgnoredOnExternalSky(const FString& Key)
	{
		return Key == TEXT("exposureEV100") || Key == TEXT("sunIntensity") || Key == TEXT("sunAltitudeDeg")
			|| Key == TEXT("sunAzimuthDeg") || Key == TEXT("skyIntensity") || Key == TEXT("sunColor");
	}

	/** 허용 목록 밖의 키가 있으면 -32602 로 거절한다(받고 무시하는 것이 가장 나쁜 실패다 - 보드 #1099). */
	bool RejectUnknownKeys(const TSharedPtr<FJsonObject>& P, std::initializer_list<const TCHAR*> Allowed, FRpcError& E)
	{
		if (!P.IsValid())
		{
			return true;
		}
		for (const auto& Pair : P->Values)
		{
			bool bKnown = false;
			for (const TCHAR* A : Allowed)
			{
				if (Pair.Key == A) { bKnown = true; break; }
			}
			if (!bKnown)
			{
				FString List;
				for (const TCHAR* A : Allowed) { List += (List.IsEmpty() ? TEXT("") : TEXT(", ")); List += A; }
				E.Fail(Park3DRpc::InvalidParams,
					FString::Printf(TEXT("알 수 없는 키: '%s' (허용: %s)"), *Pair.Key, List.IsEmpty() ? TEXT("없음") : *List),
					ERpcErrorKind::BadParams);
				return false;
			}
		}
		return true;
	}

	TSharedPtr<FJsonObject> SettingsToDto(const FLightSettings& S)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("exposureEV100"), S.ExposureEV100);
		O->SetNumberField(TEXT("sunIntensity"), S.SunIntensity);
		O->SetNumberField(TEXT("sunAltitudeDeg"), S.SunAltitudeDeg);
		O->SetNumberField(TEXT("sunAzimuthDeg"), S.SunAzimuthDeg);
		O->SetNumberField(TEXT("skyIntensity"), S.SkyIntensity);
		O->SetNumberField(TEXT("shadowFillIntensity"), S.ShadowFillIntensity);
		O->SetNumberField(TEXT("carFillIntensity"), S.CarFillIntensity);
		O->SetObjectField(TEXT("sunColor"), RpcDto::Vec3(S.SunColor.R, S.SunColor.G, S.SunColor.B));
		return O;
	}

	/**
	 * light.get 모양의 "지금 상태". 쓰기 응답도 전부 이 모양이다 - 클라이언트가 get 을 또 부르지 않아도 되게.
	 * 월드에서 읽히면 그 값, 아니면 마지막 적용값. 값이 무엇을 뜻하는지는 아래 두 필드가 알려 준다.
	 *  - fromWorld: 태양 컴포넌트를 찾아 되읽었다(= 화면과 일치한다는 뜻이 아니다. UDS 레벨은 externalSkySystem 참고).
	 *  - externalSkySystem: true 면 sun·sky·exposure 값은 레벨의 하늘 시스템이 쥐고 있어 이 RPC 로 바뀌지 않는다.
	 */
	TSharedPtr<FJsonObject> StateDto(const ALightControlManager& Mgr)
	{
		FLightSettings S;
		const bool bFromWorld = Mgr.CaptureCurrent(S);
		if (!bFromWorld) { S = Mgr.GetLastApplied(); }

		TSharedPtr<FJsonObject> O = SettingsToDto(S);
		O->SetBoolField(TEXT("fromWorld"), bFromWorld);
		O->SetStringField(TEXT("source"), Mgr.GetSourceKind());
		if (Mgr.GetCurrentFileName().IsEmpty()) { O->SetField(TEXT("fileName"), MakeShared<FJsonValueNull>()); }
		else                                    { O->SetStringField(TEXT("fileName"), Mgr.GetCurrentFileName()); }
		O->SetBoolField(TEXT("externalSkySystem"), Mgr.UsesExternalSkySystem());
		EnvToDto(O, Mgr.GetEnv());
		O->SetObjectField(TEXT("supports"), SupportsDto(Mgr.GetSupports()));
		// 가로등 요약(light.lamps 와 짝) — 하나라도 켜져 있으면 on, 세기는 첫 가로등(light.lamps 가 전부 같게 맞춘다). 없으면 null.
		const TArray<ALotLampActor*> Lamps = ALotLampActor::GetAll(Mgr.GetWorld());
		TSharedPtr<FJsonObject> L = MakeShared<FJsonObject>();
		L->SetNumberField(TEXT("count"), Lamps.Num());
		bool bAnyOn = false;
		for (const ALotLampActor* Lamp : Lamps) { bAnyOn |= Lamp->IsOn(); }
		L->SetBoolField(TEXT("on"), bAnyOn);
		if (Lamps.Num() > 0) { L->SetNumberField(TEXT("intensity"), Lamps[0]->GetIntensity()); }
		else                 { L->SetField(TEXT("intensity"), MakeShared<FJsonValueNull>()); }
		O->SetObjectField(TEXT("lamps"), L);
		return O;
	}

	TSharedPtr<FJsonObject> MetaDto()
	{
		TSharedPtr<FJsonObject> M = MakeShared<FJsonObject>();
		for (const ULightControlLibrary::FKeyMeta& K : ULightControlLibrary::GetKeyMeta())
		{
			TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
			E->SetNumberField(TEXT("min"), K.Min);
			E->SetNumberField(TEXT("max"), K.Max);
			E->SetNumberField(TEXT("step"), K.Step);
			E->SetStringField(TEXT("unit"), K.Unit);
			E->SetNumberField(TEXT("default"), K.Default);
			M->SetObjectField(K.Key, E);
		}
		// 색은 채널별 0~1 선형(엔진이 8비트 sRGB 로 저장하므로 되읽기에 ±1/255 오차가 난다).
		TSharedPtr<FJsonObject> C = MakeShared<FJsonObject>();
		C->SetNumberField(TEXT("min"), 0.0);
		C->SetNumberField(TEXT("max"), 1.0);
		C->SetNumberField(TEXT("step"), 0.01);
		C->SetStringField(TEXT("unit"), TEXT("linear RGB 0..1"));
		C->SetObjectField(TEXT("default"), RpcDto::Vec3(1.0, 1.0, 1.0));
		M->SetObjectField(TEXT("sunColor"), C);
		// 시간대·날씨(보드 #1326). default = 기동 시 값(음수 키는 "건드리지 않음" 이라 meta 에선 0).
		struct FEnvMeta { const TCHAR* Key; float Min, Max, Step; const TCHAR* Unit; float Default; };
		const FLightEnv D;
		for (const FEnvMeta& K : {
			FEnvMeta{ TEXT("timeOfDay"),     0.f, 24.f, 0.25f, TEXT("hour"), 12.f },
			FEnvMeta{ TEXT("nightAmbient"),  0.f, 5.f,  0.05f, TEXT("lux"),  D.NightAmbient },
			FEnvMeta{ TEXT("fog"),           0.f, 1.f,  0.05f, TEXT("0..1"), 0.f },
			FEnvMeta{ TEXT("cloudCoverage"), 0.f, 1.f,  0.05f, TEXT("0..1"), 0.f },
			FEnvMeta{ TEXT("rain"),          0.f, 1.f,  0.05f, TEXT("0..1"), 0.f },
			FEnvMeta{ TEXT("wetness"),       0.f, 1.f,  0.05f, TEXT("0..1"), 0.f } })
		{
			TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
			E->SetNumberField(TEXT("min"), K.Min);
			E->SetNumberField(TEXT("max"), K.Max);
			E->SetNumberField(TEXT("step"), K.Step);
			E->SetStringField(TEXT("unit"), K.Unit);
			E->SetNumberField(TEXT("default"), K.Default);
			M->SetObjectField(K.Key, E);
		}
		return M;
	}

	/** 전달된 키만 덮는다(부분 수정). sunColor 는 {x,y,z} = RGB(0~1). */
	void ApplyOptionalFields(const TSharedPtr<FJsonObject>& P, FLightSettings& S)
	{
		if (RpcParam::Has(P, TEXT("exposureEV100")))  S.ExposureEV100 = RpcParam::GetFloat(P, TEXT("exposureEV100"), S.ExposureEV100);
		if (RpcParam::Has(P, TEXT("sunIntensity")))   S.SunIntensity = RpcParam::GetFloat(P, TEXT("sunIntensity"), S.SunIntensity);
		if (RpcParam::Has(P, TEXT("sunAltitudeDeg"))) S.SunAltitudeDeg = RpcParam::GetFloat(P, TEXT("sunAltitudeDeg"), S.SunAltitudeDeg);
		if (RpcParam::Has(P, TEXT("sunAzimuthDeg")))  S.SunAzimuthDeg = RpcParam::GetFloat(P, TEXT("sunAzimuthDeg"), S.SunAzimuthDeg);
		if (RpcParam::Has(P, TEXT("skyIntensity")))   S.SkyIntensity = RpcParam::GetFloat(P, TEXT("skyIntensity"), S.SkyIntensity);
		// 채움광 2종은 UltraDynamicSky 레벨에서도 실제로 화면에 반영되는 유일한 조명 값이다.
		if (RpcParam::Has(P, TEXT("shadowFillIntensity"))) S.ShadowFillIntensity = RpcParam::GetFloat(P, TEXT("shadowFillIntensity"), S.ShadowFillIntensity);
		if (RpcParam::Has(P, TEXT("carFillIntensity")))    S.CarFillIntensity = RpcParam::GetFloat(P, TEXT("carFillIntensity"), S.CarFillIntensity);
		if (RpcParam::Has(P, TEXT("sunColor")))
		{
			const FVector C = RpcParam::GetVec3(P, TEXT("sunColor"), FVector(S.SunColor.R, S.SunColor.G, S.SunColor.B));
			S.SunColor = FLinearColor(static_cast<float>(C.X), static_cast<float>(C.Y), static_cast<float>(C.Z), 1.f);
		}
	}

	FString StripJson(const FString& Name)
	{
		return Name.EndsWith(TEXT(".json"), ESearchCase::IgnoreCase) ? Name.LeftChop(5) : Name;
	}
}

void FLightRpcModule::Register(URpcDispatcher& Dispatcher)
{
	// 현재 레벨 조명을 되읽는다. 태양을 못 찾으면 마지막 적용값으로 답한다.
	Dispatcher.Register(TEXT("light.get"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		if (!RejectUnknownKeys(P, { TEXT("withMeta") }, E)) { return nullptr; }
		ALightControlManager* Mgr = ALightControlManager::GetOrSpawn(GetWorldPtr());
		if (!Mgr) { E.FailDomain(TEXT("조명 매니저 없음(월드 미로드)")); return nullptr; }

		TSharedPtr<FJsonObject> O = StateDto(*Mgr);
		if (RpcParam::GetBool(P, TEXT("withMeta"), false))
		{
			O->SetObjectField(TEXT("meta"), MetaDto());
		}
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("light.get"), { /*bMutating=*/false, /*bDestructive=*/false,
		TEXT("withMeta?=false"),
		TEXT("조명 8값(exposureEV100 EV100·sunIntensity lux·sunAltitudeDeg 0..90°·sunAzimuthDeg 0..360°·skyIntensity·shadowFillIntensity lux·carFillIntensity lux·sunColor{x,y,z} 선형 0..1) + "
		     "fromWorld(태양 컴포넌트를 찾아 되읽음 - 화면 일치 보증이 아니다) · source(world|override|file) · fileName(마지막 적용·저장 파일, 없으면 null) · "
		     "externalSkySystem(true 면 UDS 같은 하늘 시스템이 태양·하늘빛·노출을 쥐고 있어 그 값들은 이 RPC 로 바뀌지 않고 되읽은 값도 화면과 다를 수 있다 - 채움광 2종만 유효). "
		     "withMeta:true 면 meta.<키>={min,max,step(UI 권장),unit,default(구조체 기본값)} 를 더한다. "
		     "sunAzimuthDeg = 빛이 나아가는(그림자가 향하는) 방향의 UE yaw: 0=+X, +Y 쪽으로 증가(위에서 볼 때 시계방향). 태양이 있는 방향이 아니다. "
		     "sunAltitudeDeg 는 0..90 으로 잘린다(지평선 아래 불가). exposureEV100 은 클수록 어둡고 자동 노출을 끈 고정값(min=max)이다. "
		     "shadowFillIntensity = 그림자 없는 보조 DirectionalLight(전 대상), carFillIntensity = 라이팅 채널 1 의 같은 종류(차량만). 스트림 카메라에 같게 보이는지는 측정하지 않았다") });

	// 전달한 항목만 바꿔 즉시 적용한다(가림률 측정 중 노출을 고정하는 용도).
	Dispatcher.Register(TEXT("light.set"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		if (!RejectUnknownKeys(P, { ValueKeys[0], ValueKeys[1], ValueKeys[2], ValueKeys[3], ValueKeys[4], ValueKeys[5], ValueKeys[6], ValueKeys[7],
			EnvValueKeys[0], EnvValueKeys[1], EnvValueKeys[2], EnvValueKeys[3], EnvValueKeys[4], EnvValueKeys[5] }, E)) { return nullptr; }
		ALightControlManager* Mgr = ALightControlManager::GetOrSpawn(GetWorldPtr());
		if (!Mgr) { E.FailDomain(TEXT("조명 매니저 없음(월드 미로드)")); return nullptr; }

		// 받았지만 이 레벨에서는 적용되지 않은 키를 이름으로 알린다(조용히 무시하지 않는다).
		TArray<TSharedPtr<FJsonValue>> Ignored;
		const FLightSupports Sup = Mgr->GetSupports();

		// 1) 시간대·날씨 먼저 — timeOfDay 가 태양을 정한 뒤 2) 의 태양 키가 있으면 그것이 이긴다.
		FLightEnv In;
		In.NightAmbient = -1.0f;   // 구조체 기본값(0.5)은 "보냄" 으로 읽히므로 전부 "안 보냄"(음수)에서 출발
		bool bAnyEnv = false;
		for (int32 i = 0; i < UE_ARRAY_COUNT(EnvValueKeys); ++i)
		{
			if (!RpcParam::Has(P, EnvValueKeys[i])) { continue; }
			const double V = RpcParam::GetFloat(P, EnvValueKeys[i], -1.0);
			if (!EnvKeySupported(Sup, i)) { Ignored.Add(MakeShared<FJsonValueString>(EnvValueKeys[i])); continue; }
			const float Hi = (i == 0) ? 24.0f : (i == 1 ? 5.0f : 1.0f);
			*EnvValueField(In, i) = FMath::Clamp(static_cast<float>(V), 0.0f, Hi);
			bAnyEnv = true;
		}
		if (bAnyEnv)
		{
			Mgr->ApplyEnv(In);
		}

		// 2) 기존 조명 값. 기준은 현재 월드 상태다 - 마지막 적용값에서 출발하면 패널로 바꾼 값을 되돌려 버린다.
		bool bAnyValue = false;
		for (const TCHAR* K : ValueKeys) { bAnyValue |= RpcParam::Has(P, K); }
		if (bAnyValue || !bAnyEnv)
		{
			FLightSettings S;
			if (!Mgr->CaptureCurrent(S)) { S = Mgr->GetLastApplied(); }
			ApplyOptionalFields(P, S);
			Mgr->ApplySettings(S);
		}
		if (P.IsValid() && P->Values.Num() > 0)
		{
			Mgr->SetSourceKind(TEXT("override"));
		}

		TSharedPtr<FJsonObject> O = StateDto(*Mgr);
		if (Mgr->UsesExternalSkySystem() && P.IsValid())
		{
			for (const auto& Pair : P->Values)
			{
				if (IsIgnoredOnExternalSky(FString(Pair.Key))) { Ignored.Add(MakeShared<FJsonValueString>(FString(Pair.Key))); }
			}
		}
		O->SetArrayField(TEXT("ignoredKeys"), Ignored);   // 항상 준다(빈 배열 = 전부 적용)
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("light.set"), { true, false,
		TEXT("exposureEV100? sunIntensity? sunAltitudeDeg? sunAzimuthDeg? skyIntensity? shadowFillIntensity? carFillIntensity? sunColor{x,y,z}?"),
		TEXT("보낸 키만 바꾸고 나머지는 지금 화면(월드에서 되읽은 값) 그대로 둔다. 범위는 light.get {withMeta:true}, 밖의 값은 잘린다. "
		     "응답은 light.get 모양의 적용 후 상태. 알 수 없는 키는 -32602(키 이름 포함). "
		     "UDS 같은 하늘 시스템 레벨에서는 태양·하늘빛·노출 키가 적용되지 않으며 응답 ignoredKeys 에 이름이 담긴다(채움광 2종만 적용). "
		     "하늘빛 재캡처는 태양·하늘빛이 실제로 바뀐 호출에서만 한다 - 노출·채움광만 바꾸는 호출은 싸다") });

	// 기동 때 시작 조명이 적용된 상태로 되돌린다.
	Dispatcher.Register(TEXT("light.reset"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		if (!RejectUnknownKeys(P, {}, E)) { return nullptr; }
		ALightControlManager* Mgr = ALightControlManager::GetOrSpawn(GetWorldPtr());
		if (!Mgr) { E.FailDomain(TEXT("조명 매니저 없음(월드 미로드)")); return nullptr; }

		const bool bReset = Mgr->ResetToBaseline();
		TSharedPtr<FJsonObject> O = StateDto(*Mgr);
		O->SetBoolField(TEXT("reset"), bReset);
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("light.reset"), { true, false, TEXT(""),
		TEXT("RPC·패널·light.load 로 바꾼 조명을 버리고 기동 직후 화면(시작 조명 파일까지 적용된 상태)으로 돌아간다. 노출 볼륨 override 도 그때 상태로 복원. "
		     "응답은 light.get 모양 + reset(false = 한 번도 적용한 적이 없어 할 일이 없었음). 기동 시 파일은 light.setDefault/config light_file 이 정하며 그것과 별개다") });

	Dispatcher.Register(TEXT("light.save"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		if (!RejectUnknownKeys(P, { TEXT("fullPath"), TEXT("fileName") }, E)) { return nullptr; }
		ALightControlManager* Mgr = ALightControlManager::GetOrSpawn(GetWorldPtr());
		if (!Mgr) { E.FailDomain(TEXT("조명 매니저 없음(월드 미로드)")); return nullptr; }

		FLightSettings S;
		if (!Mgr->CaptureCurrent(S)) { S = Mgr->GetLastApplied(); }

		const FString Path = ResolveLightPath(P);
		// 시간대·날씨도 같은 파일에(선택 키). 옛 파일·패널 저장분은 이 키가 없고 읽을 때 건드리지 않는다.
		if (!ULightControlLibrary::SaveToFile(Path, S, Mgr->GetEnv()))
		{
			E.FailDomain(FString::Printf(TEXT("조명 저장 실패: %s"), *Path));
			return nullptr;
		}
		Mgr->SetCurrentFileName(FPaths::GetCleanFilename(Path));

		TSharedPtr<FJsonObject> O = StateDto(*Mgr);
		O->SetBoolField(TEXT("ok"), true);
		O->SetStringField(TEXT("path"), Path);
		O->SetStringField(TEXT("fileName"), FPaths::GetCleanFilename(Path));
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("light.save"), { true, false, TEXT("fullPath? | fileName?=LightSettings"),
		TEXT("현재 조명(light.get 과 같은 값)을 Save/3D/Light/<fileName>.json 에 쓴다. 같은 이름이면 덮어쓴다. 응답은 light.get 모양 + ok·path·fileName") });

	// 파일을 읽어 즉시 적용한다. apply=false 면 값만 돌려주고 월드는 건드리지 않는다.
	Dispatcher.Register(TEXT("light.load"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		if (!RejectUnknownKeys(P, { TEXT("fullPath"), TEXT("fileName"), TEXT("apply") }, E)) { return nullptr; }
		ALightControlManager* Mgr = ALightControlManager::GetOrSpawn(GetWorldPtr());
		if (!Mgr) { E.FailDomain(TEXT("조명 매니저 없음(월드 미로드)")); return nullptr; }

		const FString Path = ResolveLightPath(P);
		FLightSettings S;
		FLightEnv FileEnv;
		bool bHasEnv = false;
		if (!ULightControlLibrary::LoadFromFile(Path, S, FileEnv, bHasEnv))
		{
			E.FailDomain(FString::Printf(TEXT("조명 로드 실패: %s"), *Path));
			return nullptr;
		}
		const bool bApply = RpcParam::GetBool(P, TEXT("apply"), true);
		if (bApply)
		{
			Mgr->ApplySettings(S);
			// 파일 시각이 있으면 태양은 시각이 다시 정한다(파일의 태양 값은 그 시각에서 저장된 값이라 같다).
			if (bHasEnv) { Mgr->ApplyEnv(FileEnv); }
			Mgr->SetSourceKind(TEXT("file"));
			Mgr->SetCurrentFileName(FPaths::GetCleanFilename(Path));
		}

		// apply=false 면 월드가 그대로이므로 상태는 현재 그대로, 읽은 파일 값은 file 필드로 따로 준다.
		TSharedPtr<FJsonObject> O = StateDto(*Mgr);
		O->SetBoolField(TEXT("ok"), true);
		O->SetBoolField(TEXT("applied"), bApply);
		if (!bApply)
		{
			TSharedPtr<FJsonObject> F = SettingsToDto(S);
			if (bHasEnv) { EnvToDto(F, FileEnv); }
			O->SetObjectField(TEXT("file"), F);
		}
		O->SetStringField(TEXT("loadedFileName"), FPaths::GetCleanFilename(Path));
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("light.load"), { true, false, TEXT("fullPath? | fileName? apply?=true"),
		TEXT("Save/3D/Light/<fileName>.json 을 읽어 적용한다. apply:false 면 월드를 건드리지 않고 파일 값을 응답 file 에만 담는다(이때 상태는 현재 그대로). "
		     "응답은 light.get 모양 + ok·applied·loadedFileName. 적용하면 source=file, fileName=그 파일. UDS 레벨 제약은 light.set 과 같다") });

	// 기동 시 적용되는 기본 파일 포인터를 갱신한다(다음 실행부터 이 설정으로 뜬다).
	Dispatcher.Register(TEXT("light.setDefault"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		if (!RejectUnknownKeys(P, { TEXT("fullPath"), TEXT("fileName") }, E)) { return nullptr; }
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
	Dispatcher.SetMethodMeta(TEXT("light.setDefault"), { true, false, TEXT("fullPath? | fileName?"),
		TEXT("리셋이 아니다 - '다음 기동 때 적용할 조명 파일' 포인터(Save/3D/Light/_default.txt)만 바꾼다. 지금 화면은 그대로이고 config light_file 이 있으면 그것이 우선한다. "
		     "지금 화면을 되돌리려면 light.reset") });

	// 조명 파일 삭제. 기본 파일(포인터 대상)은 거절한다.
	Dispatcher.Register(TEXT("light.deleteFile"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		if (!RejectUnknownKeys(P, { TEXT("fileName") }, E)) { return nullptr; }
		FString RawName;
		if (!RpcParam::RequireString(P, TEXT("fileName"), RawName, E)) { return nullptr; }

		// file.read 와 같은 검문 - 이름만(경로 구분자·'.' 시작 불가), .json, 접은 뒤에도 조명 폴더 안.
		const FString Name = RawName.TrimStartAndEnd();
		if (Name.IsEmpty() || Name.Contains(TEXT("/")) || Name.Contains(TEXT("\\")) || Name.StartsWith(TEXT(".")))
		{
			E.Fail(Park3DRpc::InvalidParams,
				FString::Printf(TEXT("fileName 이 잘못됐다 - 파일 이름만(경로·'.' 시작 불가): '%s'"), *RawName), ERpcErrorKind::BadParams);
			return nullptr;
		}
		const FString Dir = CarFilePaths::Normalize(ULightControlLibrary::GetLightDir());
		const FString Path = CarFilePaths::Normalize(Dir / (StripJson(Name) + TEXT(".json")));
		if (!CarFilePaths::IsInside(Path, Dir))
		{
			E.FailDomain(FString::Printf(TEXT("조명 폴더 밖입니다: %s"), *Path), ERpcErrorKind::BadParams);
			return nullptr;
		}

		const FString DefaultName = ULightControlLibrary::GetDefaultFileName();
		if (!DefaultName.IsEmpty() && StripJson(DefaultName).Equals(StripJson(FPaths::GetCleanFilename(Path)), ESearchCase::IgnoreCase))
		{
			E.FailDomain(FString::Printf(TEXT("기본 조명 파일은 지울 수 없다(_default.txt 가 가리킴): %s"), *FPaths::GetCleanFilename(Path)),
				ERpcErrorKind::BadParams);
			return nullptr;
		}

		const bool bExisted = IFileManager::Get().FileExists(*Path);
		if (bExisted && !IFileManager::Get().Delete(*Path, /*RequireExists=*/false, /*EvenReadOnly=*/false, /*Quiet=*/true))
		{
			E.FailDomain(FString::Printf(TEXT("삭제 실패: %s"), *Path));
			return nullptr;
		}
		// 지운 파일이 "지금 적용된 파일"로 남아 있으면 안 된다(light.get 의 fileName 이 없는 파일을 가리키게 된다).
		if (UWorld* W = GetWorldPtr())
		{
			if (ALightControlManager* Mgr = ALightControlManager::GetOrSpawn(W))
			{
				if (StripJson(Mgr->GetCurrentFileName()).Equals(StripJson(FPaths::GetCleanFilename(Path)), ESearchCase::IgnoreCase))
				{
					Mgr->SetCurrentFileName(FString());
				}
			}
		}
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetStringField(TEXT("fileName"), FPaths::GetCleanFilename(Path));
		O->SetBoolField(TEXT("existed"), bExisted);
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("light.deleteFile"), { true, true, TEXT("fileName"),
		TEXT("Save/3D/Light/<fileName>.json 하나를 지운다(이름만, .json 자동). _default.txt 가 가리키는 파일은 거절. 없는 파일은 existed:false 로 성공. 응답 {ok, fileName, existed}") });

	// ---- 가로등(보드 #1326 B2.1) — 레벨에 가로등 액터가 없어 만든다. 바꿀 때마다 Save/3D/Lamp/Lamp_<레벨>.json 에 쓴다 ----
	Dispatcher.Register(TEXT("lamp.create"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		if (!RejectUnknownKeys(P, { TEXT("pos"), TEXT("height"), TEXT("intensity"), TEXT("color"), TEXT("coneDeg"), TEXT("name"), TEXT("on") }, E)) { return nullptr; }
		UWorld* World = GetWorldPtr();
		if (!World) { E.FailDomain(TEXT("월드 미로드")); return nullptr; }
		const TSharedPtr<FJsonObject>* PosObj = nullptr;
		RpcParam::MarkRead(P, TEXT("pos"));
		double X = 0.0, Y = 0.0, Z = 0.0;
		if (!P.IsValid() || !P->TryGetObjectField(TEXT("pos"), PosObj) || !(*PosObj)->TryGetNumberField(TEXT("x"), X) || !(*PosObj)->TryGetNumberField(TEXT("y"), Y))
		{
			E.Fail(Park3DRpc::InvalidParams, TEXT("pos {x, y, z?} (m) 가 필요하다"), ERpcErrorKind::BadParams);
			return nullptr;
		}
		const bool bHasZ = (*PosObj)->TryGetNumberField(TEXT("z"), Z);
		const FVector Ground(X * 100.0, Y * 100.0, bHasZ ? Z * 100.0 : LampGroundZ(World, X * 100.0, Y * 100.0));
		const FVector C = RpcParam::GetVec3(P, TEXT("color"), FVector(1.0, 0.85, 0.65));
		const FString Name = ALotLampActor::MakeUniqueName(World, RpcParam::GetString(P, TEXT("name")));
		ALotLampActor* L = ALotLampActor::Spawn(World, Ground, Name,
			RpcParam::GetFloat(P, TEXT("height"), ALotLampActor::DefaultHeightM),
			RpcParam::GetFloat(P, TEXT("intensity"), ALotLampActor::DefaultIntensityCd),
			FLinearColor(C.X, C.Y, C.Z), RpcParam::GetFloat(P, TEXT("coneDeg"), ALotLampActor::DefaultConeDeg),
			RpcParam::GetBool(P, TEXT("on"), true));
		if (!L) { E.FailDomain(TEXT("가로등 생성 실패")); return nullptr; }
		FString Path;
		const bool bSaved = ALotLampActor::SaveAll(World, Path);
		TSharedPtr<FJsonObject> O = L->ToJson();
		O->SetBoolField(TEXT("ok"), true);
		O->SetBoolField(TEXT("saved"), bSaved);
		O->SetStringField(TEXT("file"), Path);
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("lamp.create"), { true, false,
		TEXT("pos{x,y,z?} m, height?=6 m, intensity?=150 cd, color?{x,y,z}=(1,0.85,0.65), coneDeg?=70, name?, on?=true"),
		TEXT("기둥+등기구+아래로 비추는 스폿(그림자 있음) 가로등 1개. z 생략 = 노면(정적 질의). name 이 없거나 겹치면 Lamp_<n>. "
		     "광량은 칸델라 — 이 프로젝트 태양 5 lux 에 맞춘 기본값(150 cd, 6 m 아래 약 4 lux). 레벨 파일에 저장되고 기동·레벨 전환 때 복원. 응답 = lamp.list 한 행 + ok·saved·file") });

	Dispatcher.Register(TEXT("lamp.list"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		if (!RejectUnknownKeys(P, {}, E)) { return nullptr; }
		UWorld* World = GetWorldPtr();
		if (!World) { E.FailDomain(TEXT("월드 미로드")); return nullptr; }
		return RpcDto::MakeObject(LampsDto(World));
	});
	Dispatcher.SetMethodMeta(TEXT("lamp.list"), { false, false, TEXT(""),
		TEXT("가로등 목록 {lamps:[{name,pos{x,y,z} m(지면 점),height,intensity cd,color{x,y,z},coneDeg,on}], count, file}") });

	Dispatcher.Register(TEXT("lamp.delete"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		if (!RejectUnknownKeys(P, { TEXT("name"), TEXT("names"), TEXT("all") }, E)) { return nullptr; }
		UWorld* World = GetWorldPtr();
		if (!World) { E.FailDomain(TEXT("월드 미로드")); return nullptr; }
		const bool bAll = RpcParam::GetBool(P, TEXT("all"), false);
		TArray<FString> Names;
		if (RpcParam::Has(P, TEXT("name"))) { Names.Add(RpcParam::GetString(P, TEXT("name"))); }
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		RpcParam::MarkRead(P, TEXT("names"));
		if (P.IsValid() && P->TryGetArrayField(TEXT("names"), Arr))
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr) { if (V.IsValid() && V->Type == EJson::String) { Names.Add(V->AsString()); } }
		}
		if (bAll == (Names.Num() > 0))
		{
			E.Fail(Park3DRpc::InvalidParams, TEXT("name | names[] | all:true 중 하나"), ERpcErrorKind::BadParams);
			return nullptr;
		}
		TArray<TSharedPtr<FJsonValue>> Deleted, NotFound;
		if (bAll)
		{
			for (ALotLampActor* L : ALotLampActor::GetAll(World)) { Deleted.Add(MakeShared<FJsonValueString>(L->GetLampName())); L->Destroy(); }
		}
		else
		{
			for (const FString& N : Names)
			{
				if (ALotLampActor* L = ALotLampActor::FindByName(World, N)) { Deleted.Add(MakeShared<FJsonValueString>(N)); L->Destroy(); }
				else { NotFound.Add(MakeShared<FJsonValueString>(N)); }
			}
		}
		FString Path;
		ALotLampActor::SaveAll(World, Path);
		TSharedPtr<FJsonObject> O = LampsDto(World);
		O->SetBoolField(TEXT("ok"), true);
		O->SetArrayField(TEXT("deleted"), Deleted);
		O->SetArrayField(TEXT("notFound"), NotFound);
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("lamp.delete"), { true, true, TEXT("name | names[] | all:true (하나)"),
		TEXT("가로등 삭제 후 레벨 파일 갱신(0개면 파일 삭제). 없는 이름은 notFound(실패 아님). 응답 = lamp.list 모양 + ok·deleted·notFound") });

	Dispatcher.Register(TEXT("light.lamps"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		if (!RejectUnknownKeys(P, { TEXT("on"), TEXT("intensity"), TEXT("color") }, E)) { return nullptr; }
		UWorld* World = GetWorldPtr();
		if (!World) { E.FailDomain(TEXT("월드 미로드")); return nullptr; }
		const bool bOn = RpcParam::Has(P, TEXT("on"));
		const bool bI = RpcParam::Has(P, TEXT("intensity"));
		const bool bC = RpcParam::Has(P, TEXT("color"));
		if (!bOn && !bI && !bC)
		{
			E.Fail(Park3DRpc::InvalidParams, TEXT("on · intensity · color 중 하나 이상"), ERpcErrorKind::BadParams);
			return nullptr;
		}
		const TArray<ALotLampActor*> All = ALotLampActor::GetAll(World);
		for (ALotLampActor* L : All)
		{
			if (bOn) { L->SetOn(RpcParam::GetBool(P, TEXT("on"))); }
			if (bI) { L->SetIntensity(RpcParam::GetFloat(P, TEXT("intensity"), L->GetIntensity())); }
			if (bC)
			{
				const FVector C = RpcParam::GetVec3(P, TEXT("color"), FVector(L->GetColor().R, L->GetColor().G, L->GetColor().B));
				L->SetColor(FLinearColor(C.X, C.Y, C.Z));
			}
		}
		FString Path;
		ALotLampActor::SaveAll(World, Path);
		TSharedPtr<FJsonObject> O = LampsDto(World);
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("changedCount"), All.Num());
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("light.lamps"), { true, false, TEXT("on?: bool, intensity?: cd, color?{x,y,z} (하나 이상)"),
		TEXT("가로등 전부를 한 번에 켜고/끄고/세기·색 지정(절대값). 가로등이 없으면 changedCount 0. 레벨 파일 갱신. 응답 = lamp.list 모양 + ok·changedCount") });
}
