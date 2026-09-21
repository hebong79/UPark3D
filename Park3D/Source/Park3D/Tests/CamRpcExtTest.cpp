// Copyright Epic Games, Inc. All Rights Reserved.
// CamRpcExtTest : cam.* OmiPark3D 확장 12종 중 RHI 없이 검증 가능한 것(rename / PosFile 4종 / setMarks·marks /
// listPresets·setPreset·removePreset)을 디스패처 직접 호출로 검증한다. cam.captureStats 는 실RHI 가 필요해 여기서 다루지 않는다.
// 파일 I/O 는 테스트 전용 이름(zz_test_camext.json)으로 하고 끝에 지운다(Save/3D/CameraPos 에 잔해를 남기지 않는다).

#include "Misc/AutomationTest.h"
#include "../Rpc/RpcDispatcher.h"
#include "../Rpc/Modules/CamRpcModule.h"
#include "../CameraControlManager.h"
#include "../Park3DDataPaths.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	const TCHAR* TestPosFile = TEXT("zz_test_camext.json");

	UWorld* EditorWorldOrNull()
	{
		return (GEngine && GEngine->GetWorldContexts().Num() > 0) ? GWorld : nullptr;
	}

	/** 기존 카메라 매니저 제거(격리). 스폰된 카메라 액터는 기존 테스트와 같이 월드에 남긴다. */
	void ResetCameraManager(UWorld* World)
	{
		if (ACameraControlManager* Old = Cast<ACameraControlManager>(
			UGameplayStatics::GetActorOfClass(World, ACameraControlManager::StaticClass())))
		{
			Old->Destroy();
		}
	}

	FString TestPosFilePath()
	{
		return Park3DDataPaths::GetDataFilePath(TEXT("CameraPos"), TestPosFile);
	}

	void DeleteTestPosFile()
	{
		IFileManager::Get().Delete(*TestPosFilePath(), /*RequireExists=*/false);
	}

	TSharedPtr<FJsonObject> Vec3Obj(double X, double Y, double Z)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("x"), X);
		O->SetNumberField(TEXT("y"), Y);
		O->SetNumberField(TEXT("z"), Z);
		return O;
	}

	TSharedPtr<FJsonObject> PtzObj(double P, double T, double Z)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("p"), P);
		O->SetNumberField(TEXT("t"), T);
		O->SetNumberField(TEXT("z"), Z);
		return O;
	}

	/** CamDir 1건(Unreal 미터 좌표, rot 에 tilt/pan). */
	TSharedPtr<FJsonValue> CamDirObj(int32 CamId, int32 PresetId, double X, double Y, double H, double Pan, double Tilt, double Zoom)
	{
		TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
		D->SetNumberField(TEXT("idx"), PresetId - 1);
		D->SetStringField(TEXT("sname"), FString::Printf(TEXT("Preset %d"), PresetId));
		D->SetNumberField(TEXT("cam_id"), CamId);
		D->SetNumberField(TEXT("preset_id"), PresetId);
		D->SetObjectField(TEXT("pos"), Vec3Obj(X, Y, H));
		D->SetObjectField(TEXT("rot"), Vec3Obj(Tilt, Pan, 0));
		D->SetNumberField(TEXT("pan"), Pan);
		D->SetNumberField(TEXT("tilt"), Tilt);
		D->SetNumberField(TEXT("zoom"), Zoom);
		D->SetObjectField(TEXT("ptzmin"), PtzObj(-180, -90, 1));
		D->SetObjectField(TEXT("ptzmax"), PtzObj(180, 90, 36));
		return MakeShared<FJsonValueObject>(D);
	}

	/** 카메라 CamCount 대(각 Preset 1 하나)짜리 CamPos 문서. */
	TSharedPtr<FJsonObject> CamPosDoc(int32 CamCount)
	{
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("isUnreal"), true);
		TArray<TSharedPtr<FJsonValue>> Cams;
		for (int32 i = 0; i < CamCount; ++i)
		{
			TSharedPtr<FJsonObject> CP = MakeShared<FJsonObject>();
			CP->SetNumberField(TEXT("target_pos"), 0);
			TArray<TSharedPtr<FJsonValue>> Dirs;
			Dirs.Add(CamDirObj(i + 1, 1, 2.0 * i, 1.0, 5.0 + i, 30.0 * i, 10.0, 2.0));
			CP->SetArrayField(TEXT("datas"), Dirs);
			Cams.Add(MakeShared<FJsonValueObject>(CP));
		}
		Root->SetArrayField(TEXT("datas"), Cams);
		return Root;
	}

	FString ToJsonString(const TSharedPtr<FJsonObject>& Obj)
	{
		FString Out;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj.ToSharedRef(), W);
		return Out;
	}

	int32 ArrayCount(const TSharedPtr<FJsonValue>& R, const TCHAR* Key)
	{
		if (!R.IsValid() || R->Type != EJson::Object) return -1;
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		return R->AsObject()->TryGetArrayField(Key, Arr) ? Arr->Num() : -1;
	}

	FString StringOf(const TSharedPtr<FJsonValue>& R, const TCHAR* Key)
	{
		FString V;
		if (R.IsValid() && R->Type == EJson::Object) { R->AsObject()->TryGetStringField(Key, V); }
		return V;
	}

	double NumberOf(const TSharedPtr<FJsonValue>& R, const TCHAR* Key, double Default = -1.0)
	{
		double V = Default;
		if (R.IsValid() && R->Type == EJson::Object) { R->AsObject()->TryGetNumberField(Key, V); }
		return V;
	}

	bool BoolOf(const TSharedPtr<FJsonValue>& R, const TCHAR* Key, bool Default = false)
	{
		bool V = Default;
		if (R.IsValid() && R->Type == EJson::Object) { R->AsObject()->TryGetBoolField(Key, V); }
		return V;
	}

	int32 CountCamMarkActors(UWorld* World)
	{
		int32 N = 0;
		for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
		{
			if (IsValid(*It) && It->ActorHasTag(FName(TEXT("CamMark")))) ++N;
		}
		return N;
	}
}

// ===== cam.rename → cam.get / cam.list 의 name 반영 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCamRpcExtRenameTest,
	"Park3D.Rpc.CamModuleExt.Rename",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCamRpcExtRenameTest::RunTest(const FString& Parameters)
{
	UWorld* World = EditorWorldOrNull();
	if (!World) { AddWarning(TEXT("에디터 월드 없음 — 건너뜀.")); return true; }
	ResetCameraManager(World);

	URpcDispatcher* D = NewObject<URpcDispatcher>();
	FCamRpcModule Cam([World]() -> UWorld* { return World; });
	Cam.Register(*D);
	auto Dispatch = [&](const FString& M, const TSharedPtr<FJsonObject>& P, TSharedPtr<FJsonValue>& R) -> bool
	{ FRpcError E; return D->Dispatch(M, P, R, E); };

	TSharedPtr<FJsonValue> CR;
	TestTrue(TEXT("cam.create"), Dispatch(TEXT("cam.create"), nullptr, CR));
	const int32 CamId = (int32)NumberOf(CR, TEXT("camId"));
	TestEqual(TEXT("camId 1"), CamId, 1);

	TSharedPtr<FJsonObject> GetP = MakeShared<FJsonObject>(); GetP->SetNumberField(TEXT("camId"), CamId);
	TSharedPtr<FJsonValue> G0;
	Dispatch(TEXT("cam.get"), GetP, G0);
	TestEqual(TEXT("기본 이름 Camera-1"), StringOf(G0, TEXT("name")), FString(TEXT("Camera-1")));

	// rename → get / list 반영
	TSharedPtr<FJsonObject> RnP = MakeShared<FJsonObject>();
	RnP->SetNumberField(TEXT("camId"), CamId);
	RnP->SetStringField(TEXT("name"), TEXT("  입구 카메라  "));
	TSharedPtr<FJsonValue> RnR;
	TestTrue(TEXT("cam.rename 성공"), Dispatch(TEXT("cam.rename"), RnP, RnR));
	TestEqual(TEXT("rename 응답 name(trim)"), StringOf(RnR, TEXT("name")), FString(TEXT("입구 카메라")));
	TSharedPtr<FJsonValue> G1;
	Dispatch(TEXT("cam.get"), GetP, G1);
	TestEqual(TEXT("cam.get name"), StringOf(G1, TEXT("name")), FString(TEXT("입구 카메라")));
	TSharedPtr<FJsonValue> L;
	Dispatch(TEXT("cam.list"), nullptr, L);
	if (L.IsValid() && L->Type == EJson::Object)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (L->AsObject()->TryGetArrayField(TEXT("cameras"), Arr) && Arr->Num() == 1)
		{
			TestEqual(TEXT("cam.list name"), StringOf((*Arr)[0], TEXT("name")), FString(TEXT("입구 카메라")));
		}
		else { AddError(TEXT("cam.list cameras 1개가 아님")); }
	}

	// 빈 이름 / 없는 카메라 거부
	TSharedPtr<FJsonObject> BadP = MakeShared<FJsonObject>();
	BadP->SetNumberField(TEXT("camId"), CamId);
	BadP->SetStringField(TEXT("name"), TEXT("   "));
	TSharedPtr<FJsonValue> BadR; FRpcError BadE;
	TestFalse(TEXT("빈 name 거부"), D->Dispatch(TEXT("cam.rename"), BadP, BadR, BadE));
	TestEqual(TEXT("빈 name -32000"), BadE.Code, Park3DRpc::Domain);
	TSharedPtr<FJsonObject> NoCamP = MakeShared<FJsonObject>();
	NoCamP->SetNumberField(TEXT("camId"), 99);
	NoCamP->SetStringField(TEXT("name"), TEXT("x"));
	TSharedPtr<FJsonValue> NoCamR; FRpcError NoCamE;
	TestFalse(TEXT("없는 카메라 거부"), D->Dispatch(TEXT("cam.rename"), NoCamP, NoCamR, NoCamE));

	ResetCameraManager(World);
	return true;
}

// ===== listPosFiles / importPosFile / loadPosFile / savePosFile 왕복 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCamRpcExtPosFilesTest,
	"Park3D.Rpc.CamModuleExt.PosFiles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCamRpcExtPosFilesTest::RunTest(const FString& Parameters)
{
	UWorld* World = EditorWorldOrNull();
	if (!World) { AddWarning(TEXT("에디터 월드 없음 — 건너뜀.")); return true; }
	ResetCameraManager(World);
	DeleteTestPosFile();

	URpcDispatcher* D = NewObject<URpcDispatcher>();
	FCamRpcModule Cam([World]() -> UWorld* { return World; });
	Cam.Register(*D);
	auto Dispatch = [&](const FString& M, const TSharedPtr<FJsonObject>& P, TSharedPtr<FJsonValue>& R) -> bool
	{ FRpcError E; return D->Dispatch(M, P, R, E); };
	auto ListHas = [&](const FString& Name) -> bool
	{
		TSharedPtr<FJsonValue> R; Dispatch(TEXT("cam.listPosFiles"), nullptr, R);
		if (!R.IsValid() || R->Type != EJson::Object) return false;
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!R->AsObject()->TryGetArrayField(TEXT("files"), Arr)) return false;
		for (const TSharedPtr<FJsonValue>& V : *Arr) { if (V->AsString() == Name) return true; }
		return false;
	};

	// 시작: 카메라 1대, 테스트 파일 없음
	TSharedPtr<FJsonValue> CR; Dispatch(TEXT("cam.create"), nullptr, CR);
	TestFalse(TEXT("시작 시 테스트 파일 없음"), ListHas(TestPosFile));

	// importPosFile(객체, 카메라 2대)
	TSharedPtr<FJsonObject> ImpP = MakeShared<FJsonObject>();
	ImpP->SetStringField(TEXT("fileName"), TEXT("zz_test_camext")); // .json 생략 → 붙는다
	ImpP->SetObjectField(TEXT("content"), CamPosDoc(2));
	TSharedPtr<FJsonValue> ImpR;
	TestTrue(TEXT("cam.importPosFile 성공"), Dispatch(TEXT("cam.importPosFile"), ImpP, ImpR));
	TestEqual(TEXT("import fileName"), StringOf(ImpR, TEXT("fileName")), FString(TestPosFile));
	TestEqual(TEXT("import cameraCount 2"), (int32)NumberOf(ImpR, TEXT("cameraCount")), 2);
	TestFalse(TEXT("import overwritten=false"), BoolOf(ImpR, TEXT("overwritten"), true));
	TestTrue(TEXT("파일 생성"), IFileManager::Get().FileExists(*TestPosFilePath()));
	TestTrue(TEXT("listPosFiles 에 보인다"), ListHas(TestPosFile));

	// 같은 이름 다시(overwrite 없음) → -32000 ; overwrite:true + 문자열 content → ok
	TSharedPtr<FJsonValue> Dup; FRpcError DupE;
	TestFalse(TEXT("overwrite 없이 거부"), D->Dispatch(TEXT("cam.importPosFile"), ImpP, Dup, DupE));
	TestEqual(TEXT("거부 -32000"), DupE.Code, Park3DRpc::Domain);
	TSharedPtr<FJsonObject> Imp2P = MakeShared<FJsonObject>();
	Imp2P->SetStringField(TEXT("fileName"), TestPosFile);
	Imp2P->SetStringField(TEXT("content"), ToJsonString(CamPosDoc(3)));
	Imp2P->SetBoolField(TEXT("overwrite"), true);
	TSharedPtr<FJsonValue> Imp2R;
	TestTrue(TEXT("overwrite:true 문자열 content 성공"), Dispatch(TEXT("cam.importPosFile"), Imp2P, Imp2R));
	TestTrue(TEXT("overwritten=true"), BoolOf(Imp2R, TEXT("overwritten"), false));
	TestEqual(TEXT("cameraCount 3"), (int32)NumberOf(Imp2R, TEXT("cameraCount")), 3);

	// 잘못된 content / 잘못된 이름 거부
	TSharedPtr<FJsonObject> BadP = MakeShared<FJsonObject>();
	BadP->SetStringField(TEXT("fileName"), TEXT("zz_bad"));
	BadP->SetStringField(TEXT("content"), TEXT("{\"foo\":1}"));
	TSharedPtr<FJsonValue> BadR; FRpcError BadE;
	TestFalse(TEXT("CamPos 형식 아님 거부"), D->Dispatch(TEXT("cam.importPosFile"), BadP, BadR, BadE));
	TSharedPtr<FJsonObject> BadNameP = MakeShared<FJsonObject>();
	BadNameP->SetStringField(TEXT("fileName"), TEXT("../zz_escape"));
	BadNameP->SetObjectField(TEXT("content"), CamPosDoc(1));
	TSharedPtr<FJsonValue> BadNameR; FRpcError BadNameE;
	TestFalse(TEXT("경로 포함 이름 거부"), D->Dispatch(TEXT("cam.importPosFile"), BadNameP, BadNameR, BadNameE));

	// loadPosFile → 카메라 3대, current 갱신, 2번 카메라 pan=30
	TSharedPtr<FJsonObject> LoadP = MakeShared<FJsonObject>();
	LoadP->SetStringField(TEXT("fileName"), TestPosFile);
	TSharedPtr<FJsonValue> LoadR;
	TestTrue(TEXT("cam.loadPosFile 성공"), Dispatch(TEXT("cam.loadPosFile"), LoadP, LoadR));
	TSharedPtr<FJsonValue> ListR; Dispatch(TEXT("cam.list"), nullptr, ListR);
	TestEqual(TEXT("로드 후 카메라 3대"), ArrayCount(ListR, TEXT("cameras")), 3);
	TSharedPtr<FJsonValue> FilesR; Dispatch(TEXT("cam.listPosFiles"), nullptr, FilesR);
	TestEqual(TEXT("current = 로드한 파일"), StringOf(FilesR, TEXT("current")), FString(TestPosFile));
	TSharedPtr<FJsonObject> Ptz2P = MakeShared<FJsonObject>(); Ptz2P->SetNumberField(TEXT("camId"), 2);
	TSharedPtr<FJsonValue> Ptz2R; Dispatch(TEXT("cam.getPTZ"), Ptz2P, Ptz2R);
	TestTrue(TEXT("2번 카메라 pan≈30"), FMath::IsNearlyEqual(NumberOf(Ptz2R, TEXT("pan")), 30.0, 0.1));

	// 카메라 1대 삭제 후 savePosFile(fileName 생략 = current, overwrite 불필요) → cameraCount 2
	TSharedPtr<FJsonObject> DelP = MakeShared<FJsonObject>(); DelP->SetNumberField(TEXT("camId"), 3);
	TSharedPtr<FJsonValue> DelR; Dispatch(TEXT("cam.delete"), DelP, DelR);
	TSharedPtr<FJsonValue> SaveR;
	TestTrue(TEXT("cam.savePosFile 성공"), Dispatch(TEXT("cam.savePosFile"), nullptr, SaveR));
	TestEqual(TEXT("save fileName = current"), StringOf(SaveR, TEXT("fileName")), FString(TestPosFile));
	TestEqual(TEXT("save cameraCount 2"), (int32)NumberOf(SaveR, TEXT("cameraCount")), 2);
	TestTrue(TEXT("save overwritten=true"), BoolOf(SaveR, TEXT("overwritten"), false));

	// 다시 로드하면 2대
	TSharedPtr<FJsonValue> Load2R;
	TestTrue(TEXT("재로드 성공"), Dispatch(TEXT("cam.loadPosFile"), LoadP, Load2R));
	TestEqual(TEXT("재로드 cameraCount 2"), (int32)NumberOf(Load2R, TEXT("cameraCount")), 2);

	// 없는 파일 로드 → -32000
	TSharedPtr<FJsonObject> NoP = MakeShared<FJsonObject>(); NoP->SetStringField(TEXT("fileName"), TEXT("zz_test_camext_missing"));
	TSharedPtr<FJsonValue> NoR; FRpcError NoE;
	TestFalse(TEXT("없는 파일 로드 거부"), D->Dispatch(TEXT("cam.loadPosFile"), NoP, NoR, NoE));
	TestEqual(TEXT("없는 파일 -32000"), NoE.Code, Park3DRpc::Domain);

	DeleteTestPosFile();
	TestFalse(TEXT("테스트 파일 정리"), IFileManager::Get().FileExists(*TestPosFilePath()));
	ResetCameraManager(World);
	return true;
}

// ===== setMarks on/off → marks / CamMark 액터 수 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCamRpcExtMarksTest,
	"Park3D.Rpc.CamModuleExt.Marks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCamRpcExtMarksTest::RunTest(const FString& Parameters)
{
	UWorld* World = EditorWorldOrNull();
	if (!World) { AddWarning(TEXT("에디터 월드 없음 — 건너뜀.")); return true; }
	ResetCameraManager(World);

	URpcDispatcher* D = NewObject<URpcDispatcher>();
	FCamRpcModule Cam([World]() -> UWorld* { return World; });
	Cam.Register(*D);
	auto Dispatch = [&](const FString& M, const TSharedPtr<FJsonObject>& P, TSharedPtr<FJsonValue>& R) -> bool
	{ FRpcError E; return D->Dispatch(M, P, R, E); };

	TSharedPtr<FJsonValue> C1, C2;
	Dispatch(TEXT("cam.create"), nullptr, C1);
	Dispatch(TEXT("cam.create"), nullptr, C2);

	// 기본: 꺼짐, marks 는 카메라 수만큼(상태만), 액터 없음
	TSharedPtr<FJsonValue> M0;
	TestTrue(TEXT("cam.marks 성공"), Dispatch(TEXT("cam.marks"), nullptr, M0));
	TestFalse(TEXT("기본 enabled=false"), BoolOf(M0, TEXT("enabled"), true));
	TestEqual(TEXT("marks 2건"), ArrayCount(M0, TEXT("marks")), 2);

	// 켜기 → 액터 2개(태그 CamMark)
	TSharedPtr<FJsonValue> On;
	TestTrue(TEXT("cam.setMarks 성공"), Dispatch(TEXT("cam.setMarks"), nullptr, On)); // enabled 기본 true
	TestTrue(TEXT("enabled=true"), BoolOf(On, TEXT("enabled"), false));
	TestEqual(TEXT("marks 2건"), ArrayCount(On, TEXT("marks")), 2);
	TestEqual(TEXT("CamMark 액터 2개"), CountCamMarkActors(World), 2);
	TestTrue(TEXT("dropM 키"), NumberOf(On, TEXT("dropM")) > 0.0);
	TestTrue(TEXT("ballRadiusM 키"), NumberOf(On, TEXT("ballRadiusM")) > 0.0);
	if (On.IsValid() && On->Type == EJson::Object)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (On->AsObject()->TryGetArrayField(TEXT("marks"), Arr) && Arr->Num() == 2)
		{
			TestEqual(TEXT("marks[0].camId 1"), (int32)NumberOf((*Arr)[0], TEXT("camId")), 1);
			TestEqual(TEXT("marks[0].name"), StringOf((*Arr)[0], TEXT("name")), FString(TEXT("Camera-1")));
			const TSharedPtr<FJsonObject>* Pos = nullptr;
			TestTrue(TEXT("marks[0].pos 객체"), (*Arr)[0]->AsObject()->TryGetObjectField(TEXT("pos"), Pos));
		}
	}

	// 카메라 추가 후 cam.marks → 3개로 맞춘다
	TSharedPtr<FJsonValue> C3; Dispatch(TEXT("cam.create"), nullptr, C3);
	TSharedPtr<FJsonValue> M3; Dispatch(TEXT("cam.marks"), nullptr, M3);
	TestEqual(TEXT("추가 후 marks 3건"), ArrayCount(M3, TEXT("marks")), 3);
	TestEqual(TEXT("추가 후 CamMark 액터 3개"), CountCamMarkActors(World), 3);

	// 끄기 → 액터 0개
	TSharedPtr<FJsonObject> OffP = MakeShared<FJsonObject>(); OffP->SetBoolField(TEXT("enabled"), false);
	TSharedPtr<FJsonValue> Off;
	TestTrue(TEXT("cam.setMarks false 성공"), Dispatch(TEXT("cam.setMarks"), OffP, Off));
	TestFalse(TEXT("enabled=false"), BoolOf(Off, TEXT("enabled"), true));
	TestEqual(TEXT("끈 뒤 CamMark 액터 0개"), CountCamMarkActors(World), 0);

	ResetCameraManager(World);
	return true;
}

// ===== listPresets / setPreset / removePreset (파일 기준) 왕복 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCamRpcExtFilePresetsTest,
	"Park3D.Rpc.CamModuleExt.FilePresets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCamRpcExtFilePresetsTest::RunTest(const FString& Parameters)
{
	UWorld* World = EditorWorldOrNull();
	if (!World) { AddWarning(TEXT("에디터 월드 없음 — 건너뜀.")); return true; }
	ResetCameraManager(World);
	DeleteTestPosFile();

	URpcDispatcher* D = NewObject<URpcDispatcher>();
	FCamRpcModule Cam([World]() -> UWorld* { return World; });
	Cam.Register(*D);
	auto Dispatch = [&](const FString& M, const TSharedPtr<FJsonObject>& P, TSharedPtr<FJsonValue>& R) -> bool
	{ FRpcError E; return D->Dispatch(M, P, R, E); };
	auto ListCount = [&](int32 CamId) -> int32
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetNumberField(TEXT("camId"), CamId);
		P->SetStringField(TEXT("fileName"), TestPosFile);
		TSharedPtr<FJsonValue> R;
		return Dispatch(TEXT("cam.listPresets"), P, R) ? ArrayCount(R, TEXT("presets")) : -1;
	};

	TSharedPtr<FJsonValue> CR; Dispatch(TEXT("cam.create"), nullptr, CR);
	// 카메라 자세: pan 45 / tilt 15 / zoom 3 → setPreset(값 생략) 이 이 자세를 담아야 한다.
	TSharedPtr<FJsonObject> PtzP = MakeShared<FJsonObject>();
	PtzP->SetNumberField(TEXT("camId"), 1);
	PtzP->SetNumberField(TEXT("pan"), 45.0);
	PtzP->SetNumberField(TEXT("tilt"), 15.0);
	PtzP->SetNumberField(TEXT("zoom"), 3.0);
	TSharedPtr<FJsonValue> PtzR; Dispatch(TEXT("cam.setPTZ"), PtzP, PtzR);

	// 파일 없을 때 listPresets → -32000, setPreset 은 파일을 만든다
	TSharedPtr<FJsonObject> ListP = MakeShared<FJsonObject>();
	ListP->SetNumberField(TEXT("camId"), 1);
	ListP->SetStringField(TEXT("fileName"), TestPosFile);
	TSharedPtr<FJsonValue> L0; FRpcError L0E;
	TestFalse(TEXT("파일 없으면 listPresets 거부"), D->Dispatch(TEXT("cam.listPresets"), ListP, L0, L0E));
	TestEqual(TEXT("거부 -32000"), L0E.Code, Park3DRpc::Domain);

	TSharedPtr<FJsonObject> SetP = MakeShared<FJsonObject>();
	SetP->SetNumberField(TEXT("camId"), 1);
	SetP->SetStringField(TEXT("fileName"), TestPosFile);
	TSharedPtr<FJsonValue> S1;
	TestTrue(TEXT("cam.setPreset(생성) 성공"), Dispatch(TEXT("cam.setPreset"), SetP, S1));
	TestTrue(TEXT("created=true"), BoolOf(S1, TEXT("created"), false));
	TestEqual(TEXT("새 presetId 1"), (int32)NumberOf(S1, TEXT("presetId")), 1);
	TestEqual(TEXT("name Preset 1"), StringOf(S1, TEXT("name")), FString(TEXT("Preset 1")));
	TestTrue(TEXT("pan≈45(카메라 자세)"), FMath::IsNearlyEqual(NumberOf(S1, TEXT("pan")), 45.0, 0.1));
	TestTrue(TEXT("zoom≈3"), FMath::IsNearlyEqual(NumberOf(S1, TEXT("zoom")), 3.0, 0.1));
	TestEqual(TEXT("count 1"), (int32)NumberOf(S1, TEXT("count")), 1);
	TestTrue(TEXT("파일 생성"), IFileManager::Get().FileExists(*TestPosFilePath()));
	TestEqual(TEXT("listPresets 1건"), ListCount(1), 1);

	// 두 번째 생성(presetId 생략 → 2), 세 번째는 presetId 5 지정 + 값 지정
	TSharedPtr<FJsonValue> S2;
	TestTrue(TEXT("setPreset 두 번째"), Dispatch(TEXT("cam.setPreset"), SetP, S2));
	TestEqual(TEXT("새 presetId 2"), (int32)NumberOf(S2, TEXT("presetId")), 2);
	TSharedPtr<FJsonObject> Set5P = MakeShared<FJsonObject>();
	Set5P->SetNumberField(TEXT("camId"), 1);
	Set5P->SetStringField(TEXT("fileName"), TestPosFile);
	Set5P->SetNumberField(TEXT("presetId"), 5);
	Set5P->SetStringField(TEXT("name"), TEXT("출구"));
	Set5P->SetNumberField(TEXT("pan"), -20.0);
	Set5P->SetObjectField(TEXT("pos"), Vec3Obj(3, 0, 6));
	TSharedPtr<FJsonValue> S5;
	TestTrue(TEXT("setPreset presetId 5 생성"), Dispatch(TEXT("cam.setPreset"), Set5P, S5));
	TestTrue(TEXT("5 created"), BoolOf(S5, TEXT("created"), false));
	TestEqual(TEXT("5 name"), StringOf(S5, TEXT("name")), FString(TEXT("출구")));
	TestTrue(TEXT("5 pan -20"), FMath::IsNearlyEqual(NumberOf(S5, TEXT("pan")), -20.0, 0.01));
	TestEqual(TEXT("listPresets 3건"), ListCount(1), 3);

	// 고치기: presetId 5 의 zoom 만 → name/pan 은 그대로
	TSharedPtr<FJsonObject> UpdP = MakeShared<FJsonObject>();
	UpdP->SetNumberField(TEXT("camId"), 1);
	UpdP->SetStringField(TEXT("fileName"), TestPosFile);
	UpdP->SetNumberField(TEXT("presetId"), 5);
	UpdP->SetNumberField(TEXT("zoom"), 4.0);
	TSharedPtr<FJsonValue> U5;
	TestTrue(TEXT("setPreset 고치기"), Dispatch(TEXT("cam.setPreset"), UpdP, U5));
	TestFalse(TEXT("고치기 created=false"), BoolOf(U5, TEXT("created"), true));
	TestEqual(TEXT("name 유지"), StringOf(U5, TEXT("name")), FString(TEXT("출구")));
	TestTrue(TEXT("pan 유지"), FMath::IsNearlyEqual(NumberOf(U5, TEXT("pan")), -20.0, 0.01));
	TestTrue(TEXT("zoom 4"), FMath::IsNearlyEqual(NumberOf(U5, TEXT("zoom")), 4.0, 0.01));
	TestEqual(TEXT("여전히 3건"), ListCount(1), 3);

	// 파일에서 되읽어도 pan/tilt 가 남는다(rot 동기 저장 확인)
	{
		TSharedPtr<FJsonValue> LR; Dispatch(TEXT("cam.listPresets"), ListP, LR);
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		bool bFound = false;
		if (LR.IsValid() && LR->Type == EJson::Object && LR->AsObject()->TryGetArrayField(TEXT("presets"), Arr))
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				if ((int32)NumberOf(V, TEXT("presetId")) == 5)
				{
					bFound = true;
					TestTrue(TEXT("파일의 5번 pan -20"), FMath::IsNearlyEqual(NumberOf(V, TEXT("pan")), -20.0, 0.01));
					TestEqual(TEXT("파일의 5번 name"), StringOf(V, TEXT("name")), FString(TEXT("출구")));
				}
			}
		}
		TestTrue(TEXT("5번이 파일에 있다"), bFound);
	}

	// 빈 name 거부
	TSharedPtr<FJsonObject> BadP = MakeShared<FJsonObject>();
	BadP->SetNumberField(TEXT("camId"), 1);
	BadP->SetStringField(TEXT("fileName"), TestPosFile);
	BadP->SetStringField(TEXT("name"), TEXT(" "));
	TSharedPtr<FJsonValue> BadR; FRpcError BadE;
	TestFalse(TEXT("빈 name 거부"), D->Dispatch(TEXT("cam.setPreset"), BadP, BadR, BadE));

	// removePreset 2 → 2건 남음, 다시 지우면 -32000
	TSharedPtr<FJsonObject> RmP = MakeShared<FJsonObject>();
	RmP->SetNumberField(TEXT("camId"), 1);
	RmP->SetStringField(TEXT("fileName"), TestPosFile);
	RmP->SetNumberField(TEXT("presetId"), 2);
	TSharedPtr<FJsonValue> RmR;
	TestTrue(TEXT("cam.removePreset 성공"), Dispatch(TEXT("cam.removePreset"), RmP, RmR));
	TestEqual(TEXT("remaining 2"), (int32)NumberOf(RmR, TEXT("remaining")), 2);
	TestEqual(TEXT("listPresets 2건"), ListCount(1), 2);
	TSharedPtr<FJsonValue> Rm2R; FRpcError Rm2E;
	TestFalse(TEXT("없는 프리셋 삭제 거부"), D->Dispatch(TEXT("cam.removePreset"), RmP, Rm2R, Rm2E));
	TestEqual(TEXT("거부 -32000"), Rm2E.Code, Park3DRpc::Domain);

	// 파일 기준 프리셋은 메모리도 파일과 같게 한다 → cam.applyPreset 5 가 loadPreset 없이 성립
	TSharedPtr<FJsonObject> ApP = MakeShared<FJsonObject>();
	ApP->SetNumberField(TEXT("camId"), 1);
	ApP->SetNumberField(TEXT("presetId"), 5);
	TSharedPtr<FJsonValue> ApR;
	TestTrue(TEXT("applyPreset 5(메모리 동기)"), Dispatch(TEXT("cam.applyPreset"), ApP, ApR));
	TestTrue(TEXT("apply pan -20"), FMath::IsNearlyEqual(NumberOf(ApR, TEXT("pan")), -20.0, 0.01));

	DeleteTestPosFile();
	TestFalse(TEXT("테스트 파일 정리"), IFileManager::Get().FileExists(*TestPosFilePath()));
	ResetCameraManager(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
