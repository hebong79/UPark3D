// Copyright Epic Games, Inc. All Rights Reserved.
// CamRpcModule : cam.* (21 + OmiPark3D 확장 12) 핸들러. Unity CCameraRpcModule 포팅.
// 백엔드: ACameraControlManager(권위) + APTZCameraActor(PTZ) + UCameraControlLibrary + UCamStreamSubsystem.
// savePreset/loadPreset/applyPreset 은 이 모듈이 들고 있는 PresetMemory(FCameraPosList)를 권위로 쓴다.
// 확장 12종(rename/listPresets/setPreset/removePreset/listPosFiles/importPosFile/loadPosFile/savePosFile/
// resetCameras/setMarks/marks/captureStats)은 OmiPark3D cam.py 의 unreal=False 메서드를 그대로 이식한 것이다.

#pragma once

#include "CoreMinimal.h"
#include "../RpcModuleSupport.h"
#include "../../CameraControlTypes.h"

class AStaticMeshActor;
class APTZCameraActor;
class ACameraControlManager;

class FCamRpcModule : public FRpcModuleBase
{
public:
	explicit FCamRpcModule(TFunction<UWorld*()> InWorldGetter) : FRpcModuleBase(MoveTemp(InWorldGetter)) {}

	virtual void Register(URpcDispatcher& Dispatcher) override;

private:
	/**
	 * per-camera 프리셋 인메모리 사본(Unity CSaveInitCampPos 의 m_Data 대응).
	 *  savePreset  : 현재 카메라 상태 → 이 메모리 갱신 + 파일 쓰기(카메라 적용 없음)
	 *  setSlotNumber/slotNumbers : 바닥 번호 지정 목록(slot_numbers — 프리셋 값이 아니라 파일 전체에 하나) 갱신·조회
	 *  loadPreset  : 파일 읽기 → 이 메모리 교체 + 카메라 적용
	 *  applyPreset : 이 메모리를 읽어 카메라 적용(파일 I/O 없음)
	 * 모듈은 URpcServerSubsystem 이 TUniquePtr 로 들고 있어 호출 사이에 살아 있다.
	 */
	FCameraPosList PresetMemory;

	// ---- [확장] 상태 ----

	/** cam.rename 이 준 표시 이름(카메라 액터별). 없으면 "Camera-N". 파일에는 저장하지 않는다(OmiPark3D 와 같다). */
	TMap<TWeakObjectPtr<APTZCameraActor>, FString> CamNames;

	/**
	 * 지금 적용된 CamPos 파일 이름(확장자 포함, 폴더 없음). OmiPark3D 의 cfg.cameraposFile 대응 —
	 * cam.loadPosFile/savePosFile 이 바꾸고, cam.listPresets/setPreset/removePreset 의 기본 파일이 된다.
	 * 첫 사용 때 config_pmaker.json 의 camerapos_file(주차장 항목 우선)로 한 번 채운다.
	 */
	FString CurrentPosFile;
	bool bPosFileProbed = false;

	/** 카메라 위치 표식(cam.setMarks) 켜짐 여부 + 세워 둔 구 액터(태그 CamMark). 재기동 전까지만 — 파일에 저장하지 않는다. */
	bool bMarksEnabled = false;
	TArray<TWeakObjectPtr<AStaticMeshActor>> MarkActors;

	// ---- [확장] 헬퍼 ----

	/** cam.list/cam.get/cam.marks 의 name — rename 된 이름이 있으면 그것, 없으면 "Camera-N". */
	FString CamDisplayName(APTZCameraActor* Cam, int32 CamId) const;

	/** 기본 CamPos 파일 이름(CurrentPosFile, 비어 있으면 config 를 한 번 읽어 본다). 없으면 빈 문자열. */
	FString DefaultPosFileName();

	/** fileName 파라미터 → 파일 경로. 비면 DefaultPosFileName, 그것도 없으면 "CameraPos"(cam.savePreset 과 같은 기본). */
	FString PresetFilePath(const TSharedPtr<FJsonObject>& P);

	/** 파일에 쓰고 메모리를 파일과 같게 한다(바닥 번호 기준점은 월드 권위 → 파일에 같이 쓴다). 실패 시 E 채우고 false. */
	bool SavePosFileDoc(const FString& Path, FCameraPosList& Doc, FRpcError& E);

	/** 파일이 메모리를 교체한다(부분 병합 아님) + 파일의 기준점으로 바닥 번호를 다시 매긴다(cam.loadPreset 과 같다). */
	void AdoptPosFile(const FCameraPosList& Doc);

	/** 표식 전부 제거(월드에 남은 CamMark 태그 액터까지 쓸어낸다). */
	void DestroyMarks();

	/** 표식을 지금 카메라 수·위치대로 다시 세운다(꺼져 있으면 제거만). */
	void RebuildMarks(ACameraControlManager* Mgr);

	/** {enabled, dropM, ballRadiusM, marks:[{camId,name,pos}]} */
	TSharedPtr<FJsonValue> MarksState(ACameraControlManager* Mgr);
};
