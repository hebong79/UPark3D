// Copyright Epic Games, Inc. All Rights Reserved.
// FileSystemRpcTest : file.* / preset.importFile / system.describe(디스패처 메타) 검증 — OmiPark3D 이식분.
// HTTP 없이 디스패처를 직접 호출한다. 월드가 필요 없는 핸들러들이라 에디터 월드 유무와 무관하게 돈다.
// 실제 Save/3D 폴더에 쓰는 테스트는 `_AutomationTest_` 접두 이름으로 만들고 끝에 지운다(잔해 금지).

#include "Misc/AutomationTest.h"
#include "../Rpc/RpcDispatcher.h"
#include "../Rpc/Modules/FileRpcModule.h"
#include "../Rpc/Modules/PresetRpcModule.h"
#include "../Rpc/Modules/CarFilePaths.h"
#include "../Park3DDataPaths.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FString DataFile(const TCHAR* SubDir, const TCHAR* Name)
	{
		return CarFilePaths::Normalize(Park3DDataPaths::GetDataFilePath(SubDir, Name));
	}

	TSharedPtr<FJsonObject> ObjOf(const TSharedPtr<FJsonValue>& V)
	{
		return (V.IsValid() && V->Type == EJson::Object) ? V->AsObject() : nullptr;
	}

	/** kinds.<kind>.files 배열(없으면 nullptr). */
	const TArray<TSharedPtr<FJsonValue>>* FilesOf(const TSharedPtr<FJsonValue>& ListResult, const TCHAR* Kind)
	{
		const TSharedPtr<FJsonObject> Root = ObjOf(ListResult);
		const TSharedPtr<FJsonObject>* KindsObj = nullptr;
		if (!Root.IsValid() || !Root->TryGetObjectField(TEXT("kinds"), KindsObj)) return nullptr;
		const TSharedPtr<FJsonObject>* Block = nullptr;
		if (!(*KindsObj)->TryGetObjectField(Kind, Block)) return nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Files = nullptr;
		(*Block)->TryGetArrayField(TEXT("files"), Files);
		return Files;
	}

	const TSharedPtr<FJsonObject>* FindRow(const TArray<TSharedPtr<FJsonValue>>* Files, const FString& FileName)
	{
		if (!Files) return nullptr;
		for (const TSharedPtr<FJsonValue>& V : *Files)
		{
			const TSharedPtr<FJsonObject>* O = nullptr;
			if (V->TryGetObject(O) && (*O)->GetStringField(TEXT("fileName")) == FileName) return O;
		}
		return nullptr;
	}
}

// ===== file.list: kind 별 배열, kind 생략 시 셋 다, 잘못된 kind 는 -32000 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcFileModuleListTest,
	"Park3D.Rpc.FileModule.List",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcFileModuleListTest::RunTest(const FString& Parameters)
{
	URpcDispatcher* D = NewObject<URpcDispatcher>();
	FFileRpcModule File([]() -> UWorld* { return nullptr; });
	File.Register(*D);

	// kind 생략 → car/camera/preset 세 블록 모두, 각각 files 배열.
	TSharedPtr<FJsonValue> All; FRpcError E;
	TestTrue(TEXT("file.list(전체) 성공"), D->Dispatch(TEXT("file.list"), nullptr, All, E));
	for (const TCHAR* Kind : { TEXT("car"), TEXT("camera"), TEXT("preset") })
	{
		TestNotNull(*FString::Printf(TEXT("kinds.%s.files 배열"), Kind), FilesOf(All, Kind));
	}

	// kind 지정 → 그 블록 하나만. 별칭·대소문자도 같은 블록.
	for (const TCHAR* Raw : { TEXT("car"), TEXT("CarPos"), TEXT("CAMERA"), TEXT("campos"), TEXT("preset") })
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>(); P->SetStringField(TEXT("kind"), Raw);
		TSharedPtr<FJsonValue> R; FRpcError E2;
		TestTrue(*FString::Printf(TEXT("file.list kind=%s 성공"), Raw), D->Dispatch(TEXT("file.list"), P, R, E2));
		const TSharedPtr<FJsonObject> Root = ObjOf(R);
		const TSharedPtr<FJsonObject>* Kinds = nullptr;
		if (Root.IsValid() && Root->TryGetObjectField(TEXT("kinds"), Kinds))
		{
			TestEqual(*FString::Printf(TEXT("kind=%s 는 블록 하나"), Raw), (*Kinds)->Values.Num(), 1);
		}
		else { AddError(FString::Printf(TEXT("kind=%s: kinds 객체 없음"), Raw)); }
	}

	// 블록 모양: dir(절대 경로) · current(문자열) · files.
	{
		const TSharedPtr<FJsonObject> Root = ObjOf(All);
		const TSharedPtr<FJsonObject>* Kinds = nullptr; const TSharedPtr<FJsonObject>* Car = nullptr;
		if (Root.IsValid() && Root->TryGetObjectField(TEXT("kinds"), Kinds) && (*Kinds)->TryGetObjectField(TEXT("car"), Car))
		{
			const FString Dir = (*Car)->GetStringField(TEXT("dir"));
			TestTrue(TEXT("car.dir 는 CarPos 폴더"), Dir.EndsWith(TEXT("/3D/CarPos")));
			TestTrue(TEXT("car.current 키 존재"), (*Car)->HasField(TEXT("current")));
		}
	}

	// 잘못된 kind → -32000.
	TSharedPtr<FJsonObject> Bad = MakeShared<FJsonObject>(); Bad->SetStringField(TEXT("kind"), TEXT("nope"));
	TSharedPtr<FJsonValue> BadR; FRpcError BadE;
	TestFalse(TEXT("잘못된 kind 거부"), D->Dispatch(TEXT("file.list"), Bad, BadR, BadE));
	TestEqual(TEXT("kind 오류 코드 -32000"), BadE.Code, Park3DRpc::Domain);
	return true;
}

// ===== file.read 검문: 경로 구분자·'.' 시작·폴더 밖 거부, .json 아닌 이름은 .json 을 붙여 찾는다(없으면 실패) =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcFileModuleReadGuardTest,
	"Park3D.Rpc.FileModule.ReadGuard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcFileModuleReadGuardTest::RunTest(const FString& Parameters)
{
	URpcDispatcher* D = NewObject<URpcDispatcher>();
	FFileRpcModule File([]() -> UWorld* { return nullptr; });
	File.Register(*D);

	auto Read = [&](const TCHAR* Kind, const TCHAR* Name, FRpcError& OutE) -> bool
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		if (Kind) P->SetStringField(TEXT("kind"), Kind);
		if (Name) P->SetStringField(TEXT("fileName"), Name);
		TSharedPtr<FJsonValue> R;
		return D->Dispatch(TEXT("file.read"), P, R, OutE);
	};

	FRpcError E;
	TestFalse(TEXT("역슬래시 `..\\x.json` 거부"), Read(TEXT("car"), TEXT("..\\x.json"), E));
	TestEqual(TEXT("코드 -32000"), E.Code, Park3DRpc::Domain);
	TestTrue(TEXT("사유에 fileName"), E.Message.Contains(TEXT("fileName")));

	FRpcError E2;
	TestFalse(TEXT("슬래시 `../x.json` 거부"), Read(TEXT("car"), TEXT("../x.json"), E2));
	FRpcError E3;
	TestFalse(TEXT("`.` 시작 이름 거부"), Read(TEXT("preset"), TEXT(".hidden.json"), E3));
	FRpcError E4;
	TestFalse(TEXT("절대 경로 거부"), Read(TEXT("camera"), TEXT("C:/Windows/win.ini"), E4));
	FRpcError E5;
	TestFalse(TEXT("빈 이름 거부"), Read(TEXT("car"), TEXT("   "), E5));

	// .json 이 아닌 이름은 .json 을 붙여 찾는다(파이썬 json_name) → 그런 파일은 없으므로 '파일 없음'.
	FRpcError E6;
	TestFalse(TEXT("non-json 이름은 .json 붙여 찾고 없으면 실패"), Read(TEXT("car"), TEXT("_AutomationTest_NoSuch.txt"), E6));
	TestTrue(TEXT("사유 = 파일 없음 …NoSuch.txt.json"), E6.Message.Contains(TEXT("_AutomationTest_NoSuch.txt.json")));

	// 필수 파라미터 누락.
	FRpcError E7;
	TestFalse(TEXT("kind 누락 거부"), Read(nullptr, TEXT("x"), E7));
	FRpcError E8;
	TestFalse(TEXT("fileName 누락 거부"), Read(TEXT("car"), nullptr, E8));
	return true;
}

// ===== file.read 왕복: CarPos 에 임시 파일을 쓰고 read/list 로 돌려받는다 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcFileModuleReadRoundTripTest,
	"Park3D.Rpc.FileModule.ReadRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcFileModuleReadRoundTripTest::RunTest(const FString& Parameters)
{
	const FString Path = DataFile(TEXT("CarPos"), TEXT("_AutomationTest_FileRead.json"));
	TestTrue(TEXT("임시 배치 파일 생성"),
		FFileHelper::SaveStringToFile(TEXT(R"({"isUnreal":true,"datas":[{"id":"a"},{"id":"b"},{"id":"c"}]})"), *Path,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));

	URpcDispatcher* D = NewObject<URpcDispatcher>();
	FFileRpcModule File([]() -> UWorld* { return nullptr; });
	File.Register(*D);

	// read — 확장자 생략 가능, entries = datas 길이, content = 파싱한 JSON.
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("kind"), TEXT("car"));
		P->SetStringField(TEXT("fileName"), TEXT("_AutomationTest_FileRead"));
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestTrue(TEXT("file.read 성공"), D->Dispatch(TEXT("file.read"), P, R, E));
		if (const TSharedPtr<FJsonObject> O = ObjOf(R))
		{
			TestEqual(TEXT("kind"), O->GetStringField(TEXT("kind")), FString(TEXT("car")));
			TestEqual(TEXT("fileName 에 .json"), O->GetStringField(TEXT("fileName")), FString(TEXT("_AutomationTest_FileRead.json")));
			TestEqual(TEXT("path = 쓴 자리"), O->GetStringField(TEXT("path")), Path);
			TestEqual(TEXT("entries 3"), (int32)O->GetNumberField(TEXT("entries")), 3);
			const TSharedPtr<FJsonObject>* Content = nullptr;
			if (O->TryGetObjectField(TEXT("content"), Content))
			{
				TestTrue(TEXT("content.isUnreal"), (*Content)->GetBoolField(TEXT("isUnreal")));
				const TArray<TSharedPtr<FJsonValue>>* Datas = nullptr;
				TestTrue(TEXT("content.datas 3"), (*Content)->TryGetArrayField(TEXT("datas"), Datas) && Datas->Num() == 3);
			}
			else { AddError(TEXT("content 객체 없음")); }
		}
		else { AddError(TEXT("file.read 결과가 객체가 아님")); }
	}

	// list withContent — 행에 fileName/path/sizeBytes/modified/entries/content.
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("kind"), TEXT("car"));
		P->SetBoolField(TEXT("withContent"), true);
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestTrue(TEXT("file.list withContent 성공"), D->Dispatch(TEXT("file.list"), P, R, E));
		if (const TSharedPtr<FJsonObject>* Row = FindRow(FilesOf(R, TEXT("car")), TEXT("_AutomationTest_FileRead.json")))
		{
			TestEqual(TEXT("row.path"), (*Row)->GetStringField(TEXT("path")), Path);
			TestTrue(TEXT("row.sizeBytes > 0"), (*Row)->GetNumberField(TEXT("sizeBytes")) > 0.0);
			TestFalse(TEXT("row.modified 비어있지 않음"), (*Row)->GetStringField(TEXT("modified")).IsEmpty());
			TestEqual(TEXT("row.entries 3"), (int32)(*Row)->GetNumberField(TEXT("entries")), 3);
			TestTrue(TEXT("row.content 객체"), (*Row)->HasTypedField<EJson::Object>(TEXT("content")));
		}
		else { AddError(TEXT("list 에 임시 파일 행이 없음")); }

		// withContent 생략 → content 키 없음.
		TSharedPtr<FJsonObject> P2 = MakeShared<FJsonObject>(); P2->SetStringField(TEXT("kind"), TEXT("car"));
		TSharedPtr<FJsonValue> R2; FRpcError E2;
		D->Dispatch(TEXT("file.list"), P2, R2, E2);
		if (const TSharedPtr<FJsonObject>* Row = FindRow(FilesOf(R2, TEXT("car")), TEXT("_AutomationTest_FileRead.json")))
		{
			TestFalse(TEXT("withContent=false 면 content 없음"), (*Row)->HasField(TEXT("content")));
		}
	}

	// 루트가 객체가 아닌 파일 → read 는 실패, list 는 entries=null 로 행만 낸다.
	{
		const FString ArrPath = DataFile(TEXT("CarPos"), TEXT("_AutomationTest_FileReadArray.json"));
		FFileHelper::SaveStringToFile(TEXT("[1,2,3]"), *ArrPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("kind"), TEXT("car"));
		P->SetStringField(TEXT("fileName"), TEXT("_AutomationTest_FileReadArray.json"));
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestFalse(TEXT("루트 배열 파일은 read 실패"), D->Dispatch(TEXT("file.read"), P, R, E));
		TestTrue(TEXT("사유 = JSON 객체가 아니다"), E.Message.Contains(TEXT("JSON 객체가 아니다")));
		IFileManager::Get().Delete(*ArrPath, false, true, true);
	}

	IFileManager::Get().Delete(*Path, false, true, true);
	TestFalse(TEXT("임시 파일 정리"), IFileManager::Get().FileExists(*Path));
	return true;
}

// ===== preset.importFile: 검증 → 쓰기 → file.read(kind=preset) 로 되읽기, overwrite 규칙, 자리 검문 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcPresetImportFileTest,
	"Park3D.Rpc.PresetImportFile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcPresetImportFileTest::RunTest(const FString& Parameters)
{
	URpcDispatcher* D = NewObject<URpcDispatcher>();
	auto NoWorld = []() -> UWorld* { return nullptr; };
	FPresetRpcModule Preset(NoWorld);   // importFile 은 매니저(월드)를 쓰지 않는다 — 씬을 건드리지 않는 것이 계약이다.
	FFileRpcModule File(NoWorld);
	Preset.Register(*D);
	File.Register(*D);

	const FString Name = TEXT("_AutomationTest_PresetImport");
	const FString Path = DataFile(TEXT("Preset"), TEXT("_AutomationTest_PresetImport.json"));
	IFileManager::Get().Delete(*Path, false, true, true); // 이전 실행 잔해 제거

	const FString Content = TEXT(R"({"isUnreal":true,"datas":[{"idx":7,"presetName":"T","faceCount":4,"offsetPos":{"x":1.5,"y":0,"z":2.5},"faceRot":0,"groupRot":90,"xSize":2.5,"zSize":5,"dirType":0,"useBaseWidth":true,"camIdx":2}]})");

	auto Import = [&](const FString& FileName, const TSharedPtr<FJsonValue>& ContentVal, bool bOverwrite, TSharedPtr<FJsonValue>& R, FRpcError& E) -> bool
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		if (!FileName.IsEmpty()) P->SetStringField(TEXT("fileName"), FileName);
		if (ContentVal.IsValid()) P->SetField(TEXT("content"), ContentVal);
		if (bOverwrite) P->SetBoolField(TEXT("overwrite"), true);
		return D->Dispatch(TEXT("preset.importFile"), P, R, E);
	};

	// 1) 문자열 content → 쓰기 성공, 결과 모양.
	{
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestTrue(TEXT("importFile(문자열) 성공"), Import(Name, MakeShared<FJsonValueString>(Content), false, R, E));
		if (const TSharedPtr<FJsonObject> O = ObjOf(R))
		{
			TestTrue(TEXT("ok"), O->GetBoolField(TEXT("ok")));
			TestFalse(TEXT("처음엔 overwritten=false"), O->GetBoolField(TEXT("overwritten")));
			TestEqual(TEXT("path"), O->GetStringField(TEXT("path")), Path);
			TestEqual(TEXT("fileName"), O->GetStringField(TEXT("fileName")), FString(TEXT("_AutomationTest_PresetImport.json")));
			TestEqual(TEXT("count 1"), (int32)O->GetNumberField(TEXT("count")), 1);
			const TArray<TSharedPtr<FJsonValue>>* Presets = nullptr;
			if (O->TryGetArrayField(TEXT("presets"), Presets) && Presets->Num() == 1)
			{
				TestEqual(TEXT("presets[0].faceCount"), (int32)(*Presets)[0]->AsObject()->GetNumberField(TEXT("faceCount")), 4);
				TestEqual(TEXT("presets[0].idx"), (int32)(*Presets)[0]->AsObject()->GetNumberField(TEXT("idx")), 7);
			}
			else { AddError(TEXT("presets[] 1개가 아님")); }
		}
		else { AddError(TEXT("importFile 결과가 객체가 아님")); }
		TestTrue(TEXT("파일이 실제로 생겼다"), IFileManager::Get().FileExists(*Path));
	}

	// 2) 같은 이름 다시 → overwrite 없으면 거부, overwrite:true 면 overwritten=true. 객체 content 도 받는다.
	{
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestFalse(TEXT("같은 이름 overwrite 없이 거부"), Import(Name, MakeShared<FJsonValueString>(Content), false, R, E));
		TestTrue(TEXT("사유에 overwrite"), E.Message.Contains(TEXT("overwrite")));

		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
		TSharedPtr<FJsonObject> ContentObj;
		TestTrue(TEXT("테스트 content 파싱"), FJsonSerializer::Deserialize(Reader, ContentObj) && ContentObj.IsValid());
		TSharedPtr<FJsonValue> R2; FRpcError E2;
		TestTrue(TEXT("importFile(객체, overwrite) 성공"), Import(Name, MakeShared<FJsonValueObject>(ContentObj), true, R2, E2));
		if (const TSharedPtr<FJsonObject> O = ObjOf(R2)) { TestTrue(TEXT("overwritten=true"), O->GetBoolField(TEXT("overwritten"))); }
	}

	// 3) file.read kind=preset 로 되읽기 — preset.load 가 읽는 형식(isUnreal:true + datas[].faceCount)으로 저장돼 있다.
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("kind"), TEXT("preset"));
		P->SetStringField(TEXT("fileName"), Name);
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestTrue(TEXT("file.read(preset) 성공"), D->Dispatch(TEXT("file.read"), P, R, E));
		const TSharedPtr<FJsonObject> O = ObjOf(R);
		const TSharedPtr<FJsonObject>* Doc = nullptr;
		if (O.IsValid() && O->TryGetObjectField(TEXT("content"), Doc))
		{
			TestEqual(TEXT("entries 1"), (int32)O->GetNumberField(TEXT("entries")), 1);
			TestTrue(TEXT("저장 파일 isUnreal=true"), (*Doc)->GetBoolField(TEXT("isUnreal")));
			const TArray<TSharedPtr<FJsonValue>>* Datas = nullptr;
			if ((*Doc)->TryGetArrayField(TEXT("datas"), Datas) && Datas->Num() == 1)
			{
				const TSharedPtr<FJsonObject> Row = (*Datas)[0]->AsObject();
				TestEqual(TEXT("datas[0].faceCount 4"), (int32)Row->GetNumberField(TEXT("faceCount")), 4);
				TestEqual(TEXT("datas[0].camIdx 2"), (int32)Row->GetNumberField(TEXT("camIdx")), 2);
				TestTrue(TEXT("datas[0].groupRot 90"), FMath::IsNearlyEqual(Row->GetNumberField(TEXT("groupRot")), 90.0, 0.01));
			}
			else { AddError(TEXT("저장 파일 datas 1개가 아님")); }
		}
		else { AddError(TEXT("file.read content 없음")); }
	}

	// 4) 형식 오류: 차량 파일(datas 항목에 faceCount 없음) · datas 없음 · 빈 datas · 깨진 JSON · content 누락.
	{
		const TCHAR* BadName = TEXT("_AutomationTest_PresetImportBad");
		const FString BadPath = DataFile(TEXT("Preset"), TEXT("_AutomationTest_PresetImportBad.json"));
		for (const TCHAR* Bad : { TEXT(R"({"isUnreal":true,"datas":[{"id":"0-1","prefabId":2}]})"), TEXT(R"({"foo":1})"), TEXT(R"({"datas":[]})"), TEXT("{not json") })
		{
			TSharedPtr<FJsonValue> R; FRpcError E;
			TestFalse(*FString::Printf(TEXT("형식 오류 거부: %s"), Bad), Import(BadName, MakeShared<FJsonValueString>(Bad), true, R, E));
			TestEqual(TEXT("코드 -32000"), E.Code, Park3DRpc::Domain);
		}
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestFalse(TEXT("content 누락 거부"), Import(BadName, nullptr, true, R, E));
		TestTrue(TEXT("사유에 content"), E.Message.Contains(TEXT("content")));
		TestFalse(TEXT("형식 오류는 파일을 만들지 않는다"), IFileManager::Get().FileExists(*BadPath));
	}

	// 5) 자리 검문: fileName 에 구분자 · fullPath 가 Preset 폴더 밖 · fileName/fullPath 둘 다 없음.
	{
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestFalse(TEXT("fileName 에 `..\\` 거부"), Import(TEXT("..\\_AutomationTest_Escape"), MakeShared<FJsonValueString>(Content), true, R, E));

		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("fullPath"), FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Config"), TEXT("_AutomationTest_Escape.json")));
		P->SetStringField(TEXT("content"), Content);
		TSharedPtr<FJsonValue> R2; FRpcError E2;
		TestFalse(TEXT("Preset 폴더 밖 fullPath 거부"), D->Dispatch(TEXT("preset.importFile"), P, R2, E2));
		TestTrue(TEXT("사유 = 폴더 밖"), E2.Message.Contains(TEXT("폴더 밖")));

		TSharedPtr<FJsonObject> P3 = MakeShared<FJsonObject>(); P3->SetStringField(TEXT("content"), Content);
		TSharedPtr<FJsonValue> R3; FRpcError E3;
		TestFalse(TEXT("fileName/fullPath 둘 다 없으면 거부(기본 이름으로 덮어쓰지 않는다)"), D->Dispatch(TEXT("preset.importFile"), P3, R3, E3));

		// Preset 폴더 안의 fullPath 는 통과한다(그 파일도 정리).
		const FString InsidePath = DataFile(TEXT("Preset"), TEXT("_AutomationTest_PresetImportFull.json"));
		TSharedPtr<FJsonObject> P4 = MakeShared<FJsonObject>();
		P4->SetStringField(TEXT("fullPath"), InsidePath);
		P4->SetStringField(TEXT("content"), Content);
		TSharedPtr<FJsonValue> R4; FRpcError E4;
		TestTrue(TEXT("Preset 폴더 안 fullPath 통과"), D->Dispatch(TEXT("preset.importFile"), P4, R4, E4));
		IFileManager::Get().Delete(*InsidePath, false, true, true);
	}

	IFileManager::Get().Delete(*Path, false, true, true);
	TestFalse(TEXT("임시 프리셋 파일 정리"), IFileManager::Get().FileExists(*Path));
	return true;
}

// ===== system.describe: 디스패처 메타 맵 → describe 본문에 반영, 기본값, extensions, ClearSceneModules 동반 정리 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcSystemDescribeTest,
	"Park3D.Rpc.SystemDescribe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcSystemDescribeTest::RunTest(const FString& Parameters)
{
	URpcDispatcher* D = NewObject<URpcDispatcher>();
	auto Ok = [](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue> { return MakeShared<FJsonValueObject>(MakeShared<FJsonObject>()); };
	D->RegisterPersistent(TEXT("system.ping"), Ok);
	D->Register(TEXT("x.plain"), Ok);
	D->Register(TEXT("x.meta"), Ok);

	FRpcMethodMeta Meta;
	Meta.bMutating = false;
	Meta.bDestructive = true;
	Meta.Params = TEXT("a b?");
	Meta.Doc = TEXT("설명");
	D->SetMethodMeta(TEXT("x.meta"), Meta);
	TestNotNull(TEXT("FindMethodMeta"), D->FindMethodMeta(TEXT("x.meta")));
	TestNull(TEXT("메타 없는 method 는 nullptr"), D->FindMethodMeta(TEXT("x.plain")));

	const TSharedPtr<FJsonValue> R = D->Describe({ TEXT("x.meta"), TEXT("a.first") });
	const TSharedPtr<FJsonObject> O = ObjOf(R);
	if (!O.IsValid()) { AddError(TEXT("describe 결과가 객체가 아님")); return true; }

	TestEqual(TEXT("count = 등록 수"), (int32)O->GetNumberField(TEXT("count")), 3);
	const TArray<TSharedPtr<FJsonValue>>* Ext = nullptr;
	if (O->TryGetArrayField(TEXT("extensions"), Ext) && Ext->Num() == 2)
	{
		TestEqual(TEXT("extensions 정렬"), (*Ext)[0]->AsString(), FString(TEXT("a.first")));
		TestEqual(TEXT("extensions[1]"), (*Ext)[1]->AsString(), FString(TEXT("x.meta")));
	}
	else { AddError(TEXT("extensions 2개가 아님")); }

	const TArray<TSharedPtr<FJsonValue>>* Methods = nullptr;
	TestTrue(TEXT("methods 배열 3"), O->TryGetArrayField(TEXT("methods"), Methods) && Methods->Num() == 3);
	if (Methods)
	{
		auto Find = [&](const TCHAR* Name) -> TSharedPtr<FJsonObject>
		{
			for (const TSharedPtr<FJsonValue>& V : *Methods)
			{
				if (V->AsObject()->GetStringField(TEXT("name")) == Name) return V->AsObject();
			}
			return nullptr;
		};
		if (const TSharedPtr<FJsonObject> M = Find(TEXT("x.meta")))
		{
			TestFalse(TEXT("x.meta mutating=false"), M->GetBoolField(TEXT("mutating")));
			TestTrue(TEXT("x.meta destructive=true"), M->GetBoolField(TEXT("destructive")));
			TestFalse(TEXT("x.meta persistent=false"), M->GetBoolField(TEXT("persistent")));
			TestEqual(TEXT("x.meta params"), M->GetStringField(TEXT("params")), FString(TEXT("a b?")));
			TestEqual(TEXT("x.meta doc"), M->GetStringField(TEXT("doc")), FString(TEXT("설명")));
			TestTrue(TEXT("x.meta unreal=true"), M->GetBoolField(TEXT("unreal")));
		}
		else { AddError(TEXT("x.meta 항목 없음")); }
		if (const TSharedPtr<FJsonObject> M = Find(TEXT("x.plain")))
		{
			TestTrue(TEXT("메타 없는 비영속은 mutating=true"), M->GetBoolField(TEXT("mutating")));
			TestFalse(TEXT("메타 없는 비영속은 destructive=false"), M->GetBoolField(TEXT("destructive")));
			TestEqual(TEXT("params 빈 문자열"), M->GetStringField(TEXT("params")), FString());
		}
		else { AddError(TEXT("x.plain 항목 없음")); }
		if (const TSharedPtr<FJsonObject> M = Find(TEXT("system.ping")))
		{
			TestTrue(TEXT("영속 persistent=true"), M->GetBoolField(TEXT("persistent")));
			TestFalse(TEXT("메타 없는 영속은 mutating=false"), M->GetBoolField(TEXT("mutating")));
		}
		else { AddError(TEXT("system.ping 항목 없음")); }
	}

	// ClearSceneModules 는 비영속 핸들러와 함께 그 메타도 지운다.
	D->ClearSceneModules();
	TestNull(TEXT("정리 후 x.meta 메타 없음"), D->FindMethodMeta(TEXT("x.meta")));
	TestTrue(TEXT("system.ping 생존"), D->IsPersistent(TEXT("system.ping")) && D->HasMethod(TEXT("system.ping")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
