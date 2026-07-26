using System;
using System.Collections;
using System.Collections.Generic;
//using GLTFast.Logging;
//using GLTFast.Schema;
using UnityEngine;
using static CSaveInitCampPos;

public class CVlmPSimGameUI : CPSimBaseGameUI
{
   // public Dictionary<int, List<CObjCar>> m_listObjCar = new();   // 프리셋 아이디별 차량 오브젝트 리스트 ( 프리셋 아이디 - 1 부터 시작 )

    public int m_ImgCount = 0;      // 생성된 이미지 카운트 (1부터 시작)
    public float m_FramePerSec = 5;     // 초당 프레임 수 (이미지 저장 간격)
    public float m_StepDelay = 2.0f;     // 차량 번호판 중심으로 이동 후 대기 시간 (초)
    public bool m_IsTrain = true;        // 학습용 데이터 여부(true: train, false: eval)
    // Start is called once before the first execution of Update after the MonoBehaviour is created
    void Start()
    {
        
    }
    public override void Initialize()
    {
        base.Initialize();

        Invoke("Callback_Initialize", 1.0f);
    }

    void Callback_Initialize()
    {
        Initialize_AfterDefaultSetting();
        m_CamObjListUI.SetInitCurCamera();
    }

    public override void Init_Environment()
    {
        base.Init_Environment();
        
    }

    public void StartCreateVLMImages(float fDelay, int nCount, Action<int, int> onEpisodeProgress = null, Action onComplete = null)
    {
        StartCoroutine(Enum_CreateVLMImages(fDelay, nCount, onEpisodeProgress, onComplete));
    }


    IEnumerator Enum_CreateVLMImages(float fDelay, int nCount, Action<int, int> onEpisodeProgress = null, Action onComplete = null)
    {
        yield return null;

        int nCurCount = 0;
        m_ImgCount = 0;

        CSaveSlotOccupyData kSaveSlotOccupyData = new CSaveSlotOccupyData();

        while (nCurCount < nCount)
        {
            onEpisodeProgress?.Invoke(nCurCount + 1, nCount);

            var kCamObjList = m_CamObjListUI.m_CamObjList;
            Debug.Assert(kCamObjList.Count > 0, "카메라 오브젝트가 존재하지 않습니다.!!!");

            //── 차량 재생성 ──────────────────────────────────────────────
            var kCarPosData = CDataMgr.Inst.m_SaveCarPosData;
            m_CarObjListUI.Reset_CreateCarObjectList(kCamObjList[0].getCamera, true);

            // ★ 차량 재생성 후 반드시 프리셋별 리스트 및 환경 정보를 다시 구성
            //   (Reset_CreateCarObjectList 가 새 CObjCar 인스턴스를 생성하므로
            //    m_EnvObjs 안의 parkingVehicles / noiseVehicles 가 이전 인스턴스를 가리켜
            //    HideRandomCars / HideRandomNoiseCars 가 제대로 동작하지 않는 버그 수정)
            ConvertObjCarListByPresetID();
            Init_Environment();

            foreach (KeyValuePair<int, SEnvObj> itor in m_EnvObjs)
            {
                SEnvObj kEnvObj = itor.Value;
                m_CarObjListUI.HideRandomCars(kEnvObj.parkingVehicles);
                m_CarObjListUI.HideRandomNoiseCars(kEnvObj.noiseVehicles);
            }

            //── 카메라별 이미지 캡쳐 ─────────────────────────────────────
            var kCamPosList = CDataMgr.Inst.m_SaveCameraPosData;
            for (int i = 0; i < kCamObjList.Count; i++)
            {
                CObjCamera kCamObj = kCamObjList[i];

                for (int j = 0; j < kCamPosList.Count(); j++)
                {
                    var kDataList = kCamPosList.GetCameraPos(j+1).datas;

                    for (int k = 0; k < kDataList.Count; k++)
                    {
                        SCamDir kCamPos = kDataList[k];

                        //1. 카메라 위치 셋팅
                        kCamObj.transform.position = kCamPos.pos.ToVector3();
                        kCamObj.SetPenTilt(kCamPos.rot.y, kCamPos.rot.x);
                        kCamObj.zoom = kCamPos.zoom;

                        //3. 이미지 캡쳐 및 저장
                        string imgName = string.Format("{0:D4}.jpg", m_ImgCount + 1);
                        SaveFile_ScreenShot(imgName);
                        m_ImgCount++;

                        //4. 주차면 점유 정보 저장
                        SavelotOccupyData_ByParkSlots(kCamPos.preset_id, kSaveSlotOccupyData, imgName);
                        yield return new WaitForSeconds(fDelay);
                    }
                    nCurCount++;
                }
            }
        }
        kSaveSlotOccupyData.SaveFile_Jsonl("Save/CamCapture/manifest.jsonl");
        onComplete?.Invoke();

    }

    public void SavelotOccupyData_ByParkSlots(int presetId, CSaveSlotOccupyData kSaveSlotOccupyData, string imgName)
    {
        List<SSOSlotItem> slotItems = new List<SSOSlotItem>();

        if( m_EnvObjs.ContainsKey(presetId) == false )
        {
            Debug.LogError(string.Format("프리셋 아이디 {0}에 해당하는 환경 정보가 존재하지 않습니다.!!!", presetId));
            return;
        }

        SEnvObj kEnvObj = m_EnvObjs[presetId];
        List<CObjCar> kCarList = kEnvObj.parkingVehicles;
        List<CObjCar> kNoiseCarList = kEnvObj.noiseVehicles;

        for (int i = 0; i < kCarList.Count; i++)
        {
            CObjCar kCarObj = kCarList[i];

            SSOSlotItem slotItem = new SSOSlotItem();
            slotItem.slot = i + 1;
            slotItem.preset_id = presetId;

            // 주차면에 차량이 있는지 여부
            slotItem.occupied = kCarObj.gameObject.activeSelf;
            slotItem.vehicle_type = kCarObj.m_VehicleType.ToString();
            slotItems.Add(slotItem);

            //약간회전, 약간 앞뒤 이동 추가
            if (kCarObj.gameObject.activeSelf)
            {
                bool isFront = SetRandomCarPos_ByParkSlot(i, kCarObj.gameObject);
                slotItem.is_front = isFront;
            }
        }
        kSaveSlotOccupyData.Add(imgName, slotItems);


        for( int i = 0; i < kNoiseCarList.Count; i++)
        {
            CObjCar kNoiseCarObj = kNoiseCarList[i];

            //약간회전, 약간 앞뒤 이동 추가
            if (kNoiseCarObj.gameObject.activeSelf)
            {
                bool isFront = SetRandomCarPos_ByParkSlot(-1, kNoiseCarObj.gameObject);
            }
        }
    }

    public void StartCreateVLAImages(bool isTrain, float fEpisodeDelay, int nEpisodeCount, float fStepDelay = 2f, float framePerSec = 5f, 
                                     Action<int, int> onEpisodeProgress = null, Action onComplete = null)
    {
        m_FramePerSec = framePerSec;
        m_StepDelay = fStepDelay;
        m_IsTrain = isTrain;
        StartCoroutine(Enum_CreateVLAImages(fEpisodeDelay, nEpisodeCount, onEpisodeProgress, onComplete));
    }

    IEnumerator Enum_CreateVLAImages(float fEpisodeDelay, int nEpisodeCount,  Action<int, int> onEpisodeProgress = null, Action onComplete = null)
    {
        
        yield return null;

        int nCurCount = 0;
        m_ImgCount = 0;

        // ✅ [GC 최적화] WaitForSeconds 캐싱
        WaitForSeconds kEpisodeDelayWait = new WaitForSeconds(fEpisodeDelay);  // 에피소드 간 대기 시간
        WaitForSeconds kEpStartDelayWait = new WaitForSeconds(1.0f);           // 에피소드 시작 후 초기 안정화 대기 시간 (카메라 위치 셋팅 및 차량 재생성 후)

        //CSaveSlotOccupyData kSaveSlotOccupyData = new CSaveSlotOccupyData();
        CSaveCenteriseData kSaveCenteriseData = new CSaveCenteriseData();

        // ✅ [메모리 절약 준비] 에피소드마다 appendMode로 저장하므로 시작 전 기존 파일 초기화
        string sPrefix = m_IsTrain ? "train" : "eval";
        string kManifestPath = $"Save/CamCaptureVLA/{sPrefix}_manifest.jsonl";
        if (System.IO.File.Exists(kManifestPath))
            System.IO.File.Delete(kManifestPath);

        while (nCurCount < nEpisodeCount)
        {
            // 에피소드 시작 시 진행 상황 콜백 호출 (현재 에피소드 번호는 1-based)
            onEpisodeProgress?.Invoke(nCurCount + 1, nEpisodeCount);

            var kCamObjList = m_CamObjListUI.m_CamObjList;
            Debug.Assert(kCamObjList.Count > 0, "카메라 오브젝트가 존재하지 않습니다.!!!");

            //── 차량 재생성 ──────────────────────────────────────────────
            var kCarPosData = CDataMgr.Inst.m_SaveCarPosData;
            m_CarObjListUI.Reset_CreateCarObjectList(kCamObjList[0].getCamera, true);
            

            // ★ 차량 재생성 후 반드시 프리셋별 리스트 및 환경 정보를 다시 구성
            //   (Reset_CreateCarObjectList 가 새 CObjCar 인스턴스를 생성하므로
            //    m_EnvObjs 안의 parkingVehicles / noiseVehicles 가 이전 인스턴스를 가리켜
            //    HideRandomCars / HideRandomNoiseCars 가 제대로 동작하지 않는 버그 수정)
            ConvertObjCarListByPresetID();
            Init_Environment();

            foreach (KeyValuePair<int, SEnvObj> itor in m_EnvObjs)
            {
                SEnvObj kEnvObj = itor.Value;
                m_CarObjListUI.HideRandomCars(kEnvObj.parkingVehicles);
                m_CarObjListUI.HideRandomNoiseCars(kEnvObj.noiseVehicles);
            }

            m_CarObjListUI.SetRandomColorOfCarList();

            //── 카메라별 이미지 캡쳐 ─────────────────────────────────────
            yield return MakeImage_VlaCenterising(kCamObjList, kSaveCenteriseData, kEpStartDelayWait, kEpisodeDelayWait);

            nCurCount++;
        }
        //kSaveSlotOccupyData.SaveFile_Jsonl("Save/CamCapture/manifest.jsonl");
        // ✅ 에피소드마다 이미 저장했으므로 마지막에는 남은 데이터가 없음 (appendMode 처리됨)

        // 전체 완료 콜백 호출
        onComplete?.Invoke();
    }

    public IEnumerator MakeImage_VlaCenterising(List<CObjCamera> kCamObjList, CSaveCenteriseData kSaveCenteriseData,
                                                        WaitForSeconds kEpStartDelayWait, WaitForSeconds kEpisodeDelayWait)
    {

        string sPrefix = m_IsTrain ? "train" : "eval";

        //── 카메라별 이미지 캡쳐 ─────────────────────────────────────
        var kCamPosList = CDataMgr.Inst.m_SaveCameraPosData;   // 카메라 리스트(주의: CObjCamera 리스트와 1:1 매칭되어야 함)
        for (int i = 0; i < kCamObjList.Count; i++)
        {
            CObjCamera kCamObj = kCamObjList[i];
            var kDataList = kCamPosList.GetCameraPos(i + 1).datas;

            //var kDataList = kCamPosList[j].GetCameraPos().datas;
            // 카메라내 프리셋 리스트 순회
            for (int k = 0; k < kDataList.Count; k++)
            {
                SCamDir kCamPos = kDataList[k];

                //1. 카메라 위치 셋팅
                kCamObj.transform.position = kCamPos.pos.ToVector3();
                kCamObj.SetPenTilt(kCamPos.rot.y, kCamPos.rot.x);
                kCamObj.zoom = kCamPos.zoom;

                RandomFrontBack_CarObjectList(kCamPos.preset_id);

                //2. 프리셋(P1존) 이미지 캡쳐 및 저장
                //string sPrefix = m_IsTrain ? "train" : "eval";
                string imgName = string.Format("{0}_{1:D4}.jpg", sPrefix, m_ImgCount + 1);
                SaveFile_ScreenShot(imgName, "Save/CamCaptureVLA/Images");
                m_ImgCount++;

                SCenterPresetData kCenterPresetData = new SCenterPresetData();
                //kCenterPresetData.preset_id = kCamPos.preset_id;
                //kCenterPresetData.cam_id = kCamObj.m_Idx;
                kCenterPresetData.pan = kCamPos.Pan();
                kCenterPresetData.tilt = kCamPos.Tilt();
                kCenterPresetData.zoom = kCamPos.zoom;
                kCenterPresetData.img = imgName;
                kSaveCenteriseData.Add(kCenterPresetData);

                yield return kEpStartDelayWait; // P1존 이미지 캡쳐 후 잠시대기(0.5초) - 카메라 위치 안정화 및 차량 재배치 완료 대기

                //3. 프리셋 위 차량 정보 저장 (프리셋 아이디, target_slot_id, ptz, img이름, 차량앞면 여부)
                // 프리셋 위  차량 번호판 중심으로 이동. 이미지를 프레임별 저장
                yield return SetCenteriseData(kCamPos.preset_id, kCenterPresetData, kCamObj, m_IsTrain);

                //yield return kEpisodeDelayWait;
            }
        }
        // ✅ [메모리 절약] 에피소드 단위로 JSONL 플러시 후 데이터 초기화
        // kSaveCenteriseData가 100 에피소드 동안 누적되면 managed memory가 과도하게 증가할 수 있음.
        // 에피소드마다 저장 후 Clear하여 메모리 상주량을 최소화.
      
        kSaveCenteriseData.SaveFile_Jsonl($"Save/CamCaptureVLA/{sPrefix}_manifest.jsonl", appendMode: true);
        kSaveCenteriseData.Clear();
    }

    protected IEnumerator SetCenteriseData(int presetId, SCenterPresetData kCenterPresetData, CObjCamera kCamObj, bool isTrain)
    {
        if (m_EnvObjs.ContainsKey(presetId) == false)
        {
            Debug.LogError(string.Format("프리셋 아이디 {0}에 해당하는 환경 정보가 존재하지 않습니다.!!!", presetId));
            yield break;
        }

        yield return Enum_CenterisingByFrame(presetId, kCenterPresetData, kCamObj, isTrain);


        //kSaveCenteriseData.Add(kCenterData);
    }

    /// <summary>
    /// 카메라를 현재 Pan/Tilt/FOV 에서 목표값까지 초당 5프레임(0.2초) 간격으로 보간 이동한다.
    /// </summary>
    /// <param name="kCamObj">이동할 CObjCamera</param>
    /// <param name="targetPan">목표 Pan 각도 (도)</param>
    /// <param name="targetTilt">목표 Tilt 각도 (도)</param>
    /// <param name="targetFov">목표 수직 FOV (도)</param>
    /// <param name="duration">전체 보간 시간 (초, 기본 1.0f)</param>
    protected IEnumerator Enum_LerpCameraToTarget(SCenterPresetData kCenterPresetData, CObjCamera kCamObj, int targetSlotId, 
                                    float targetPan, float targetTilt, float targetFov, bool isTrain, float duration = 1.0f)
    {
        float kLerpFPS = m_FramePerSec;
        float interval = 1.0f / kLerpFPS;   // 0.2초 간격 = 초당 5프레임

        // ✅ [GC 최적화] WaitForSeconds 캐싱 - 루프마다 new 생성 방지
        WaitForSeconds kWait = new WaitForSeconds(interval);

        float startPan  = kCamObj.pan;
        float startTilt = kCamObj.tilt;
        float startFov  = kCamObj.getCamera.fieldOfView;

        // [버그수정] elapsed를 interval부터 시작(t=0 이미지 제거):
        //   t=0은 카메라가 아직 목표 차량 방향으로 이동하지 않은 이전 위치(이전 차량 or 전체씬뷰)이므로
        //   현재 targetSlotId로 레이블링하면 슬롯-이미지 불일치가 발생한다.
        //   t=interval(첫 번째 실제 이동 위치)부터 저장하여 레이블 정확도를 보장한다.
        float elapsed = interval;
        while (elapsed < duration + interval * 0.5f)  // 부동소수점 오차 허용: t=1.0 포함
        {
            float t = Mathf.Clamp01(elapsed / duration);

            kCamObj.pan  = Mathf.LerpAngle(startPan,  targetPan,  t);
            kCamObj.tilt = Mathf.LerpAngle(startTilt, targetTilt, t);
            kCamObj.SetCameraFOV(Mathf.Lerp(startFov, targetFov, t));

            // CaptureImage 내부에서 kCamera.Render()를 직접 호출하므로
            // yield return 없이도 현재 카메라 이동 상태가 즉시 캡처된다.
            string sPrefix = isTrain ? "train" : "eval";
            string imgName = string.Format("{0}_{1:D4}.jpg", sPrefix, m_ImgCount + 1);
            SaveFile_ScreenShot(imgName, "Save/CamCaptureVLA/Images");
            m_ImgCount++;

            AddCenteriseItem(kCenterPresetData, kCamObj, slotId: targetSlotId, imgName, false);

            yield return kWait;

            elapsed += interval;
        }

        // 최종값 정확히 적용 (t=1.0 보정)
        kCamObj.pan  = targetPan;
        kCamObj.tilt = targetTilt;
        kCamObj.SetCameraFOV(targetFov);
        // yield return null 제거: FocusOnLicensePlate while 루프가 지연 없이 즉시 시작되도록 한다.
        // (CaptureImage가 즉시 렌더링하므로 추가 프레임 대기 불필요)
    }

    IEnumerator Enum_CenterisingByFrame(int presetId, SCenterPresetData kCenterPresetData, CObjCamera kCamObj, bool isTrain)
    {
        // [버그수정] 불필요한 yield return null 제거:
        //   호출 전(MakeImage_VlaCenterising)에서 이미 kEpStartDelayWait(0.5초) 대기를 완료했으므로
        //   추가 1프레임 지연은 불필요하며, 첫 번째 차량의 CalcTargetPTZ 계산 타이밍에 영향을 줄 수 있음.

        SEnvObj kEnvObj = m_EnvObjs[presetId];
        List<CObjCar> kCarList = kEnvObj.parkingVehicles;

        float fDelay = 1.0f / m_FramePerSec;
        // ✅ [GC 최적화] WaitForSeconds 캐싱 - 루프마다 new 생성 방지
        WaitForSeconds kFocusWait = new WaitForSeconds(fDelay);

        WaitForSeconds kSetpWait = new WaitForSeconds(m_StepDelay);  // 차량 번호판 중심으로 이동 후 대기 시간

        for (int i = 0; i < kCarList.Count; i++)
        {
            CObjCar kCarObj = kCarList[i];
            if (!kCarObj.IsShow())
                continue;

            bool isFront = kCarObj.m_IsFront;

            Debug.Log($"[Center] index = {i}, slot = {kCarObj.m_iFaceSlot}, preset id = {presetId}, time = {DateTime.Now}");


            // 현재 차량 번호판 위치까지 보간법을 이용해 카메라 이동 ( 초당 5프레임 간격)
            // ※ 이전에는 다음 차량(i+1)을 타겟으로 사용해 오버슈트가 발생했음.
            //   FocusOnLicensePlate 도 같은 car[i]를 대상으로 하므로, 보간 타겟도 car[i]로 통일한다.
            float fTargetPan, fTargetTilt, fTargetFov;
            if (CLicensePlateFocusCapture.CalcTargetPTZ(kCamObj, kCarObj, isFront, out fTargetPan, out fTargetTilt, out fTargetFov))
            {
                //// 최종값 정확히 적용 (t=1.0 보정)
                //kCamObj.pan = fTargetPan;
                //kCamObj.tilt = fTargetTilt;
                //kCamObj.SetCameraFOV(fTargetFov);

                yield return StartCoroutine(Enum_LerpCameraToTarget(kCenterPresetData, kCamObj, kCarObj.m_iFaceSlot,  fTargetPan, fTargetTilt, fTargetFov, isTrain));
                // Enum_LerpCameraToTarget 내부에서 최종값 적용 후 yield return null로
                // 렌더링 반영을 보장하므로 여기서는 추가 대기 불필요
            }

            //3. 스크린샷 저장: CaptureImage 내부에서 kCamera.Render()를 직접 호출하므로
            //   이동 직후 즉시 저장해도 현재 카메라 방향이 정확히 캡처된다.
            string sPrefix = isTrain ? "train" : "eval";
            string imgName = string.Format("{0}_{1:D4}.jpg", sPrefix, m_ImgCount + 1);
            SaveFile_ScreenShot(imgName, "Save/CamCaptureVLA/Images");
            m_ImgCount++;

            //4. kSaveCenteriseData 정보저장( 프리셋id, target_slot_id, ptz, img이름, 차량앞면 여부)
            AddCenteriseItem(kCenterPresetData, kCamObj, kCarObj.m_iFaceSlot, imgName, true);
            yield return kSetpWait;

            //// 보간 이동이 끝난 후에도 차량 번호판 중심으로 카메라 고정 (보간 중에는 차량이동으로 인해 중심이 어긋날 수 있으므로)
            ////// ✅ [GC 최적화] WaitForSeconds 캐싱 - 루프마다 new 생성 방지
            //float curtime = 0f;
            //while ( curtime <= m_StepDelay) // 차량번호판 중심으로 이동 후 m_StepDelay초간 프레임별 이미지 저장
            //{
            //    //2. 카메라를 차량 번호판 중심으로 이동 + 줌
            //    CLicensePlateFocusCapture.FocusOnLicensePlate(kCamObj, kCarObj, isFront);

            //    //3. 스크린샷 저장: CaptureImage 내부에서 kCamera.Render()를 직접 호출하므로
            //    //   이동 직후 즉시 저장해도 현재 카메라 방향이 정확히 캡처된다.
            //    string sPrefix = isTrain ? "train" : "eval";
            //    string imgName = string.Format("{0}_{1:D4}.jpg", sPrefix, m_ImgCount + 1);
            //    SaveFile_ScreenShot(imgName, "Save/CamCaptureVLA/Images");
            //    m_ImgCount++;

            //    //4. kSaveCenteriseData 정보저장( 프리셋id, target_slot_id, ptz, img이름, 차량앞면 여부)
            //    AddCenteriseItem(kCenterPresetData, kCamObj, presetId, kCarObj.m_iFaceSlot, imgName);

            //    curtime += fDelay;
            //    yield return kFocusWait;
            //}

        }
    }

  
    // kSaveCenteriseData에 SCenteriseItem 정보저장( 프리셋id, target_slot_id, ptz, img이름, 차량앞면 여부)
    // isMoveComplete: 차량번호판 중심으로 이동이 완료 되었는지 여부 (true: 이동 완료, false: 이동 중 )
    public SCenteriseItem AddCenteriseItem(SCenterPresetData kCenterPresetData, CObjCamera kCamObj, int slotId, string imgName, bool isMoveComplete)
    {
        SCenteriseItem kCenterItem = new SCenteriseItem();
        kCenterItem.img = imgName;
        kCenterItem.pan = kCamObj.pan;
        kCenterItem.tilt = kCamObj.tilt;
        kCenterItem.zoom = kCamObj.zoom;
        kCenterItem.target_slot = slotId;
        kCenterItem.check = isMoveComplete ? 1 : 0;
        //kCenterItem.preset_id = presetId;
        //kCenterItem.cam_id = kCamObj.m_Idx;

        kCenterPresetData.Add(kCenterItem);
        return kCenterItem;
    }

    ////===========================================================================================
    //// 평가용 이미지 생성 시작 
    //public void StartCreateVLAImages_Evaluation(float fEpisodeDelay, int nEpisodeCount, float fStepDelay = 2f, float framePerSec = 5f, Action<int, int> onEpisodeProgress = null, System.Action onComplete = null)
    //{
    //    m_FramePerSec = framePerSec;
    //    m_StepDelay = fStepDelay;
    //    StartCoroutine(Enum_CreateVLAImages_Evaluation(fEpisodeDelay, nEpisodeCount, onEpisodeProgress, onComplete));
    //}

    //IEnumerator Enum_CreateVLAImages_Evaluation(float fEpisodeDelay, int nEpisodeCount, Action<int, int> onEpisodeProgress = null, System.Action onComplete = null)
    //{

    //    yield return null;

    //    int nCurCount = 0;
    //    m_ImgCount = 0;

    //    // ✅ [GC 최적화] WaitForSeconds 캐싱
    //    WaitForSeconds kEpisodeDelayWait = new WaitForSeconds(fEpisodeDelay);  // 에피소드 간 대기 시간
    //    WaitForSeconds kEpStartDelayWait = new WaitForSeconds(0.5f);            // 에피소드 시작 후 초기 안정화 대기 시간 (카메라 위치 셋팅 및 차량 재생성 후)

    //    //CSaveSlotOccupyData kSaveSlotOccupyData = new CSaveSlotOccupyData();
    //    CSaveCenteriseData kSaveCenteriseData = new CSaveCenteriseData();

    //    // ✅ [메모리 절약 준비] 에피소드마다 appendMode로 저장하므로 시작 전 기존 파일 초기화
    //    const string kManifestPath = "Save/CamCaptureVLA/eval_manifest.jsonl";
    //    if (System.IO.File.Exists(kManifestPath))
    //        System.IO.File.Delete(kManifestPath);

    //    while (nCurCount < nEpisodeCount)
    //    {
    //        // 에피소드 시작 시 진행 상황 콜백 호출 (현재 에피소드 번호는 1-based)
    //        onEpisodeProgress?.Invoke(nCurCount + 1, nEpisodeCount);

    //        var kCamObjList = m_CamObjListUI.m_CamObjList;
    //        Debug.Assert(kCamObjList.Count > 0, "카메라 오브젝트가 존재하지 않습니다.!!!");

    //        //── 차량 재생성 ──────────────────────────────────────────────
    //        var kCarPosData = CDataMgr.Inst.m_SaveCarPosData;
    //        m_CarObjListUI.Reset_CreateCarObjectList(kCamObjList[0].getCamera, true);

    //        // ★ 차량 재생성 후 반드시 프리셋별 리스트 및 환경 정보를 다시 구성
    //        //   (Reset_CreateCarObjectList 가 새 CObjCar 인스턴스를 생성하므로
    //        //    m_EnvObjs 안의 parkingVehicles / noiseVehicles 가 이전 인스턴스를 가리켜
    //        //    HideRandomCars / HideRandomNoiseCars 가 제대로 동작하지 않는 버그 수정)
    //        ConvertObjCarListByPresetID();
    //        Init_Environment();

    //        foreach (KeyValuePair<int, SEnvObj> itor in m_EnvObjs)
    //        {
    //            SEnvObj kEnvObj = itor.Value;
    //            m_CarObjListUI.HideRandomCars(kEnvObj.parkingVehicles);
    //            m_CarObjListUI.HideRandomNoiseCars(kEnvObj.noiseVehicles);
    //        }

    //        //── 카메라별 이미지 캡쳐 (train과 똑같다. 단 jsonl 파일이름만 다르다.) ────────────────────
    //        yield return MakeImage_VlaCenterising_Evaluation(kCamObjList, kSaveCenteriseData, kEpStartDelayWait, kEpisodeDelayWait);

    //        nCurCount++;
    //    }
    //    //kSaveSlotOccupyData.SaveFile_Jsonl("Save/CamCapture/manifest.jsonl");
    //    // ✅ 에피소드마다 이미 저장했으므로 마지막에는 남은 데이터가 없음 (appendMode 처리됨)

    //    // 전체 완료 콜백 호출
    //    onComplete?.Invoke();
    //}

    //public IEnumerator MakeImage_VlaCenterising_Evaluation(List<CObjCamera> kCamObjList, CSaveCenteriseData kSaveCenteriseData,
    //    WaitForSeconds kEpStartDelayWait, WaitForSeconds kEpisodeDelayWait)
    //{
    //    //── 카메라별 이미지 캡쳐 ─────────────────────────────────────
    //    var kCamPosList = CDataMgr.Inst.m_SaveCamPosList;   // 카메라 리스트(주의: CObjCamera 리스트와 1:1 매칭되어야 함)
    //    for (int i = 0; i < kCamObjList.Count; i++)
    //    {
    //        CObjCamera kCamObj = kCamObjList[i];
    //        var kDataList = kCamPosList[i].m_CamDirList.datas;

    //        //var kDataList = kCamPosList[j].m_CamDirList.datas;
    //        // 카메라내 프리셋 리스트 순회
    //        for (int k = 0; k < kDataList.Count; k++)
    //        {
    //            SCamDir kCamPos = kDataList[k];

    //            //1. 카메라 위치 셋팅
    //            kCamObj.transform.position = kCamPos.pos.ToVector3();
    //            kCamObj.SetPenTilt(kCamPos.rot.y, kCamPos.rot.x);
    //            kCamObj.zoom = kCamPos.zoom;

    //            RandomFrontBack_CarObjectList(kCamPos.preset_id);

    //            //2. 프리셋(P1존) 이미지 캡쳐 및 저장
    //            string imgName = string.Format("eval_{0:D4}.jpg", m_ImgCount + 1);
    //            SaveFile_ScreenShot(imgName, "Save/CamCaptureVLA/Images");
    //            m_ImgCount++;

    //            SCenterPresetData kCenterPresetData = new SCenterPresetData();
    //            kCenterPresetData.preset_id = kCamPos.preset_id;
    //            //kCenterPresetData.cam_id = kCamObj.m_Idx;
    //            kCenterPresetData.pen = kCamPos.Pen();
    //            kCenterPresetData.tilt = kCamPos.Tilt();
    //            kCenterPresetData.zoom = kCamPos.zoom;
    //            kCenterPresetData.img = imgName;
    //            kSaveCenteriseData.Add(kCenterPresetData);

    //            yield return kEpStartDelayWait; // P1존 이미지 캡쳐 후 잠시대기(0.5초) - 카메라 위치 안정화 및 차량 재배치 완료 대기

    //            //3. 프리셋 위 차량 정보 저장 (프리셋 아이디, target_slot_id, ptz, img이름, 차량앞면 여부)
    //            // 프리셋 위  차량 번호판 중심으로 이동. 이미지를 프레임별 저장
    //            yield return SetCenteriseData(kCamPos.preset_id, kCenterPresetData, kCamObj, false);

    //            yield return kEpisodeDelayWait;
    //        }
    //    }
    //    // ✅ [메모리 절약] 에피소드 단위로 JSONL 플러시 후 데이터 초기화
    //    // kSaveCenteriseData가 100 에피소드 동안 누적되면 managed memory가 과도하게 증가할 수 있음.
    //    // 에피소드마다 저장 후 Clear하여 메모리 상주량을 최소화.
    //    kSaveCenteriseData.SaveFile_Jsonl("Save/CamCaptureVLA/eval_manifest.jsonl", appendMode: true);
    //    kSaveCenteriseData.Clear();
    //}



}
