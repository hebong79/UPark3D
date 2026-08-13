// Copyright Epic Games, Inc. All Rights Reserved.

#include "PresetRpcModule.h"
#include "../RpcDispatcher.h"
#include "../RpcParamUtil.h"
#include "../../ParkingPresetManager.h"
#include "../../PresetMakerWidget.h"
#include "../../ParkingGeometryLibrary.h"
#include "Misc/Paths.h"

namespace
{
	FString ResolvePresetPath(const TSharedPtr<FJsonObject>& P)
	{
		FString FullPath = RpcParam::GetString(P, TEXT("fullPath"));
		if (!FullPath.IsEmpty())
		{
			return FullPath;
		}
		FString Dir = RpcParam::GetString(P, TEXT("path"));
		if (Dir.IsEmpty())
		{
			Dir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Save"), TEXT("3D"), TEXT("Preset"));
		}
		FString FileName = RpcParam::GetString(P, TEXT("fileName"), TEXT("preset"));
		if (!FileName.EndsWith(TEXT(".json")))
		{
			FileName += TEXT(".json");
		}
		return FPaths::Combine(Dir, FileName);
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
		const FString Path = ResolvePresetPath(P);
		if (!UPresetMakerWidget::SavePresetsToJson(Path, Mgr->GetPresets()))
		{
			E.FailDomain(FString::Printf(TEXT("프리셋 저장 실패: %s"), *Path));
			return nullptr;
		}
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetStringField(TEXT("path"), Path);
		O->SetStringField(TEXT("fileName"), FPaths::GetCleanFilename(Path));
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("preset.load"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingPresetManager* Mgr = GetPresetManager(E); if (!Mgr) return nullptr;
		const FString Path = ResolvePresetPath(P);
		TArray<FParkingPreset> Loaded;
		if (!UPresetMakerWidget::LoadPresetsFromJson(Path, Loaded))
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
		return RpcDto::MakeObject(O);
	});

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
		ApplyOptionalFields(P, *Pr);
		Mgr->RefreshView();
		return RpcDto::PresetToDtoValue(*Pr);
	});

	Dispatcher.Register(TEXT("preset.delete"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingPresetManager* Mgr = GetPresetManager(E); if (!Mgr) return nullptr;
		int32 Idx = 0;
		if (!RpcParam::RequireInt(P, TEXT("idx"), Idx, E)) return nullptr;
		Mgr->RemovePresetByIdx(Idx); // 없어도 예외 없음(Unity 동일)
		Mgr->RefreshView();
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("idx"), Idx);
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("preset.clear"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingPresetManager* Mgr = GetPresetManager(E); if (!Mgr) return nullptr;
		Mgr->ClearPresets();
		return RpcDto::OkTrue();
	});

	// ---- 이동/회전/크기 ----
	Dispatcher.Register(TEXT("preset.move"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingPresetManager* Mgr = GetPresetManager(E); if (!Mgr) return nullptr;
		int32 Idx = 0;
		if (!RpcParam::RequireInt(P, TEXT("idx"), Idx, E)) return nullptr;
		FParkingPreset* Pr = Mgr->FindPresetByIdx(Idx);
		if (!Pr) { E.FailDomain(FString::Printf(TEXT("프리셋 없음: idx=%d"), Idx)); return nullptr; }

		if (RpcParam::Has(P, TEXT("to")))
		{
			Pr->Offset = RpcParam::GetVec3(P, TEXT("to"), Pr->Offset); // 절대
		}
		else if (RpcParam::Has(P, TEXT("delta")))
		{
			const FVector D = RpcParam::GetVec3(P, TEXT("delta")); // 상대(y 변경 안 함)
			Pr->Offset.X += D.X;
			Pr->Offset.Z += D.Z;
		}
		Mgr->RefreshView();
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("idx"), Idx);
		O->SetNumberField(TEXT("x"), Pr->Offset.X);
		O->SetNumberField(TEXT("y"), Pr->Offset.Y);
		O->SetNumberField(TEXT("z"), Pr->Offset.Z);
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("preset.rotate"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingPresetManager* Mgr = GetPresetManager(E); if (!Mgr) return nullptr;
		int32 Idx = 0;
		if (!RpcParam::RequireInt(P, TEXT("idx"), Idx, E)) return nullptr;
		FParkingPreset* Pr = Mgr->FindPresetByIdx(Idx);
		if (!Pr) { E.FailDomain(FString::Printf(TEXT("프리셋 없음: idx=%d"), Idx)); return nullptr; }
		Pr->FaceRotate += static_cast<float>(RpcParam::GetFloat(P, TEXT("deltaFaceRot"), 0.0));
		Pr->GroupFaceRotate += static_cast<float>(RpcParam::GetFloat(P, TEXT("deltaGroupRot"), 0.0));
		Mgr->RefreshView();
		return RpcDto::PresetToDtoValue(*Pr);
	});

	Dispatcher.Register(TEXT("preset.groupMove"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingPresetManager* Mgr = GetPresetManager(E); if (!Mgr) return nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Idxs = nullptr;
		if (!P.IsValid() || !P->TryGetArrayField(TEXT("idxs"), Idxs)) { E.FailDomain(TEXT("idxs 필수")); return nullptr; }
		const FVector D = RpcParam::GetVec3(P, TEXT("delta"));
		int32 Moved = 0;
		for (const TSharedPtr<FJsonValue>& V : *Idxs)
		{
			if (FParkingPreset* Pr = Mgr->FindPresetByIdx(static_cast<int32>(V->AsNumber())))
			{
				Pr->Offset.X += D.X; Pr->Offset.Y += D.Y; Pr->Offset.Z += D.Z; ++Moved;
			}
		}
		Mgr->RefreshView();
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("moved"), Moved);
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("preset.groupRotate"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingPresetManager* Mgr = GetPresetManager(E); if (!Mgr) return nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Idxs = nullptr;
		if (!P.IsValid() || !P->TryGetArrayField(TEXT("idxs"), Idxs)) { E.FailDomain(TEXT("idxs 필수")); return nullptr; }
		const float DFace = static_cast<float>(RpcParam::GetFloat(P, TEXT("deltaFaceRot"), 0.0));
		const float DGroup = static_cast<float>(RpcParam::GetFloat(P, TEXT("deltaGroupRot"), 0.0));
		int32 Rotated = 0;
		for (const TSharedPtr<FJsonValue>& V : *Idxs)
		{
			if (FParkingPreset* Pr = Mgr->FindPresetByIdx(static_cast<int32>(V->AsNumber())))
			{
				Pr->FaceRotate += DFace; Pr->GroupFaceRotate += DGroup; ++Rotated;
			}
		}
		Mgr->RefreshView();
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("rotated"), Rotated);
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("preset.setSize"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingPresetManager* Mgr = GetPresetManager(E); if (!Mgr) return nullptr;
		int32 Idx = 0;
		if (!RpcParam::RequireInt(P, TEXT("idx"), Idx, E)) return nullptr;
		FParkingPreset* Pr = Mgr->FindPresetByIdx(Idx);
		if (!Pr) { E.FailDomain(FString::Printf(TEXT("프리셋 없음: idx=%d"), Idx)); return nullptr; }
		if (RpcParam::Has(P, TEXT("xSize")))        Pr->BoxSizeX = RpcParam::GetFloat(P, TEXT("xSize"), Pr->BoxSizeX);
		if (RpcParam::Has(P, TEXT("zSize")))        Pr->BoxSizeZ = RpcParam::GetFloat(P, TEXT("zSize"), Pr->BoxSizeZ);
		if (RpcParam::Has(P, TEXT("useBaseWidth"))) Pr->bIsBaseWidth = RpcParam::GetBool(P, TEXT("useBaseWidth"), Pr->bIsBaseWidth);
		Mgr->RefreshView();
		return RpcDto::PresetToDtoValue(*Pr);
	});

	Dispatcher.Register(TEXT("preset.setDirType"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingPresetManager* Mgr = GetPresetManager(E); if (!Mgr) return nullptr;
		int32 Idx = 0, DirType = 0;
		if (!RpcParam::RequireInt(P, TEXT("idx"), Idx, E)) return nullptr;
		if (!RpcParam::RequireInt(P, TEXT("dirType"), DirType, E)) return nullptr;
		FParkingPreset* Pr = Mgr->FindPresetByIdx(Idx);
		if (!Pr) { E.FailDomain(FString::Printf(TEXT("프리셋 없음: idx=%d"), Idx)); return nullptr; }
		Pr->DirType = static_cast<EFaceDirType>(FMath::Clamp(DirType, 0, 1));
		Mgr->RefreshView();
		return RpcDto::PresetToDtoValue(*Pr);
	});

	// ---- 선택/표시/재빌드 ----
	Dispatcher.Register(TEXT("preset.select"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingPresetManager* Mgr = GetPresetManager(E); if (!Mgr) return nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Idxs = nullptr;
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
