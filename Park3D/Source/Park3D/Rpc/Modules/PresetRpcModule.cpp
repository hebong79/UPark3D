// Copyright Epic Games, Inc. All Rights Reserved.

#include "PresetRpcModule.h"
#include "CarFilePaths.h"
#include "../RpcDispatcher.h"
#include "../RpcParamUtil.h"
#include "../../ParkingPresetManager.h"
#include "../../PresetMakerWidget.h"
#include "../../ParkingGeometryLibrary.h"
#include "../../Park3DDataPaths.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Policies/CondensedJsonPrintPolicy.h"

namespace
{
	/** preset.setView / preset.getView 공용 — 지금 표시 설정. */
	TSharedPtr<FJsonObject> PresetViewState(const AParkingPresetManager* Mgr)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetBoolField(TEXT("useDecal"), Mgr->bUseDecalView);
		O->SetBoolField(TEXT("show3D"), Mgr->bShow3DView);
		O->SetNumberField(TEXT("lineThickness"), Mgr->LineThickness);
		O->SetNumberField(TEXT("decalThickness"), Mgr->DecalLineThicknessCm);
		O->SetBoolField(TEXT("showNumbers"), Mgr->bShowSlotNumbers);
		O->SetNumberField(TEXT("numberSize"), Mgr->SlotNumberSizeCm);
		O->SetNumberField(TEXT("numberZ"), Mgr->SlotNumberZ);
		O->SetStringField(TEXT("numberMode"), Mgr->bNumbersAnchorsOnly ? TEXT("anchorsOnly") : TEXT("auto"));
		O->SetBoolField(TEXT("global"), true);
		return O;
	}

	/** 프리셋의 비교용 서명 — DTO 를 한 줄로. 변경 메서드가 실제로 바뀌었는지(changed)를 일반적으로 판정한다(보드 #1099). */
	FString PresetSignature(const FParkingPreset& Pr)
	{
		FString Out;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(RpcDto::PresetToDto(Pr).ToSharedRef(), W);
		return Out;
	}

	/** 변경 후 프리셋 DTO + changed(0|1). 값이 그대로면 changed:0 — "회전했다" 가 거짓이 되지 않게. */
	TSharedPtr<FJsonValue> PresetAfterValue(const FParkingPreset& Pr, const FString& BeforeSig)
	{
		TSharedPtr<FJsonObject> O = RpcDto::PresetToDto(Pr);
		O->SetNumberField(TEXT("changed"), PresetSignature(Pr) != BeforeSig ? 1 : 0);
		return MakeShared<FJsonValueObject>(O);
	}

	FString ResolvePresetPath(const TSharedPtr<FJsonObject>& P)
	{
		FString FullPath = RpcParam::GetString(P, TEXT("fullPath"));
		if (!FullPath.IsEmpty())
		{
			return FullPath;
		}
		FString FileName = RpcParam::GetString(P, TEXT("fileName"), TEXT("preset"));
		if (!FileName.EndsWith(TEXT(".json")))
		{
			FileName += TEXT(".json");
		}
		FString Dir = RpcParam::GetString(P, TEXT("path"));
		if (Dir.IsEmpty())
		{
			// 패널(PresetMakerWidget)과 같은 Save 루트를 써야 한다. ProjectDir()/Save 를 직접 조합하면
			// 패키지에서는 그 폴더가 없어 새로 만들어지고, 다음 기동부터 GetSaveRootDir() 가 그 빈 폴더를
			// 골라 config·카메라·차량 파일을 전부 못 읽는다(레벨이 빈 부팅맵에 머문 사고, 2026-09-15).
			return Park3DDataPaths::GetDataFilePath(TEXT("Preset"), *FileName);
		}
		return FPaths::Combine(Dir, FileName);
	}

	/**
	 * preset.importFile 의 쓰기 자리 — fullPath > Save/3D/Preset + fileName, **하나는 필수**(OmiPark3D preset_write_path).
	 * preset.save 처럼 기본 이름(preset.json)으로 조용히 덮어쓰지 않는다. fileName 은 이름만(경로·'.' 시작 불가).
	 *
	 * OmiPark3D 는 fullPath 를 그대로 믿지만 여기서는 car.deleteFile 과 같은 이유로 가둔다 — RPC 서버가
	 * AllowAnonymous 일 수 있어 경로를 믿으면 네트워크의 누구나 아무 데나 .json 을 쓸 수 있다.
	 * 허용 폴더는 Save/3D/Preset 하나(패키지에서는 스테이지 루트 — Park3DDataPaths 가 해석)다.
	 */
	bool ResolvePresetWritePath(const TSharedPtr<FJsonObject>& P, FString& OutPath, FRpcError& OutError)
	{
		const FString Root = CarFilePaths::Normalize(FPaths::GetPath(Park3DDataPaths::GetDataFilePath(TEXT("Preset"), TEXT("x.json"))));

		const FString FullPath = RpcParam::GetString(P, TEXT("fullPath"));
		if (!FullPath.IsEmpty())
		{
			OutPath = CarFilePaths::Normalize(FullPath);
		}
		else
		{
			FString Name;
			if (!RpcParam::RequireString(P, TEXT("fileName"), Name, OutError)) return false;
			Name.TrimStartAndEndInline();
			if (Name.IsEmpty() || Name.Contains(TEXT("/")) || Name.Contains(TEXT("\\")) || Name.StartsWith(TEXT(".")))
			{
				OutError.FailDomain(FString::Printf(TEXT("fileName 이 잘못됐다 — 파일 이름만(경로·'.' 시작 불가): '%s'"), *Name));
				return false;
			}
			if (!Name.EndsWith(TEXT(".json"), ESearchCase::IgnoreCase)) Name += TEXT(".json");
			OutPath = CarFilePaths::Normalize(Root / Name);
		}

		if (!OutPath.EndsWith(TEXT(".json"), ESearchCase::IgnoreCase))
		{
			OutError.FailDomain(FString::Printf(TEXT("프리셋 파일이 아닙니다(.json 만 쓸 수 있습니다): %s"), *OutPath));
			return false;
		}
		if (!CarFilePaths::IsInside(OutPath, Root))
		{
			OutError.FailDomain(FString::Printf(TEXT("프리셋 폴더 밖입니다: %s (허용: %s)"), *OutPath, *Root));
			return false;
		}
		return true;
	}

	/**
	 * content(JSON 문자열 | 객체) → 프리셋 목록. 프리셋 파일 형식(preset.load 가 읽는 {isUnreal, datas:[SDPresetInfo…]})인지
	 * 검증한다 — 루트 datas[] 가 배열이고 객체 항목마다 faceCount 가 있어야 한다(차량·카메라 파일을 거른다, OmiPark3D presets_from_doc).
	 */
	bool PresetsFromContent(const TSharedPtr<FJsonObject>& P, TArray<FParkingPreset>& Out, FRpcError& OutError)
	{
		TSharedPtr<FJsonObject> Doc;
		RpcParam::MarkRead(P, TEXT("content"));
		const TSharedPtr<FJsonValue> Raw = (P.IsValid() && P->Values.Contains(TEXT("content"))) ? P->Values[TEXT("content")] : nullptr;
		if (Raw.IsValid() && Raw->Type == EJson::String)
		{
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw->AsString());
			TSharedPtr<FJsonValue> Parsed;
			if (!FJsonSerializer::Deserialize(Reader, Parsed) || !Parsed.IsValid())
			{
				OutError.FailDomain(FString::Printf(TEXT("JSON 파싱 실패: %s"), *Reader->GetErrorMessage()));
				return false;
			}
			Doc = Parsed->Type == EJson::Object ? Parsed->AsObject() : nullptr;
		}
		else if (Raw.IsValid() && Raw->Type == EJson::Object)
		{
			Doc = Raw->AsObject();
		}
		else
		{
			OutError.FailDomain(TEXT("필수 파라미터 누락: content (프리셋 JSON 문자열 또는 객체)"));
			return false;
		}

		static const TCHAR* FormatError = TEXT("프리셋 파일 형식이 아니다 — 루트 datas[] 에 faceCount 를 가진 프리셋 항목이 하나 이상 있어야 한다");
		const TArray<TSharedPtr<FJsonValue>>* Datas = nullptr;
		if (!Doc.IsValid() || !Doc->TryGetArrayField(TEXT("datas"), Datas))
		{
			OutError.FailDomain(FormatError);
			return false;
		}
		int32 Items = 0;
		for (const TSharedPtr<FJsonValue>& V : *Datas)
		{
			if (!V.IsValid() || V->Type != EJson::Object) continue; // 객체 아닌 항목은 무시(파이썬 동일)
			if (!V->AsObject()->HasField(TEXT("faceCount")))
			{
				OutError.FailDomain(FormatError);
				return false;
			}
			++Items;
		}
		if (Items == 0)
		{
			OutError.FailDomain(FormatError);
			return false;
		}

		FParkingPresetDTOList List;
		if (!FJsonObjectConverter::JsonObjectToUStruct(Doc.ToSharedRef(), &List, 0, 0))
		{
			OutError.FailDomain(FormatError);
			return false;
		}
		Out.Reset();
		Out.Reserve(List.datas.Num());
		for (const FParkingPresetDTO& D : List.datas)
		{
			Out.Add(UPresetMakerWidget::FromDTO(D, List.isUnreal)); // Unity 형식(isUnreal:false)은 언리얼 미터로 정규화
		}
		return Out.Num() > 0;
	}

	/**
	 * preset.save 의 lot(보드 #1185) — 주차장 영역 {points:[{x,y}] m, source?}. SettingManager 의 것이라 해석하지 않고
	 * 파일에 그대로 쓴다. 생략·null 이면 OutLot=nullptr(키를 안 쓴다 — 옛 파일의 lot 을 이어 받지 않는다).
	 * 쓰레기가 파일에 박히지 않게 모양만 본다: 객체, points 배열 3..2000, 점마다 숫자 x·y. 어기면 -32602.
	 */
	bool ReadPresetFileLot(const TSharedPtr<FJsonObject>& P, TSharedPtr<FJsonObject>& OutLot, FRpcError& E)
	{
		OutLot = nullptr;
		RpcParam::MarkRead(P, TEXT("lot"));
		const TSharedPtr<FJsonValue> Raw = (P.IsValid() && P->Values.Contains(TEXT("lot"))) ? P->Values[TEXT("lot")] : nullptr;
		if (!Raw.IsValid() || Raw->IsNull()) return true;

		auto Bad = [&E](const FString& Msg) { E.Fail(Park3DRpc::InvalidParams, TEXT("lot: ") + Msg, ERpcErrorKind::BadParams); return false; };
		if (Raw->Type != EJson::Object) return Bad(TEXT("{points:[{x,y}], source?} 객체 또는 null 이어야 합니다"));
		const TSharedPtr<FJsonObject> Lot = Raw->AsObject();
		const TArray<TSharedPtr<FJsonValue>>* Points = nullptr;
		if (!Lot->TryGetArrayField(TEXT("points"), Points)) return Bad(TEXT("points:[{x,y}] 배열이 필요합니다"));
		if (Points->Num() < 3 || Points->Num() > 2000) return Bad(FString::Printf(TEXT("points 는 3..2000 개 — %d개"), Points->Num()));
		for (int32 i = 0; i < Points->Num(); ++i)
		{
			const TSharedPtr<FJsonObject>* Pt = nullptr;
			double X = 0, Y = 0;
			if (!(*Points)[i]->TryGetObject(Pt) || !Pt || !(*Pt)->TryGetNumberField(TEXT("x"), X) || !(*Pt)->TryGetNumberField(TEXT("y"), Y))
			{
				return Bad(FString::Printf(TEXT("points[%d] 는 숫자 x·y 를 가진 객체여야 합니다"), i));
			}
		}
		OutLot = Lot;
		return true;
	}

	/** 바닥 번호 글자 회전을 0..360 으로 접는다(-90 → 270, 450 → 90). */
	float NormalizeNumberRot(double Deg)
	{
		return static_cast<float>(FRotator::ClampAxis(Deg));
	}

	/** 전달된 키만 프리셋 필드에 반영(update/setSize 공용). */
	void ApplyOptionalFields(const TSharedPtr<FJsonObject>& P, FParkingPreset& Pr)
	{
		if (RpcParam::Has(P, TEXT("faceCount")))    Pr.FaceCount = RpcParam::GetInt(P, TEXT("faceCount"), Pr.FaceCount);
		if (RpcParam::Has(P, TEXT("faceRot")))      Pr.FaceRotate = RpcParam::GetFloat(P, TEXT("faceRot"), Pr.FaceRotate);
		if (RpcParam::Has(P, TEXT("groupRot")))     Pr.GroupFaceRotate = RpcParam::GetFloat(P, TEXT("groupRot"), Pr.GroupFaceRotate);
		if (RpcParam::Has(P, TEXT("xSize")))        Pr.BoxSizeX = RpcParam::GetFloat(P, TEXT("xSize"), Pr.BoxSizeX);
		if (RpcParam::Has(P, TEXT("zSize")))        Pr.BoxSizeZ = RpcParam::GetFloat(P, TEXT("zSize"), Pr.BoxSizeZ);
		if (RpcParam::Has(P, TEXT("useBaseWidth"))) Pr.bIsBaseWidth = RpcParam::GetBool(P, TEXT("useBaseWidth"), Pr.bIsBaseWidth);
		if (RpcParam::Has(P, TEXT("camIdx")))       Pr.CameraIdx = RpcParam::GetInt(P, TEXT("camIdx"), Pr.CameraIdx);
		if (RpcParam::Has(P, TEXT("dirType")))      Pr.DirType = static_cast<EFaceDirType>(FMath::Clamp(RpcParam::GetInt(P, TEXT("dirType"), 0), 0, 1));
		if (RpcParam::Has(P, TEXT("presetName")))   Pr.PresetName = RpcParam::GetString(P, TEXT("presetName"), Pr.PresetName);
		if (RpcParam::Has(P, TEXT("offset")))       Pr.Offset = RpcParam::GetVec3(P, TEXT("offset"), Pr.Offset);
		if (RpcParam::Has(P, TEXT("numberRot")))    Pr.NumberRotate = NormalizeNumberRot(RpcParam::GetFloat(P, TEXT("numberRot"), Pr.NumberRotate));
	}
}

void FPresetRpcModule::Register(URpcDispatcher& Dispatcher)
{
	// ---- 조회/저장 ----
	Dispatcher.Register(TEXT("preset.list"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingPresetManager* Mgr = GetPresetManager(E); if (!Mgr) return nullptr;
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FParkingPreset& Pr : Mgr->GetPresets()) { Arr.Add(RpcDto::PresetToDtoValue(Pr)); }
		return MakeShared<FJsonValueArray>(Arr);
	});

	// 프리셋 메이커 패널의 표시 설정(라인 두께·데칼 두께·데칼/3D 전환)을 RPC 로.
	// 지금까지 preset.rebuildAll{useDecal} 하나뿐이라 두께·3D 전환은 UI 로만 가능했다.
	Dispatcher.Register(TEXT("preset.setView"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingPresetManager* Mgr = GetPresetManager(E); if (!Mgr) return nullptr;

		// numberMode(보드 #1146) — 모르는 값은 아무것도 바꾸기 전에 거절한다(오타가 "auto" 로 조용히 떨어지지 않게).
		bool bAnchorsOnly = Mgr->bNumbersAnchorsOnly;
		if (RpcParam::Has(P, TEXT("numberMode")))
		{
			const FString Mode = RpcParam::GetString(P, TEXT("numberMode"));
			if (Mode == TEXT("auto"))             { bAnchorsOnly = false; }
			else if (Mode == TEXT("anchorsOnly")) { bAnchorsOnly = true; }
			else
			{
				E.Fail(Park3DRpc::InvalidParams, FString::Printf(TEXT("numberMode 는 \"auto\" | \"anchorsOnly\" 입니다: '%s'"), *Mode), ERpcErrorKind::BadParams);
				return nullptr;
			}
		}
		// anchorsOnly = 기준점(cam.setSlotNumber)이 매긴 면에만 번호 **글자**를 그린다. 면·조회·스냅은 그대로다.
		Mgr->bNumbersAnchorsOnly = bAnchorsOnly;

		if (RpcParam::Has(P, TEXT("useDecal")))      { Mgr->bUseDecalView = RpcParam::GetBool(P, TEXT("useDecal"), Mgr->bUseDecalView); }
		if (RpcParam::Has(P, TEXT("show3D")))         { Mgr->bShow3DView = RpcParam::GetBool(P, TEXT("show3D"), Mgr->bShow3DView); }
		if (RpcParam::Has(P, TEXT("lineThickness")))  { Mgr->LineThickness = RpcParam::GetFloat(P, TEXT("lineThickness"), Mgr->LineThickness); }
		if (RpcParam::Has(P, TEXT("decalThickness"))) { Mgr->DecalLineThicknessCm = RpcParam::GetFloat(P, TEXT("decalThickness"), Mgr->DecalLineThicknessCm); }
		// 바닥 주차면 번호 — 패널의 "주차면 번호" 콤보(출력/숨김)와 같은 스위치(매니저 플래그가 주인).
		// numberSize 는 **상한**이다: 실제 글자 높이 = min(numberSize, 면 폭 × 45%) 라 좁은 면에서는
		// 올려도 안 커진다(라인을 넘지 않게 하는 규칙). numberZ 는 면 판 위로 띄우는 상대 높이(cm).
		// 패널에는 없는 값이지만 주차장마다 면 크기·판 높이가 달라 원격 조정 수단이 필요하다.
		// 주의: 이 호출로 바꾼 상태는 열려 있는 패널의 콤보 표시에 즉시 반영되지 않는다(패널은 열릴 때 동기화).
		if (RpcParam::Has(P, TEXT("showNumbers")))    { Mgr->bShowSlotNumbers = RpcParam::GetBool(P, TEXT("showNumbers"), Mgr->bShowSlotNumbers); }
		if (RpcParam::Has(P, TEXT("numberSize")))     { Mgr->SlotNumberSizeCm = RpcParam::GetFloat(P, TEXT("numberSize"), Mgr->SlotNumberSizeCm); }
		if (RpcParam::Has(P, TEXT("numberZ")))        { Mgr->SlotNumberZ = RpcParam::GetFloat(P, TEXT("numberZ"), Mgr->SlotNumberZ); }
		Mgr->RefreshView();
		return RpcDto::MakeObject(PresetViewState(Mgr));
	});

	// preset.getView — setView 의 짝(보드 #1101 ⑤: 모든 표시 set 에 get). 표시 설정은 월드에 한 벌이라 모든 시청자에게 같다.
	Dispatcher.Register(TEXT("preset.getView"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingPresetManager* Mgr = GetPresetManager(E); if (!Mgr) return nullptr;
		return RpcDto::MakeObject(PresetViewState(Mgr));
	});
	Dispatcher.SetMethodMeta(TEXT("preset.getView"), { false, false, TEXT(""),
		TEXT("{useDecal, show3D, lineThickness, decalThickness, showNumbers, numberSize, numberZ, numberMode:auto|anchorsOnly, selectedIdx, global:true}") });

	/**
	 * 바닥에 붙는 주차면 번호 목록. 표시가 꺼져 있어도 계산해서 돌려준다(번호 체계는 표시와 무관하다).
	 * **레벨 면(BP_ParkingSlot 의 ISM)을 나열하는 유일한 RPC 다** — 지금까지는 이 목록이 없어
	 * 커맨드릿(inventory_level.py)으로 뽑아야 했다(2026-09-05 항목).
	 * pos 는 다른 조회들과 같은 **미터**, rotY 는 면 길이축 방향(도).
	 */
	Dispatcher.Register(TEXT("preset.numbers"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingPresetManager* Mgr = GetPresetManager(E); if (!Mgr) return nullptr;

		TArray<FParkingSlotNumberInfo> Slots;
		Mgr->CollectSlotNumbers(Mgr->ResolvePresets(), Slots);

		const float U = Mgr->MetersToUU > 0.f ? Mgr->MetersToUU : 100.f;
		TArray<TSharedPtr<FJsonValue>> Arr;
		int32 PresetCount = 0;
		for (const FParkingSlotNumberInfo& S : Slots)
		{
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetNumberField(TEXT("number"), S.Number);
			// baseNumber 는 기준점(cam.savePreset startFace/startSlot)을 걸기 전 순번, faceKey 는 그 기준점에 넣는 키.
			O->SetNumberField(TEXT("baseNumber"), S.BaseNumber);
			O->SetStringField(TEXT("faceKey"), S.FaceKey());
			// drawn = 바닥에 이 글자를 그리는 판정(numberMode anchorsOnly 에서 기준점 없는 면은 false). 표시 전체가 꺼져도(visible) 판정은 같다.
			O->SetBoolField(TEXT("drawn"), Mgr->ShouldDrawNumber(S));
			O->SetBoolField(TEXT("anchored"), S.bAnchored);
			O->SetStringField(TEXT("source"), S.bFromPreset ? TEXT("preset") : TEXT("level"));
			O->SetObjectField(TEXT("pos"), RpcDto::Vec3(S.Center.X / U, S.Center.Y / U, S.Center.Z / U));
			O->SetNumberField(TEXT("rotY"), FMath::RadiansToDegrees(FMath::Atan2(S.AxisDir.Y, S.AxisDir.X)));
			// textRotY = 바닥 글자의 **위쪽**이 향하는 방향(도, UE yaw). 실제로 그려지는 방향이다(rotY 는 면 축).
			O->SetNumberField(TEXT("textRotY"), FRotator::ClampAxis(FMath::RadiansToDegrees(FMath::Atan2(-S.TextAxis.Y, -S.TextAxis.X))));
			O->SetNumberField(TEXT("widthCm"), S.WidthCm);
			if (S.bFromPreset)
			{
				O->SetNumberField(TEXT("numberRot"), S.NumberRotDeg);
				O->SetNumberField(TEXT("presetId"), S.PresetIdx);
				O->SetNumberField(TEXT("faceSlot"), S.SlotId); // car.list 의 faceSlot 과 같은 공간
				++PresetCount;
			}
			else
			{
				// 레벨 면에는 슬롯 번호가 없다 — 액터·인스턴스가 그 면을 다시 찾는 유일한 키다.
				O->SetStringField(TEXT("levelActor"), S.LevelActor);
				O->SetNumberField(TEXT("levelInstance"), S.LevelInstance);
			}
			Arr.Add(MakeShared<FJsonValueObject>(O));
		}

		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("ok"), true);
		Root->SetBoolField(TEXT("visible"), Mgr->bShowSlotNumbers);
		Root->SetStringField(TEXT("numberMode"), Mgr->bNumbersAnchorsOnly ? TEXT("anchorsOnly") : TEXT("auto"));
		Root->SetNumberField(TEXT("count"), Slots.Num());
		Root->SetNumberField(TEXT("presetCount"), PresetCount);
		Root->SetNumberField(TEXT("levelCount"), Slots.Num() - PresetCount);
		Root->SetArrayField(TEXT("numbers"), Arr);
		return RpcDto::MakeObject(Root);
	});

	Dispatcher.Register(TEXT("preset.get"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingPresetManager* Mgr = GetPresetManager(E); if (!Mgr) return nullptr;
		int32 Idx = 0;
		if (!RpcParam::RequireInt(P, TEXT("idx"), Idx, E)) return nullptr;
		const FParkingPreset* Pr = Mgr->FindPresetByIdx(Idx);
		if (!Pr) { E.FailDomain(FString::Printf(TEXT("프리셋 없음: idx=%d"), Idx)); return nullptr; }
		return RpcDto::PresetToDtoValue(*Pr);
	});

	Dispatcher.Register(TEXT("preset.save"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingPresetManager* Mgr = GetPresetManager(E); if (!Mgr) return nullptr;
		TSharedPtr<FJsonObject> Lot;
		if (!ReadPresetFileLot(P, Lot, E)) return nullptr;
		const FString Path = ResolvePresetPath(P);
		if (!UPresetMakerWidget::SavePresetsToJson(Path, Mgr->GetPresets(), Lot))
		{
			E.FailDomain(FString::Printf(TEXT("프리셋 저장 실패: %s"), *Path));
			return nullptr;
		}
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetStringField(TEXT("path"), Path);
		O->SetStringField(TEXT("fileName"), FPaths::GetCleanFilename(Path));
		O->SetBoolField(TEXT("lot"), Lot.IsValid());
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("preset.load"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingPresetManager* Mgr = GetPresetManager(E); if (!Mgr) return nullptr;
		const FString Path = ResolvePresetPath(P);
		TArray<FParkingPreset> Loaded;
		TSharedPtr<FJsonObject> Lot;
		if (!UPresetMakerWidget::LoadPresetsFromJson(Path, Loaded, &Lot))
		{
			E.FailDomain(FString::Printf(TEXT("프리셋 로드 실패: %s"), *Path));
			return nullptr;
		}
		Mgr->StoredPresets = MoveTemp(Loaded);
		Mgr->SelectedPresetIndex = Mgr->StoredPresets.Num() > 0 ? 0 : INDEX_NONE;
		Mgr->RefreshView();
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("count"), Mgr->StoredPresets.Num());
		// 파일의 주차장 영역(보드 #1185) — 그대로 돌려줄 뿐 그리지 않는다(그리기는 SettingManager 가 preview.show 로). 없으면 키 없음.
		if (Lot.IsValid()) O->SetObjectField(TEXT("lot"), Lot);
		return RpcDto::MakeObject(O);
	});

	/**
	 * preset.importFile — 프리셋 파일 JSON(content) 을 검증해 Save/3D/Preset/<fileName>.json 으로 쓴다(OmiPark3D 확장 이식).
	 * 씬·메모리는 건드리지 않는다(적용은 preset.load). 외부 PC 가 만든 주차면을 이 폴더에 두는 길이다.
	 * 같은 이름은 overwrite:true 로만 덮어쓴다. 저장은 preset.save 와 같은 함수라 항상 isUnreal:true 로 정규화된다.
	 */
	Dispatcher.Register(TEXT("preset.importFile"), [](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		FString Path;
		if (!ResolvePresetWritePath(P, Path, E)) return nullptr;
		TArray<FParkingPreset> Loaded;
		if (!PresetsFromContent(P, Loaded, E)) return nullptr;

		const bool bExisted = IFileManager::Get().FileExists(*Path);
		const bool bOverwrite = RpcParam::GetBool(P, TEXT("overwrite"), false);   // 조건 밖에서 읽는다 — 새 파일일 때도 받은 키다(#1099 경고)
		if (bExisted && !bOverwrite)
		{
			E.FailDomain(FString::Printf(TEXT("이미 있는 파일: %s — overwrite:true 로 덮어쓴다"), *FPaths::GetCleanFilename(Path)));
			return nullptr;
		}
		if (!UPresetMakerWidget::SavePresetsToJson(Path, Loaded))
		{
			E.FailDomain(FString::Printf(TEXT("프리셋 저장 실패: %s"), *Path));
			return nullptr;
		}
		UE_LOG(LogTemp, Log, TEXT("[Preset] 파일 가져오기: %s (%d개, 덮어씀=%s)"), *Path, Loaded.Num(), bExisted ? TEXT("예") : TEXT("아니오"));

		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FParkingPreset& Pr : Loaded) { Arr.Add(RpcDto::PresetToDtoValue(Pr)); }
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetBoolField(TEXT("overwritten"), bExisted);
		O->SetStringField(TEXT("path"), Path);
		O->SetStringField(TEXT("fileName"), FPaths::GetCleanFilename(Path));
		O->SetNumberField(TEXT("count"), Loaded.Num());
		O->SetArrayField(TEXT("presets"), Arr);
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("preset.importFile"), { /*bMutating=*/false, /*bDestructive=*/false,
		TEXT("fileName | fullPath(필수) content(JSON 문자열|객체) overwrite?=false"),
		TEXT("프리셋 파일 JSON({isUnreal, datas:[SDPresetInfo…]}, preset.save 가 쓰는 구조)을 검증해 Save/3D/Preset/<fileName>.json 으로 쓴다 — 메모리는 그대로(적용은 preset.load). 같은 이름은 overwrite:true 로만 → {ok, overwritten, path, fileName, count, presets[]}") });

	// ---- 생성/수정/삭제 ----
	Dispatcher.Register(TEXT("preset.create"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingPresetManager* Mgr = GetPresetManager(E); if (!Mgr) return nullptr;
		FVector Offset; int32 FaceCount = 0;
		if (!RpcParam::RequirePosXZ(P, TEXT("offset"), Offset, E)) return nullptr;
		if (!RpcParam::RequireInt(P, TEXT("faceCount"), FaceCount, E)) return nullptr;

		FParkingPreset Pr;
		// 구조체 기본값이 PresetIdx=1 이라 그대로 두면 AddPreset 의 자동 번호(PresetIdx<=0 조건)가
		// 돌지 않아 만드는 족족 idx=1 이 된다 — 0 으로 낮춰 번호 부여를 매니저에 위임한다.
		Pr.PresetIdx = 0;
		Pr.Offset = Offset;
		Pr.FaceCount = FaceCount;
		Pr.FaceRotate = RpcParam::GetFloat(P, TEXT("faceRot"), 0.0);
		Pr.GroupFaceRotate = RpcParam::GetFloat(P, TEXT("groupRot"), 0.0);
		Pr.DirType = static_cast<EFaceDirType>(FMath::Clamp(RpcParam::GetInt(P, TEXT("dirType"), 0), 0, 1));
		Pr.CameraIdx = RpcParam::GetInt(P, TEXT("camIdx"), 1);
		Pr.bIsBaseWidth = RpcParam::GetBool(P, TEXT("useBaseWidth"), true);
		Pr.BoxSizeX = RpcParam::GetFloat(P, TEXT("xSize"), 2.5);
		Pr.BoxSizeZ = RpcParam::GetFloat(P, TEXT("zSize"), 5.0);
		Pr.NumberRotate = NormalizeNumberRot(RpcParam::GetFloat(P, TEXT("numberRot"), 0.0));

		const int32 NewIdx = Mgr->AddPreset(Pr);
		// 방금 추가한 것은 배열의 끝이다. FindPresetByIdx 는 첫 일치를 돌려주므로
		// idx 가 겹친 목록에서는 남(0번)의 이름을 덮어쓴다.
		FParkingPreset* Added = Mgr->StoredPresets.Num() > 0 ? &Mgr->StoredPresets.Last() : nullptr;
		if (Added)
		{
			Added->PresetName = RpcParam::GetString(P, TEXT("presetName"), FString::Printf(TEXT("Preset_%03d"), NewIdx));
		}
		Mgr->RefreshView();
		return Added ? RpcDto::PresetToDtoValue(*Added) : RpcDto::OkTrue();
	});

	Dispatcher.Register(TEXT("preset.update"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingPresetManager* Mgr = GetPresetManager(E); if (!Mgr) return nullptr;
		int32 Idx = 0;
		if (!RpcParam::RequireInt(P, TEXT("idx"), Idx, E)) return nullptr;
		FParkingPreset* Pr = Mgr->FindPresetByIdx(Idx);
		if (!Pr) { E.FailDomain(FString::Printf(TEXT("프리셋 없음: idx=%d"), Idx)); return nullptr; }
		const FString Before = PresetSignature(*Pr);
		ApplyOptionalFields(P, *Pr);
		Mgr->RefreshView();
		return PresetAfterValue(*Pr, Before);
	});

	Dispatcher.Register(TEXT("preset.delete"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingPresetManager* Mgr = GetPresetManager(E); if (!Mgr) return nullptr;
		int32 Idx = 0;
		if (!RpcParam::RequireInt(P, TEXT("idx"), Idx, E)) return nullptr;
		const bool bExisted = Mgr->RemovePresetByIdx(Idx); // 없어도 예외 없음(Unity 동일) — 대신 existed/changed 로 알린다
		Mgr->RefreshView();
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("idx"), Idx);
		O->SetBoolField(TEXT("existed"), bExisted);
		O->SetNumberField(TEXT("changed"), bExisted ? 1 : 0);
		O->SetNumberField(TEXT("remaining"), Mgr->StoredPresets.Num());
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("preset.clear"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingPresetManager* Mgr = GetPresetManager(E); if (!Mgr) return nullptr;
		const int32 Cleared = Mgr->StoredPresets.Num();
		Mgr->ClearPresets();
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("cleared"), Cleared);
		O->SetNumberField(TEXT("changed"), Cleared);
		return RpcDto::MakeObject(O);
	});

	// ---- 이동/회전/크기 ----
	Dispatcher.Register(TEXT("preset.move"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingPresetManager* Mgr = GetPresetManager(E); if (!Mgr) return nullptr;
		int32 Idx = 0;
		if (!RpcParam::RequireInt(P, TEXT("idx"), Idx, E)) return nullptr;
		FParkingPreset* Pr = Mgr->FindPresetByIdx(Idx);
		if (!Pr) { E.FailDomain(FString::Printf(TEXT("프리셋 없음: idx=%d"), Idx)); return nullptr; }
		const FVector Before = Pr->Offset;

		if (RpcParam::Has(P, TEXT("to")))
		{
			Pr->Offset = RpcParam::GetVec3(P, TEXT("to"), Pr->Offset); // 절대
		}
		else if (RpcParam::Has(P, TEXT("delta")))
		{
			// 상대 — x·y 지면, z 높이(UE 축). 옛 "y 변경 안 함" 은 Unity(y=높이) 잔재라 y 를 조용히 버렸다(보드 #1096).
			const FVector D = RpcParam::GetVec3(P, TEXT("delta"));
			Pr->Offset += D;
		}
		Mgr->RefreshView();
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("idx"), Idx);
		O->SetNumberField(TEXT("x"), Pr->Offset.X);
		O->SetNumberField(TEXT("y"), Pr->Offset.Y);
		O->SetNumberField(TEXT("z"), Pr->Offset.Z);
		O->SetNumberField(TEXT("changed"), Pr->Offset.Equals(Before, 1e-6) ? 0 : 1);
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("preset.rotate"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingPresetManager* Mgr = GetPresetManager(E); if (!Mgr) return nullptr;
		int32 Idx = 0;
		if (!RpcParam::RequireInt(P, TEXT("idx"), Idx, E)) return nullptr;
		FParkingPreset* Pr = Mgr->FindPresetByIdx(Idx);
		if (!Pr) { E.FailDomain(FString::Printf(TEXT("프리셋 없음: idx=%d"), Idx)); return nullptr; }
		const FString Before = PresetSignature(*Pr);
		Pr->FaceRotate += static_cast<float>(RpcParam::GetFloat(P, TEXT("deltaFaceRot"), 0.0));
		Pr->GroupFaceRotate += static_cast<float>(RpcParam::GetFloat(P, TEXT("deltaGroupRot"), 0.0));
		// 바닥 번호 글자만 돌린다(면 기하 불변) — faceRot/groupRot 은 글자 방향을 바꾸지 못한다(보드 #1086).
		Pr->NumberRotate = NormalizeNumberRot(Pr->NumberRotate + RpcParam::GetFloat(P, TEXT("deltaNumberRot"), 0.0));
		Mgr->RefreshView();
		return PresetAfterValue(*Pr, Before);
	});

	Dispatcher.Register(TEXT("preset.groupMove"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingPresetManager* Mgr = GetPresetManager(E); if (!Mgr) return nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Idxs = nullptr;
		RpcParam::MarkRead(P, TEXT("idxs"));
		if (!P.IsValid() || !P->TryGetArrayField(TEXT("idxs"), Idxs)) { E.FailDomain(TEXT("idxs 필수")); return nullptr; }
		const FVector D = RpcParam::GetVec3(P, TEXT("delta"));
		int32 Moved = 0, Changed = 0;
		TArray<TSharedPtr<FJsonValue>> After, NotFound;
		for (const TSharedPtr<FJsonValue>& V : *Idxs)
		{
			if (FParkingPreset* Pr = Mgr->FindPresetByIdx(static_cast<int32>(V->AsNumber())))
			{
				const FVector Before = Pr->Offset;
				Pr->Offset.X += D.X; Pr->Offset.Y += D.Y; Pr->Offset.Z += D.Z; ++Moved;
				if (!Pr->Offset.Equals(Before, 1e-6)) { ++Changed; }
				After.Add(RpcDto::PresetToDtoValue(*Pr));
			}
			else
			{
				NotFound.Add(MakeShared<FJsonValueNumber>(V->AsNumber()));
			}
		}
		Mgr->RefreshView();
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("moved"), Moved);   // 찾은 프리셋 수(옛 키). 실제로 바뀐 수는 changed
		O->SetNumberField(TEXT("changed"), Changed);
		O->SetArrayField(TEXT("presets"), After);
		O->SetArrayField(TEXT("notFound"), NotFound);
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("preset.groupRotate"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingPresetManager* Mgr = GetPresetManager(E); if (!Mgr) return nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Idxs = nullptr;
		RpcParam::MarkRead(P, TEXT("idxs"));
		if (!P.IsValid() || !P->TryGetArrayField(TEXT("idxs"), Idxs)) { E.FailDomain(TEXT("idxs 필수")); return nullptr; }
		const float DFace = static_cast<float>(RpcParam::GetFloat(P, TEXT("deltaFaceRot"), 0.0));
		const float DGroup = static_cast<float>(RpcParam::GetFloat(P, TEXT("deltaGroupRot"), 0.0));
		int32 Rotated = 0, Changed = 0;
		TArray<TSharedPtr<FJsonValue>> After, NotFound;
		for (const TSharedPtr<FJsonValue>& V : *Idxs)
		{
			if (FParkingPreset* Pr = Mgr->FindPresetByIdx(static_cast<int32>(V->AsNumber())))
			{
				const FString Before = PresetSignature(*Pr);
				Pr->FaceRotate += DFace; Pr->GroupFaceRotate += DGroup; ++Rotated;
				if (PresetSignature(*Pr) != Before) { ++Changed; }
				After.Add(RpcDto::PresetToDtoValue(*Pr));
			}
			else
			{
				NotFound.Add(MakeShared<FJsonValueNumber>(V->AsNumber()));
			}
		}
		Mgr->RefreshView();
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("rotated"), Rotated);   // 찾은 프리셋 수(옛 키). 실제로 돈 수는 changed
		O->SetNumberField(TEXT("changed"), Changed);
		O->SetArrayField(TEXT("presets"), After);
		O->SetArrayField(TEXT("notFound"), NotFound);
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("preset.setSize"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingPresetManager* Mgr = GetPresetManager(E); if (!Mgr) return nullptr;
		int32 Idx = 0;
		if (!RpcParam::RequireInt(P, TEXT("idx"), Idx, E)) return nullptr;
		FParkingPreset* Pr = Mgr->FindPresetByIdx(Idx);
		if (!Pr) { E.FailDomain(FString::Printf(TEXT("프리셋 없음: idx=%d"), Idx)); return nullptr; }
		const FString Before = PresetSignature(*Pr);
		if (RpcParam::Has(P, TEXT("xSize")))        Pr->BoxSizeX = RpcParam::GetFloat(P, TEXT("xSize"), Pr->BoxSizeX);
		if (RpcParam::Has(P, TEXT("zSize")))        Pr->BoxSizeZ = RpcParam::GetFloat(P, TEXT("zSize"), Pr->BoxSizeZ);
		if (RpcParam::Has(P, TEXT("useBaseWidth"))) Pr->bIsBaseWidth = RpcParam::GetBool(P, TEXT("useBaseWidth"), Pr->bIsBaseWidth);
		Mgr->RefreshView();
		return PresetAfterValue(*Pr, Before);
	});

	Dispatcher.Register(TEXT("preset.setDirType"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingPresetManager* Mgr = GetPresetManager(E); if (!Mgr) return nullptr;
		int32 Idx = 0, DirType = 0;
		if (!RpcParam::RequireInt(P, TEXT("idx"), Idx, E)) return nullptr;
		if (!RpcParam::RequireInt(P, TEXT("dirType"), DirType, E)) return nullptr;
		FParkingPreset* Pr = Mgr->FindPresetByIdx(Idx);
		if (!Pr) { E.FailDomain(FString::Printf(TEXT("프리셋 없음: idx=%d"), Idx)); return nullptr; }
		const FString Before = PresetSignature(*Pr);
		Pr->DirType = static_cast<EFaceDirType>(FMath::Clamp(DirType, 0, 1));
		Mgr->RefreshView();
		return PresetAfterValue(*Pr, Before);
	});

	// ---- 선택/표시/재빌드 ----
	Dispatcher.Register(TEXT("preset.select"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingPresetManager* Mgr = GetPresetManager(E); if (!Mgr) return nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Idxs = nullptr;
		RpcParam::MarkRead(P, TEXT("idxs"));
		if (P.IsValid() && P->TryGetArrayField(TEXT("idxs"), Idxs))
		{
			// 다중: 렌더러는 단일 강조만 지원 → primary(또는 첫 항목)만 선택.
			int32 Primary = RpcParam::GetInt(P, TEXT("primary"), Idxs->Num() > 0 ? static_cast<int32>((*Idxs)[0]->AsNumber()) : -1);
			Mgr->SetSelectedByIdx(Primary);
			Mgr->RefreshView();
			TArray<TSharedPtr<FJsonValue>> Out;
			for (const TSharedPtr<FJsonValue>& V : *Idxs) { Out.Add(MakeShared<FJsonValueNumber>(V->AsNumber())); }
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetBoolField(TEXT("ok"), true);
			O->SetArrayField(TEXT("idxs"), Out);
			O->SetNumberField(TEXT("primary"), Primary);
			return RpcDto::MakeObject(O);
		}
		const int32 Idx = RpcParam::GetInt(P, TEXT("idx"), -1); // -1=해제
		Mgr->SetSelectedByIdx(Idx);
		Mgr->RefreshView();
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("idx"), Idx);
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("preset.setBoxVisible"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingPresetManager* Mgr = GetPresetManager(E); if (!Mgr) return nullptr;
		int32 Idx = 0; bool bVisible = true;
		if (!RpcParam::RequireInt(P, TEXT("idx"), Idx, E)) return nullptr;
		if (!RpcParam::RequireBool(P, TEXT("visible"), bVisible, E)) return nullptr;
		if (!Mgr->SetBoxVisible(Idx, bVisible))
		{
			E.FailDomain(FString::Printf(TEXT("프리셋 없음: idx=%d"), Idx));
			return nullptr;
		}
		// Unity 는 큐브 오브젝트만 껐다 켜므로 재빌드가 없지만, 이 포트의 큐브는 영구 디버그 라인이라
		// 다시 그리지 않으면 화면이 바뀌지 않는다. 바닥 사각형은 같은 값으로 다시 그려지므로 결과는 동일하다.
		Mgr->RefreshView();

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("idx"), Idx);
		O->SetBoolField(TEXT("visible"), bVisible);
		// 데칼 모드/3D 토글 off 에서는 큐브 자체를 안 그리므로 이 설정이 화면에 나타나지 않는다.
		O->SetBoolField(TEXT("show3D"), Mgr->bShow3DView);
		O->SetBoolField(TEXT("useDecal"), Mgr->bUseDecalView);
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("preset.renumber"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingPresetManager* Mgr = GetPresetManager(E); if (!Mgr) return nullptr;
		const TArray<FParkingSpaceAssignment> Assigns = UParkingGeometryLibrary::CalculateParkingSpaceAssignments(Mgr->GetPresets());
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FParkingSpaceAssignment& A : Assigns)
		{
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetNumberField(TEXT("camIdx"), A.CamIdx);
			O->SetNumberField(TEXT("presetIdx"), A.PresetIdx);
			O->SetNumberField(TEXT("startFaceNum"), A.StartFaceNum);
			O->SetNumberField(TEXT("faceCount"), A.FaceCount);
			TArray<TSharedPtr<FJsonValue>> Nums;
			for (int32 n = A.StartFaceNum; n <= A.EndFaceNum(); ++n) { Nums.Add(MakeShared<FJsonValueNumber>(n)); }
			O->SetArrayField(TEXT("faceNumbers"), Nums);
			Arr.Add(MakeShared<FJsonValueObject>(O));
		}
		return MakeShared<FJsonValueArray>(Arr);
	});

	Dispatcher.Register(TEXT("preset.rebuildAll"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingPresetManager* Mgr = GetPresetManager(E); if (!Mgr) return nullptr;
		Mgr->bShow3DView = RpcParam::GetBool(P, TEXT("showQubeBox"), false);
		// useDecal 은 전달됐을 때만 대입(생략 호출이 데칼 모드를 끄지 않도록).
		if (RpcParam::Has(P, TEXT("useDecal"))) Mgr->bUseDecalView = RpcParam::GetBool(P, TEXT("useDecal"), Mgr->bUseDecalView);
		if (RpcParam::Has(P, TEXT("decalThickness"))) Mgr->DecalLineThicknessCm = RpcParam::GetFloat(P, TEXT("decalThickness"), Mgr->DecalLineThicknessCm);
		Mgr->RefreshView();
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("count"), Mgr->GetPresets().Num());
		O->SetBoolField(TEXT("useDecal"), Mgr->bUseDecalView);
		O->SetBoolField(TEXT("show3D"), Mgr->bShow3DView);
		O->SetNumberField(TEXT("decalThickness"), Mgr->DecalLineThicknessCm);
		return RpcDto::MakeObject(O);
	});
}
