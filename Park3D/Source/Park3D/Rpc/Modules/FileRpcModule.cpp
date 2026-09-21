// Copyright Epic Games, Inc. All Rights Reserved.

#include "FileRpcModule.h"
#include "CarFilePaths.h"
#include "../RpcDispatcher.h"
#include "../RpcParamUtil.h"
#include "../../Park3DDataPaths.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"

namespace
{
	/** kind → Save/3D 하위 폴더. 순서가 file.list(kind 생략) 의 출력 순서다(OmiPark3D KINDS 와 같다). */
	struct FKindEntry
	{
		const TCHAR* Kind;
		const TCHAR* SubDir;
	};
	const FKindEntry Kinds[] = {
		{ TEXT("car"),    TEXT("CarPos") },
		{ TEXT("camera"), TEXT("CameraPos") },
		{ TEXT("preset"), TEXT("Preset") },
	};

	/** kind 정규화(대소문자 무시 + 폴더 이름 별칭). 실패면 OutError(-32000). */
	bool ResolveKind(const FString& Raw, FString& OutKind, FRpcError& OutError)
	{
		FString K = Raw.TrimStartAndEnd().ToLower();
		if (K == TEXT("carpos")) K = TEXT("car");
		else if (K == TEXT("camerapos") || K == TEXT("campos")) K = TEXT("camera");
		for (const FKindEntry& E : Kinds)
		{
			if (K == E.Kind)
			{
				OutKind = E.Kind;
				return true;
			}
		}
		OutError.FailDomain(FString::Printf(TEXT("kind 가 잘못됐다 — car | camera | preset 중 하나: '%s'"), *Raw));
		return false;
	}

	const TCHAR* SubDirOf(const FString& Kind)
	{
		for (const FKindEntry& E : Kinds)
		{
			if (Kind == E.Kind) return E.SubDir;
		}
		return TEXT("");
	}

	/** kind 의 폴더(정규화된 절대 경로, 슬래시 통일). */
	FString KindDir(const FString& Kind)
	{
		return CarFilePaths::Normalize(FPaths::Combine(Park3DDataPaths::GetSaveRootDir(), TEXT("3D"), SubDirOf(Kind)));
	}

	FString JsonName(const FString& Raw)
	{
		return Raw.EndsWith(TEXT(".json"), ESearchCase::IgnoreCase) ? Raw : Raw + TEXT(".json");
	}

	/**
	 * fileName 검문 — 이름만 받는다(경로 구분자·'.' 시작 불가), .json 을 붙이고, 접은 뒤에도 폴더 안인지 본다.
	 * 마지막 검사는 방어선이다: 구분자 검사만으로도 빠져나갈 수 없지만 규칙이 바뀌어도 폴더 밖은 못 읽게.
	 */
	bool ResolveFileInDir(const FString& Dir, const FString& RawName, FString& OutPath, FRpcError& OutError)
	{
		const FString Name = RawName.TrimStartAndEnd();
		if (Name.IsEmpty() || Name.Contains(TEXT("/")) || Name.Contains(TEXT("\\")) || Name.StartsWith(TEXT(".")))
		{
			OutError.FailDomain(FString::Printf(TEXT("fileName 이 잘못됐다 — 파일 이름만(경로·'.' 시작 불가): '%s'"), *RawName));
			return false;
		}
		OutPath = CarFilePaths::Normalize(Dir / JsonName(Name));
		if (!CarFilePaths::IsInside(OutPath, Dir))
		{
			OutError.FailDomain(FString::Printf(TEXT("데이터 폴더 밖입니다: %s (허용: %s)"), *OutPath, *Dir));
			return false;
		}
		return true;
	}

	/** 파일 → JSON 루트 객체. 읽기 실패·파싱 실패·루트가 객체가 아니면 nullptr(OmiPark3D read_json_object 동일). */
	TSharedPtr<FJsonObject> ReadJsonObject(const FString& Path)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			return nullptr;
		}
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		TSharedPtr<FJsonValue> Root;
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid() || Root->Type != EJson::Object)
		{
			return nullptr;
		}
		return Root->AsObject();
	}

	/** 루트 datas[] 길이(차량 대수·카메라 대수·프리셋 수). 없으면 null. */
	TSharedPtr<FJsonValue> EntryCount(const TSharedPtr<FJsonObject>& Doc)
	{
		const TArray<TSharedPtr<FJsonValue>>* Datas = nullptr;
		if (Doc.IsValid() && Doc->TryGetArrayField(TEXT("datas"), Datas))
		{
			return MakeShared<FJsonValueNumber>(Datas->Num());
		}
		return MakeShared<FJsonValueNull>();
	}

	/** 파일 수정 시각(로컬, 초 단위 ISO — 파이썬 isoformat(timespec="seconds") 와 같은 모양). */
	FString ModifiedIso(const FString& Path)
	{
		const FDateTime Utc = IFileManager::Get().GetTimeStamp(*Path);
		if (Utc == FDateTime::MinValue())
		{
			return FString();
		}
		const FDateTime Local = Utc + (FDateTime::Now() - FDateTime::UtcNow());
		return Local.ToString(TEXT("%Y-%m-%dT%H:%M:%S"));
	}

	TSharedPtr<FJsonObject> FileRow(const FString& Path, bool bWithContent)
	{
		const TSharedPtr<FJsonObject> Doc = ReadJsonObject(Path);
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("fileName"), FPaths::GetCleanFilename(Path));
		O->SetStringField(TEXT("path"), Path);
		O->SetNumberField(TEXT("sizeBytes"), static_cast<double>(IFileManager::Get().FileSize(*Path)));
		O->SetStringField(TEXT("modified"), ModifiedIso(Path));
		O->SetField(TEXT("entries"), EntryCount(Doc));
		if (bWithContent)
		{
			if (Doc.IsValid()) O->SetObjectField(TEXT("content"), Doc);
			else               O->SetField(TEXT("content"), MakeShared<FJsonValueNull>());
		}
		return O;
	}

	TSharedPtr<FJsonObject> KindBlock(const FString& Kind, bool bWithContent)
	{
		const FString Dir = KindDir(Kind);
		TArray<FString> Names;
		IFileManager::Get().FindFiles(Names, *(Dir / TEXT("*.json")), /*Files=*/true, /*Directories=*/false);
		Names.Sort([](const FString& A, const FString& B) { return A.ToLower() < B.ToLower(); });

		TArray<TSharedPtr<FJsonValue>> Files;
		for (const FString& N : Names)
		{
			Files.Add(MakeShared<FJsonValueObject>(FileRow(Dir / N, bWithContent)));
		}

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("dir"), Dir);
		// OmiPark3D 는 AppConfig.carposFile/cameraposFile/presetFile(지금 장소에 적용된 파일)을 낸다.
		// 언리얼 config_pmaker.json 에는 그 항목이 없어 항상 빈 문자열이다(키는 계약대로 남긴다).
		O->SetStringField(TEXT("current"), FString());
		O->SetArrayField(TEXT("files"), Files);
		return O;
	}
}

void FFileRpcModule::Register(URpcDispatcher& Dispatcher)
{
	Dispatcher.Register(TEXT("file.list"), [](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		TArray<FString> Wanted;
		const FString Raw = RpcParam::GetString(P, TEXT("kind"));
		if (!Raw.IsEmpty())
		{
			FString Kind;
			if (!ResolveKind(Raw, Kind, E)) return nullptr;
			Wanted.Add(Kind);
		}
		else
		{
			for (const FKindEntry& K : Kinds) { Wanted.Add(K.Kind); }
		}
		const bool bWithContent = RpcParam::GetBool(P, TEXT("withContent"), false);

		TSharedPtr<FJsonObject> KindsObj = MakeShared<FJsonObject>();
		for (const FString& K : Wanted)
		{
			KindsObj->SetObjectField(K, KindBlock(K, bWithContent));
		}
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetObjectField(TEXT("kinds"), KindsObj);
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("file.list"), { /*bMutating=*/false, /*bDestructive=*/false,
		TEXT("kind?(car|camera|preset) withContent?=false"),
		TEXT("Save/3D/{CarPos,CameraPos,Preset} 폴더의 *.json 목록 — kind 를 비우면 셋 다. kinds.<kind> = {dir, current, files:[{fileName, path, sizeBytes, modified, entries(datas 길이)}]}. withContent:true 면 파일 JSON 을 content 로 함께 낸다. 읽기 전용") });

	Dispatcher.Register(TEXT("file.read"), [](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		FString RawKind, RawName;
		if (!RpcParam::RequireString(P, TEXT("kind"), RawKind, E)) return nullptr;
		if (!RpcParam::RequireString(P, TEXT("fileName"), RawName, E)) return nullptr;
		FString Kind;
		if (!ResolveKind(RawKind, Kind, E)) return nullptr;

		FString Path;
		if (!ResolveFileInDir(KindDir(Kind), RawName, Path, E)) return nullptr;
		if (!IFileManager::Get().FileExists(*Path))
		{
			E.FailDomain(FString::Printf(TEXT("파일 없음: %s"), *Path));
			return nullptr;
		}
		const TSharedPtr<FJsonObject> Doc = ReadJsonObject(Path);
		if (!Doc.IsValid())
		{
			E.FailDomain(FString::Printf(TEXT("JSON 객체가 아니다(파싱 실패 또는 루트가 객체가 아님): %s"), *Path));
			return nullptr;
		}

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("kind"), Kind);
		O->SetStringField(TEXT("fileName"), FPaths::GetCleanFilename(Path));
		O->SetStringField(TEXT("path"), Path);
		O->SetField(TEXT("entries"), EntryCount(Doc));
		O->SetObjectField(TEXT("content"), Doc);
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("file.read"), { false, false, TEXT("kind fileName"),
		TEXT("파일 하나의 JSON(content) — 적용은 car.load {path, fileName} · cam.loadPosFile {fileName} · preset.load {path, fileName}") });
}
