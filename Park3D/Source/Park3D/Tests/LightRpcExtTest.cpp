// Copyright Epic Games, Inc. All Rights Reserved.
// LightRpcExtTest : 조명 RPC 확장(보드 #1117 A 항목) 검증 - 알 수 없는 키 거절, 조명 파일 kind, deleteFile 가드, 범위 메타.
// HTTP·월드 없이 디스패처를 직접 호출한다(거절·파일 경로 검사는 월드를 보기 전에 끝난다).
// 실제 Save/3D/Light 에 쓰는 검사는 `_AutomationTest_` 접두 이름으로 만들고 끝에 지운다.

#include "Misc/AutomationTest.h"
#include "../Rpc/RpcDispatcher.h"
#include "../Rpc/Modules/LightRpcModule.h"
#include "../Rpc/Modules/FileRpcModule.h"
#include "../Light/LightControlLibrary.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	URpcDispatcher* NewLightDispatcher(FLightRpcModule& Light, FFileRpcModule& File)
	{
		URpcDispatcher* D = NewObject<URpcDispatcher>();
		Light.Register(*D);
		File.Register(*D);
		return D;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightRpcUnknownKeyTest,
	"Park3D.Rpc.LightExt.UnknownKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLightRpcUnknownKeyTest::RunTest(const FString& Parameters)
{
	FLightRpcModule Light([]() -> UWorld* { return nullptr; });
	FFileRpcModule File([]() -> UWorld* { return nullptr; });
	URpcDispatcher* D = NewLightDispatcher(Light, File);

	// 받고 무시하는 것이 가장 나쁜 실패 - 모든 light.* 가 키 이름을 담아 -32602 로 거절해야 한다.
	for (const TCHAR* Method : { TEXT("light.get"), TEXT("light.set"), TEXT("light.reset"), TEXT("light.save"),
		TEXT("light.load"), TEXT("light.setDefault"), TEXT("light.deleteFile") })
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetNumberField(TEXT("bogus"), 1);
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestFalse(*FString::Printf(TEXT("%s bogus 거절"), Method), D->Dispatch(Method, P, R, E));
		TestEqual(*FString::Printf(TEXT("%s 코드 -32602"), Method), E.Code, -32602);
		TestTrue(*FString::Printf(TEXT("%s 메시지에 키 이름"), Method), E.Message.Contains(TEXT("bogus")));
	}

	// 오타 한 글자도 같다(조용히 "아무것도 안 바꿈" 이 되는 경로).
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetNumberField(TEXT("exposureEV10"), 1.0);
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestFalse(TEXT("오타 키 거절"), D->Dispatch(TEXT("light.set"), P, R, E));
		TestTrue(TEXT("오타 키 이름 포함"), E.Message.Contains(TEXT("exposureEV10")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightRpcFileKindTest,
	"Park3D.Rpc.LightExt.FileKind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLightRpcFileKindTest::RunTest(const FString& Parameters)
{
	FLightRpcModule Light([]() -> UWorld* { return nullptr; });
	FFileRpcModule File([]() -> UWorld* { return nullptr; });
	URpcDispatcher* D = NewLightDispatcher(Light, File);

	const FString Name = TEXT("_AutomationTest_light");
	const FString Path = FPaths::Combine(ULightControlLibrary::GetLightDir(), Name + TEXT(".json"));
	FLightSettings S;
	TestTrue(TEXT("시험 파일 저장"), ULightControlLibrary::SaveToFile(Path, S));

	// kind 생략 시 기존 셋만 - light 는 요청해야 나온다.
	{
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestTrue(TEXT("file.list 전체"), D->Dispatch(TEXT("file.list"), nullptr, R, E));
		const TSharedPtr<FJsonObject>* Kinds = nullptr;
		if (R.IsValid() && R->AsObject()->TryGetObjectField(TEXT("kinds"), Kinds))
		{
			TestFalse(TEXT("전체 목록에 light 없음"), (*Kinds)->HasField(TEXT("light")));
			TestEqual(TEXT("전체 목록 kind 수"), (*Kinds)->Values.Num(), 3);
		}
		else { AddError(TEXT("kinds 없음")); }
	}

	// kind=light: dir·current·default·files(내부 파일 표시).
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>(); P->SetStringField(TEXT("kind"), TEXT("light"));
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestTrue(TEXT("file.list light"), D->Dispatch(TEXT("file.list"), P, R, E));
		const TSharedPtr<FJsonObject>* Kinds = nullptr;
		const TSharedPtr<FJsonObject>* Block = nullptr;
		if (R.IsValid() && R->AsObject()->TryGetObjectField(TEXT("kinds"), Kinds) && (*Kinds)->TryGetObjectField(TEXT("light"), Block))
		{
			TestTrue(TEXT("current 키"), (*Block)->HasField(TEXT("current")));
			TestTrue(TEXT("default 키"), (*Block)->HasField(TEXT("default")));
			bool bFound = false;
			const TArray<TSharedPtr<FJsonValue>>* Files = nullptr;
			if ((*Block)->TryGetArrayField(TEXT("files"), Files))
			{
				for (const TSharedPtr<FJsonValue>& V : *Files)
				{
					const TSharedPtr<FJsonObject> Row = V->AsObject();
					if (Row->GetStringField(TEXT("fileName")) == Name + TEXT(".json"))
					{
						bFound = true;
						TestTrue(TEXT("'_' 시작 파일은 internal"), Row->GetBoolField(TEXT("internal")));
					}
				}
			}
			TestTrue(TEXT("시험 파일이 목록에 있음"), bFound);
		}
		else { AddError(TEXT("kinds.light 없음")); }
	}

	// file.read light.
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("kind"), TEXT("light")); P->SetStringField(TEXT("fileName"), Name);
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestTrue(TEXT("file.read light"), D->Dispatch(TEXT("file.read"), P, R, E));
	}

	// 잘못된 kind 메시지는 light 를 안내한다.
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>(); P->SetStringField(TEXT("kind"), TEXT("nope"));
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestFalse(TEXT("잘못된 kind 거절"), D->Dispatch(TEXT("file.list"), P, R, E));
		TestTrue(TEXT("안내에 light"), E.Message.Contains(TEXT("light")));
	}

	IFileManager::Get().Delete(*Path, false, true, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightRpcDeleteFileTest,
	"Park3D.Rpc.LightExt.DeleteFile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLightRpcDeleteFileTest::RunTest(const FString& Parameters)
{
	FLightRpcModule Light([]() -> UWorld* { return nullptr; });
	FFileRpcModule File([]() -> UWorld* { return nullptr; });
	URpcDispatcher* D = NewLightDispatcher(Light, File);

	const FString Name = TEXT("_AutomationTest_del");
	const FString Path = FPaths::Combine(ULightControlLibrary::GetLightDir(), Name + TEXT(".json"));
	auto Call = [&](const FString& FileName, TSharedPtr<FJsonValue>& R, FRpcError& E)
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>(); P->SetStringField(TEXT("fileName"), FileName);
		return D->Dispatch(TEXT("light.deleteFile"), P, R, E);
	};

	// 경로 탈출·'.' 시작은 거절.
	for (const TCHAR* Bad : { TEXT("../x"), TEXT("a/b"), TEXT("..\\x"), TEXT(".hidden") })
	{
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestFalse(*FString::Printf(TEXT("'%s' 거절"), Bad), Call(Bad, R, E));
	}

	// 지워진다.
	{
		TestTrue(TEXT("시험 파일 저장"), ULightControlLibrary::SaveToFile(Path, FLightSettings()));
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestTrue(TEXT("삭제 성공"), Call(Name, R, E));
		TestFalse(TEXT("파일 사라짐"), IFileManager::Get().FileExists(*Path));
		TSharedPtr<FJsonValue> R2; FRpcError E2;
		TestTrue(TEXT("없는 파일도 성공(existed:false)"), Call(Name, R2, E2));
		if (R2.IsValid()) { TestFalse(TEXT("existed false"), R2->AsObject()->GetBoolField(TEXT("existed"))); }
	}

	// 기본 파일(포인터 대상)은 거절하고 파일은 남는다. 포인터는 시험 뒤 원복한다.
	{
		const FString PointerPath = ULightControlLibrary::GetDefaultPointerPath();
		FString SavedPointer;
		const bool bHadPointer = FFileHelper::LoadFileToString(SavedPointer, *PointerPath);

		TestTrue(TEXT("시험 파일 저장"), ULightControlLibrary::SaveToFile(Path, FLightSettings()));
		TestTrue(TEXT("포인터 지정"), ULightControlLibrary::SetDefaultFile(Path));
		TestEqual(TEXT("기본 파일 이름"), ULightControlLibrary::GetDefaultFileName(), Name + TEXT(".json"));

		TSharedPtr<FJsonValue> R; FRpcError E;
		TestFalse(TEXT("기본 파일 삭제 거절"), Call(Name, R, E));
		TestTrue(TEXT("기본 파일은 남음"), IFileManager::Get().FileExists(*Path));

		if (bHadPointer) { FFileHelper::SaveStringToFile(SavedPointer, *PointerPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM); }
		else { IFileManager::Get().Delete(*PointerPath, false, true, true); }
		IFileManager::Get().Delete(*Path, false, true, true);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightRpcMetaTest,
	"Park3D.Rpc.LightExt.Meta",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLightRpcMetaTest::RunTest(const FString& Parameters)
{
	// 메타 범위가 ClampSettings 와 같은 상수에서 나오는지 - 한쪽만 고쳐져 어긋나는 것을 막는다.
	const TArray<ULightControlLibrary::FKeyMeta> Meta = ULightControlLibrary::GetKeyMeta();
	TestEqual(TEXT("키 7개(sunColor 제외)"), Meta.Num(), 7);

	for (const ULightControlLibrary::FKeyMeta& K : Meta)
	{
		FLightSettings Hi, Lo;
		auto Set = [&](FLightSettings& S, float V)
		{
			const FString Key(K.Key);
			if (Key == TEXT("exposureEV100")) S.ExposureEV100 = V;
			else if (Key == TEXT("sunIntensity")) S.SunIntensity = V;
			else if (Key == TEXT("sunAltitudeDeg")) S.SunAltitudeDeg = V;
			else if (Key == TEXT("sunAzimuthDeg")) S.SunAzimuthDeg = V;
			else if (Key == TEXT("skyIntensity")) S.SkyIntensity = V;
			else if (Key == TEXT("shadowFillIntensity")) S.ShadowFillIntensity = V;
			else if (Key == TEXT("carFillIntensity")) S.CarFillIntensity = V;
		};
		auto Get = [&](const FLightSettings& S) -> float
		{
			const FString Key(K.Key);
			if (Key == TEXT("exposureEV100")) return S.ExposureEV100;
			if (Key == TEXT("sunIntensity")) return S.SunIntensity;
			if (Key == TEXT("sunAltitudeDeg")) return S.SunAltitudeDeg;
			if (Key == TEXT("sunAzimuthDeg")) return S.SunAzimuthDeg;
			if (Key == TEXT("skyIntensity")) return S.SkyIntensity;
			if (Key == TEXT("shadowFillIntensity")) return S.ShadowFillIntensity;
			return S.CarFillIntensity;
		};
		Set(Hi, K.Max + 1000.f);
		Set(Lo, K.Min - 1000.f);
		ULightControlLibrary::ClampSettings(Hi);
		ULightControlLibrary::ClampSettings(Lo);
		// 방위는 잘라내지 않고 감는다 - 360 은 0 과 같아 max 가 곧 상한은 아니다(범위 [0,360)).
		if (FString(K.Key) == TEXT("sunAzimuthDeg"))
		{
			TestTrue(TEXT("방위는 0..360 안으로 감김"), Get(Hi) >= 0.f && Get(Hi) < 360.f);
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s 상한"), K.Key), Get(Hi), K.Max, 1e-3f);
		TestEqual(*FString::Printf(TEXT("%s 하한"), K.Key), Get(Lo), K.Min, 1e-3f);
		TestTrue(*FString::Printf(TEXT("%s 기본값이 범위 안"), K.Key), K.Default >= K.Min && K.Default <= K.Max);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
