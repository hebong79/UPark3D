// Copyright Epic Games, Inc. All Rights Reserved.

#include "SceneRpcModule.h"
#include "../RpcDispatcher.h"
#include "../RpcParamUtil.h"
#include "../../Config/Park3DAppConfig.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"

namespace
{
	/**
	 * 장소의 데이터 파일 세트 — 항목에 키가 있으면 그 값, 없으면 최상위 공통 값(OmiPark3D scene_files 의 폴백과 같은 규칙,
	 * 기동 시 ApplyLevelOverrides 가 실제로 쓰는 값이기도 하다). 빈 문자열은 "이 장소는 그 파일을 안 쓴다"는 뜻이라 그대로 낸다.
	 */
	void FillSceneFiles(const FPark3DAppConfig& Config, const FPark3DLevelOption* Opt, const TSharedPtr<FJsonObject>& O)
	{
		O->SetStringField(TEXT("presetFile"),    Opt && Opt->PresetFile.IsSet()    ? Opt->PresetFile.GetValue()    : Config.PresetFile);
		O->SetStringField(TEXT("carPosFile"),    Opt && Opt->CarPosFile.IsSet()    ? Opt->CarPosFile.GetValue()    : Config.CarPosFile);
		O->SetStringField(TEXT("cameraPosFile"), Opt && Opt->CameraPosFile.IsSet() ? Opt->CameraPosFile.GetValue() : Config.CameraPosFile);
	}

	/** 지금 떠 있는 레벨과 같은 levels[] 항목(첫 것). 없으면 nullptr — 공통 폴백으로 기동한 상태. */
	const FPark3DLevelOption* FindActive(const FPark3DAppConfig& Config, const FString& CurrentLevelPath)
	{
		if (CurrentLevelPath.IsEmpty())
		{
			return nullptr;
		}
		for (const FPark3DLevelOption& Opt : Config.Levels)
		{
			if (UPark3DAppConfigLibrary::NormalizeLevelPath(Opt.Level).Equals(CurrentLevelPath, ESearchCase::IgnoreCase))
			{
				return &Opt;
			}
		}
		return nullptr;
	}
}

void FSceneRpcModule::Register(URpcDispatcher& Dispatcher)
{
	// scene.list — config levels[] 의 장소 목록 + 지금 떠 있는 장소.
	// → {scenes:[{name, level(설정 표기), levelPath(정규화), active, presetFile, carPosFile, cameraPosFile}],
	//    current(활성 장소 이름, 없으면 ""), level(지금 레벨 패키지 경로), files{presetFile, carPosFile, cameraPosFile}}
	// config 파일이 없으면 빈 목록이다(오류가 아니다 — 콤보도 그때 뜨지 않는다).
	Dispatcher.Register(TEXT("scene.list"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		FPark3DAppConfig Config;
		UPark3DAppConfigLibrary::Load(Config);
		const FString Current = UPark3DAppConfigLibrary::GetCurrentLevelPath(GetWorldPtr());
		const FPark3DLevelOption* Active = FindActive(Config, Current);

		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FPark3DLevelOption& Opt : Config.Levels)
		{
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("name"), Opt.Name);
			Row->SetStringField(TEXT("level"), Opt.Level);
			Row->SetStringField(TEXT("levelPath"), UPark3DAppConfigLibrary::NormalizeLevelPath(Opt.Level));
			Row->SetBoolField(TEXT("active"), &Opt == Active);
			FillSceneFiles(Config, &Opt, Row);
			Arr.Add(MakeShared<FJsonValueObject>(Row));
		}

		TSharedPtr<FJsonObject> Files = MakeShared<FJsonObject>();
		FillSceneFiles(Config, Active, Files);

		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetArrayField(TEXT("scenes"), Arr);
		Root->SetStringField(TEXT("current"), Active ? Active->Name : FString());
		Root->SetStringField(TEXT("level"), Current);
		Root->SetObjectField(TEXT("files"), Files);
		return RpcDto::MakeObject(Root);
	});

	// scene.load {name} — 그 장소의 레벨로 옮겨 간다(주차장 선택 콤보와 같은 경로). 같은 레벨이면 다시 연다(재로드 = OmiPark3D
	// 의 reset 재붓기에 해당). → {ok, name, level(정규화 경로), reload, files}
	Dispatcher.Register(TEXT("scene.load"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		FString Name;
		if (!RpcParam::RequireString(P, TEXT("name"), Name, E)) return nullptr;
		UWorld* World = GetWorldPtr();
		if (!World) { E.FailDomain(TEXT("월드 없음(맵 미로드)")); return nullptr; }

		FPark3DAppConfig Config;
		UPark3DAppConfigLibrary::Load(Config);
		const FPark3DLevelOption* Opt = Config.Levels.FindByPredicate([&Name](const FPark3DLevelOption& O) { return O.Name == Name; });
		if (!Opt)
		{
			E.FailDomain(FString::Printf(TEXT("장소 없음: name=\"%s\" — config_pmaker.json levels[] 에 없다"), *Name));
			return nullptr;
		}

		const FString Target = UPark3DAppConfigLibrary::NormalizeLevelPath(Opt->Level);
		// 패키지에 없는 레벨(재쿡 전 pak 에 빠진 LV_Park_03 등)로 OpenLevel 하면 travel failure 뒤 엔진이 통째로 죽는다
		// (Work13520 실측: LoadMap "?closed" 에서 Fatal). RPC 한 번으로 시뮬레이터가 내려가면 안 되므로 먼저 확인한다.
		if (!FPackageName::DoesPackageExist(Target))
		{
			E.FailDomain(FString::Printf(TEXT("레벨 패키지 없음: %s — 이 빌드(pak)에 쿠킹되지 않았다. MapsToCook 확인 후 재패키징"), *Target));
			return nullptr;
		}
		const FString Current = UPark3DAppConfigLibrary::GetCurrentLevelPath(World);
		const bool bReload = Current.Equals(Target, ESearchCase::IgnoreCase);
		UE_LOG(LogTemp, Log, TEXT("[Scene] 장소 전환: '%s' → %s (현재 %s%s, 출처: config_pmaker.json levels)"),
			*Name, *Target, *Current, bReload ? TEXT(", 같은 레벨 재로드") : TEXT(""));

		// OpenLevel 은 이동을 예약만 한다 — 이 응답이 먼저 나가고 다음 틱에 월드가 바뀐다.
		UGameplayStatics::OpenLevel(World, FName(*Target));

		TSharedPtr<FJsonObject> Files = MakeShared<FJsonObject>();
		FillSceneFiles(Config, Opt, Files);
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("ok"), true);
		Root->SetStringField(TEXT("name"), Name);
		Root->SetStringField(TEXT("level"), Target);
		Root->SetBoolField(TEXT("reload"), bReload);
		Root->SetObjectField(TEXT("files"), Files);
		return RpcDto::MakeObject(Root);
	});
}
