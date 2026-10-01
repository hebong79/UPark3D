// Copyright Epic Games, Inc. All Rights Reserved.

#include "LightRpcModule.h"
#include "CarFilePaths.h"
#include "../RpcDispatcher.h"
#include "../RpcParamUtil.h"
#include "../../Light/LightControlManager.h"
#include "../../Light/LightControlLibrary.h"
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
		if (!RejectUnknownKeys(P, { ValueKeys[0], ValueKeys[1], ValueKeys[2], ValueKeys[3], ValueKeys[4], ValueKeys[5], ValueKeys[6], ValueKeys[7] }, E)) { return nullptr; }
		ALightControlManager* Mgr = ALightControlManager::GetOrSpawn(GetWorldPtr());
		if (!Mgr) { E.FailDomain(TEXT("조명 매니저 없음(월드 미로드)")); return nullptr; }

		// 기준은 현재 월드 상태다 - 마지막 적용값에서 출발하면 패널로 바꾼 값을 되돌려 버린다.
		FLightSettings S;
		if (!Mgr->CaptureCurrent(S)) { S = Mgr->GetLastApplied(); }
		ApplyOptionalFields(P, S);
		Mgr->ApplySettings(S);
		if (P.IsValid() && P->Values.Num() > 0)
		{
			Mgr->SetSourceKind(TEXT("override"));
		}

		TSharedPtr<FJsonObject> O = StateDto(*Mgr);
		// 받았지만 이 레벨에서는 적용되지 않은 키를 이름으로 알린다(조용히 무시하지 않는다).
		if (Mgr->UsesExternalSkySystem() && P.IsValid())
		{
			TArray<TSharedPtr<FJsonValue>> Ignored;
			for (const auto& Pair : P->Values)
			{
				if (IsIgnoredOnExternalSky(FString(Pair.Key))) { Ignored.Add(MakeShared<FJsonValueString>(FString(Pair.Key))); }
			}
			O->SetArrayField(TEXT("ignoredKeys"), Ignored);
		}
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
		if (!ULightControlLibrary::SaveToFile(Path, S))
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
		if (!ULightControlLibrary::LoadFromFile(Path, S))
		{
			E.FailDomain(FString::Printf(TEXT("조명 로드 실패: %s"), *Path));
			return nullptr;
		}
		const bool bApply = RpcParam::GetBool(P, TEXT("apply"), true);
		if (bApply)
		{
			Mgr->ApplySettings(S);
			Mgr->SetSourceKind(TEXT("file"));
			Mgr->SetCurrentFileName(FPaths::GetCleanFilename(Path));
		}

		// apply=false 면 월드가 그대로이므로 상태는 현재 그대로, 읽은 파일 값은 file 필드로 따로 준다.
		TSharedPtr<FJsonObject> O = StateDto(*Mgr);
		O->SetBoolField(TEXT("ok"), true);
		O->SetBoolField(TEXT("applied"), bApply);
		if (!bApply) { O->SetObjectField(TEXT("file"), SettingsToDto(S)); }
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
}
