using System;
using UnityEngine;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using static CSaveInitCampPos;



/// <summary>
/// VLA Parking Simulator 이미지 메이커 게임 UI 클래스
///
///  [ tilt/zoom은 아래값을 충족해야 하늘을 보지않고 번호판이 잘 보이는 이미지가 생성됨 (카메라 프리셋 모두 동일 )]
///  tilt = min : 5  max : 25 범위
///  zoom = min : 1  max : 5 범위
///  -------------------------------------------------------------------
///  pan = min : -180 max : 180 범위 (카메라 프리셋 마다 다름 )
/// </summary>
public class CVlaPSImgMakerGameUI : CPSimBaseGameUI
{
    public enum EMakeMode
    {
        Train,          // 학습용 데이터 생성 모드 (manifest 파일명 접두사 "train_")
        Evaluation,     // 평가용 데이터 생성 모드 (manifest 파일명 접두사 "eval_")
        Test            // 테스트용 데이터 생성 모드 (manifest 파일명 접두사 "test_")
    }

    private const string kImgFolder      = "Save/CamCaptureVLA/Images";
    private const string kPlateImgFolder = "Save/CamCaptureVLA/PlateImages";
    private const string kJsonlDir       = "Save/CamCaptureVLA";


    public Transform m_trMinRangePos;   // 랜덤 배치 시 카메라가 위치할 수 있는 영역의 최소/최대 좌표 (맵 사각박스)
    public Transform m_trMaxRangePos;   // 랜덤 배치 시 카메라가 위치할 수 있는 영역의 최소/최대 좌표 (맵 사각박스) 
    public Transform m_trTargetPos;     // 랜덤 배치 시 카메라가 바라보는 중심 위치 (맵 중앙 등)

    // P2: 분포 집계를 위해 현재 세션의 jsonl 경로를 보관한다
    private string m_CurrentJsonlPath = "";

    // P0-1: pan 원형 정규화 헬퍼 — C#의 음수 % 보정 포함, 결과 범위 [-180, 180]
    //   public static: EditMode 테스트가 동일 함수를 직접 호출해 공식을 보증하도록 노출.
    public static float NormalizePan(float x)
    {
        return ((x + 180f) % 360f + 360f) % 360f - 180f;
    }

    // ── 실행 파라미터 ──────────────────────────────────────────
    private float m_PanStep = 2f;       // PTZ 격자 순회 시 판/틸트/줌의 스텝 간격. 예: panStep=2 → pan = -180, -178, -176, ... 178, 180
    private float m_TiltStep = 1f;      // 하향 범위가 넓어도 번호판이 잘 보이는 이미지가 생성되도록 tiltStep을 5도로 늘림. (기존 2도)
    private float m_ZoomStep = 1f;      // 줌 범위가 넓어도 번호판이 잘 보이는 이미지가 생성되도록 zoomStep을 1로 늘림. (기존 0.5)
    private float m_MaxZoom = 5f;
    private float m_HeightMin = 2f;     // PTZ 격자 순회 시 카메라 높이 최솟값(m)
    private float m_HeightMax = 10f;    // PTZ 격자 순회 시 카메라 높이 최댓값(m)
    private float m_HeightStep = 0.5f;  // PTZ 격자 순회 시 카메라 높이 스텝(m)
    private int m_MinVehicle = 1;
    private int m_MaxVehicle = 7;
    private float m_CaptureDelay = 1.0f;
    //private bool m_IsTrain = true;
    private EMakeMode m_eMakeMode = EMakeMode.Train;

    // ── 내부 상태 ──────────────────────────────────────────────
    private int m_ImgCount = 0;
    private bool m_bRunning = false;        // 순회 실행 중 여부 플래그 (true: 실행 중, false: 정지). PTZ 스윕/랜덤배치 순회 모두 공통으로 사용. Start/Stop 메서드에서 설정, 코루틴에서 체크.
    private Coroutine m_SweepCoroutine;

    protected int m_CurEpisode = 0;
    protected int m_MaxEpisode = 0;

    private WaitForSeconds WAIT_Capture = null; // new WaitForSeconds(1);       // 캡처 간격 대기용 (번호판 포커스 후, 다음 PTZ 스텝 적용 전)
    private WaitForSeconds WAIT_Focus = null;  // new WaitForSeconds(1);          // 렌더링 안정화 대기용 (캡처 전, 번호판 포커스 후)

    public bool IsFocusImgFileSave { get; set; } = false;   // 번호판 포커스 이미지 파일 저장 여부 (true: 저장, false: 미저장)

    // ── 카메라 랜덤배치 순회 (StartRandomCamSweep) 인스펙터 설정 ──
    //   설계서: Docs/20260523_141843_카메라랜덤배치_VLA_설계서.md
    [Header("카메라 랜덤배치 영역(맵 사각박스)")]
    public Vector3 m_MapBoxMin = new Vector3(-25f, 0f, -25f);  // 좌측하단
    public Vector3 m_MapBoxMax = new Vector3( 25f, 0f,  25f);  // 우측상단
    public float   m_GroundY            = 0f;                  // 지면 높이(타겟/배치 기준)
    public float   m_NearCenterRadius   = 3f;                  // 중심 근접 판정 반경
    public float   m_NearCenterBoxScale = 2f;                  // 근접 시 ±z 확대박스 배율
    public float   m_MinDownTilt        = 22f;                 // 하늘 제거용 최소 하향 tilt(도). 0=미적용

    [Header("카메라 랜덤배치 순회 기본값")]
    public int   m_RC_MakeMode   = 2;                          // 0:train 1:eval 2:test
    public int   m_RC_CamCount   = 3;                          // 랜덤 카메라 대수
    public float m_RC_HeightMin  = 2f,  m_RC_HeightMax = 10f, m_RC_HeightStep = 0.5f;
    public float m_RC_PanMin     = -3f, m_RC_PanMax    = 3f,  m_RC_PanStep    = 1f;
    public float m_RC_TiltMin    = -1f, m_RC_TiltMax   = 1f,  m_RC_TiltStep   = 1f;
    public float m_RC_ZoomMin    = 1f,  m_RC_ZoomMax   = 3f,  m_RC_ZoomStep   = 1f;
    public int   m_RC_MinVehicle = 1,   m_RC_MaxVehicle = 7;
    public float m_RC_CaptureDelay = 0.3f;                     // 캡처 전 렌더 안정화 대기(초)
    public int   m_RC_MaxEpisodes  = 0;                        // 0=무제한(저장 레코드 상한)

    //public int m_ImgCount = 0;      // 생성된 이미지 카운트 (1부터 시작)
    //public float m_FramePerSec = 5;     // 초당 프레임 수 (이미지 저장 간격)
    //public float m_StepDelay = 2.0f;     // 차량 번호판 중심으로 이동 후 대기 시간 (초)
    //public bool m_IsTrain = true;        // 학습용 데이터 여부(true: train, false: eval)
    // Start is called once before the first execution of Update after the MonoBehaviour is created

    void Start()
    {
        m_MapBoxMin = m_trMinRangePos != null ? m_trMinRangePos.position : m_MapBoxMin;
        m_MapBoxMax = m_trMaxRangePos != null ? m_trMaxRangePos.position : m_MapBoxMax;
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

    public string GetMakeModePrefix()
    {
        switch (m_eMakeMode)
        {
            case EMakeMode.Train:
                return "train";
            case EMakeMode.Evaluation:
                return "eval";
            case EMakeMode.Test:
                return "test";
            default:
                return "train";
        }
    }


    // ──────────────────────────────────────────────────────────
    /// <summary>PTZ 격자 순회를 시작한다.</summary>
    public void StartPtzSpaceSweep(
        int makeMode,
        float panStep,
        float tiltStep,
        float zoomStep,
        float maxZoom,
        int minVehicle,
        int maxVehicle,
        float captureDelay,
        float hMin,
        float hMax,
        float hStep,
        int maxEpisodes = 0,
        Action<int, int> onProgress = null,
        Action onComplete = null)
    {
        if (m_bRunning)
        {
            Debug.LogWarning("[CVlaPSImgMakerGameUI] 이미 실행 중입니다. 먼저 StopPtzSpaceSweep()을 호출하세요.");
            return;
        }

        if( WAIT_Capture == null || m_CaptureDelay != captureDelay)
            WAIT_Capture = new WaitForSeconds(m_CaptureDelay);

        WAIT_Focus = new WaitForSeconds(0.05f);  // 번호판 포커스 후 렌더링 안정화 대기 (고정값)


        m_eMakeMode = (EMakeMode)makeMode;
        m_PanStep = Mathf.Max(panStep, 0.1f);
        m_TiltStep = Mathf.Max(tiltStep, 0.1f);
        m_ZoomStep = Mathf.Max(zoomStep, 0.1f);
        m_MaxZoom = Mathf.Max(maxZoom, 1f);
        m_HeightMin  = hMin;
        m_HeightMax  = hMax;
        m_HeightStep = Mathf.Max(hStep, 0.1f);   // 0/음수 스텝 → 무한루프 방지
        m_MinVehicle = Mathf.Max(minVehicle, 1);
        m_MaxVehicle = Mathf.Max(maxVehicle, m_MinVehicle);
        m_CaptureDelay = Mathf.Max(captureDelay, 0f);
        m_ImgCount = 0;
        m_CurEpisode = 0;
        m_MaxEpisode = maxEpisodes;   // 0 이면 무제한(전체 PTZ 조합)
        m_bRunning = true;

        // P2: 세션 시작 시 RNG 시드 1회 고정 (재현성 확보)
        int seed = (int)(System.DateTime.Now.Ticks & 0x7FFFFFFF);
        UnityEngine.Random.InitState(seed);
        Debug.Log($"[CVlaPSImgMakerGameUI] RNG seed={seed}");

        m_SweepCoroutine = StartCoroutine(Enum_PtzSpaceSweep(onProgress, onComplete));
    }

    /// <summary>실행 중인 순회를 즉시 정지한다.</summary>
    public void StopPtzSpaceSweep()
    {
        m_bRunning = false;
        if (m_SweepCoroutine != null)
        {
            StopCoroutine(m_SweepCoroutine);
            m_SweepCoroutine = null;
        }
    }

    // ── 코루틴 계층 ────────────────────────────────────────────

    /// <summary>
    /// 현재 파라미터(팅틸/판/줄옵 스텝) 기준으로 전체 PTZ 쪸합 수(실제 에피소드 수)를 계산한다.
    /// </summary>
    private int CalcTotalPtzEpisodes()
    {
        var camList    = m_CamObjListUI.m_CamObjList;
        var camPosData = CDataMgr.Inst.m_SaveCameraPosData;
        int total = 0;

        for (int ci = 0; ci < camList.Count; ci++)
        {
            if (ci >= camPosData.Count()) break;
            var presets = camPosData.GetCameraPos(ci + 1).datas;

            foreach (var preset in presets)
            {
                float tiltMin = preset.ptzmin.t;
                // P0-2: CalcTotalPtzEpisodes도 Enum_PtzSpaceSweep과 동일 범위 보장
                float tiltMax = Mathf.Max(preset.ptzmax.t, 8f);
                //float tiltMid = tiltMin + (tiltMax - tiltMin) * 0.5f;

                float panMin = preset.ptzmin.p;
                float panMax = preset.ptzmax.p;

                float zoomMin = 1f;
                float zoomMax = Mathf.Max(Mathf.Min(preset.ptzmax.z, m_MaxZoom), Mathf.Max(zoomMin, 1.5f));

                int tiltSteps   = CountFloatSteps(tiltMin,     tiltMax,     m_TiltStep);
                int panSteps    = CountFloatSteps(panMin,      panMax,      m_PanStep);
                int zoomSteps   = CountFloatSteps(zoomMin,     zoomMax,     m_ZoomStep);
                int heightSteps = CountFloatSteps(m_HeightMin, m_HeightMax, m_HeightStep);

                total += tiltSteps * panSteps * zoomSteps * heightSteps;
            }
        }
        return total;
    }

    /// <summary>from → to 를 step 간격으로 순회할 때의 스텝 횟수 (정수 근사)</summary>
    private static int CountFloatSteps(float from, float to, float step)
    {
        if (step <= 0f || from > to + 0.001f) return 0;
        return Mathf.FloorToInt((to - from + 0.001f) / step) + 1;
    }

    private IEnumerator Enum_PtzSpaceSweep(Action<int, int> onProgress, Action onComplete)
    {
        string prefix    = GetMakeModePrefix();
        string jsonlPath = Path.Combine(kJsonlDir, $"{prefix}_manifest.jsonl");

        if (!Directory.Exists(kJsonlDir)) Directory.CreateDirectory(kJsonlDir);
        if (File.Exists(jsonlPath)) File.Delete(jsonlPath);

        var camList    = m_CamObjListUI.m_CamObjList;
        var camPosData = CDataMgr.Inst.m_SaveCameraPosData;

        // 실제 전체 PTZ 조합 수 사전 계산 (tilt × pan × zoom 스텝 수의 합)
        int totalPtzEpisodes = CalcTotalPtzEpisodes();

        // UI에서 설정한 최대 에피소드 수 적용 (0이면 전체 PTZ 조합 기준)
        int effectiveMax = (m_MaxEpisode > 0) ? Mathf.Min(m_MaxEpisode, totalPtzEpisodes) : totalPtzEpisodes;
        onProgress?.Invoke(0, effectiveMax);

        // P2: jsonlPath를 멤버에 기록 (랜덤배치 순회와 공유하지 않으므로 여기서만 설정)
        m_CurrentJsonlPath = jsonlPath;

        // ── 카메라당 에피소드 상한 계산 (나머지 분배 → 8대 균등) ──
        int camCount = Mathf.Min(camList.Count, camPosData.Count());
        int baseEp   = (m_MaxEpisode > 0 && camCount > 0) ? m_MaxEpisode / camCount : 0;
        int remEp    = (m_MaxEpisode > 0 && camCount > 0) ? m_MaxEpisode % camCount : 0;

        // P2: saveData를 카메라 루프 바깥에 두어 분포 통계를 세션 전체로 누적한다
        //     FlushToJsonl은 m_Records만 초기화하고 통계 버퍼는 유지함
        var saveData = new CSavePtzSpaceData();

        // ── 카메라별 순차 순회 (cam0 전부 → cam1 전부 → …) ──
        for (int ci = 0; ci < camCount && m_bRunning; ci++)
        {
            CObjCamera cam     = camList[ci];
            var        presets = camPosData.GetCameraPos(ci + 1).datas;
            m_CamObjListUI.SetSelect(ci);

            // 이 카메라가 생성할 최대 에피소드 수 (0 = 무제한)
            int camMax = (m_MaxEpisode > 0) ? baseEp + (ci < remEp ? 1 : 0) : 0;
            int camEp  = 0;
            bool CamBudgetLeft() => camMax <= 0 || camEp < camMax;

            for (int pi = 0; pi < presets.Count && m_bRunning && CamBudgetLeft(); pi++)
            {
                SCamDir preset  = presets[pi];
                float tiltMin   = preset.ptzmin.t;
                // P0-2: tilt 상한을 최소 8f 로 보장 — 테스트 자세(tilt 7.4)가 순회 격자 안에 들어오도록
                float tiltMax   = Mathf.Max(preset.ptzmax.t, 8f);
                float panMin    = preset.ptzmin.p, panMax  = preset.ptzmax.p;
                float zoomMin   = 1f;
                // P0-2: zoom 상한을 최소 1.5f 로 보장 — 테스트 자세(cam zoom 1.5)가 순회 격자 안에 들어오도록
                float zoomMax   = Mathf.Max(Mathf.Min(preset.ptzmax.z, m_MaxZoom), Mathf.Max(zoomMin, 1.5f));

                for (float h = m_HeightMin; h <= m_HeightMax + 0.001f && m_bRunning && CamBudgetLeft(); h += m_HeightStep)
                for (float tilt = tiltMin;  tilt <= tiltMax + 0.001f  && m_bRunning && CamBudgetLeft(); tilt += m_TiltStep)
                for (float pan  = panMin;   pan  <= panMax  + 0.001f  && m_bRunning && CamBudgetLeft(); pan  += m_PanStep)
                for (float zoom = zoomMin;  zoom <= zoomMax + 0.001f  && m_bRunning && CamBudgetLeft(); zoom += m_ZoomStep)
                {
                    Vector3 p = preset.pos.ToVector3(); p.y = h;
                    cam.transform.position = p;
                    cam.UpdatePolePosition();
                    yield return Enum_CaptureAtPTZ(cam, pan, tilt, zoom, preset, saveData, prefix, onProgress, effectiveMax);
                    camEp++;
                }
            }

            saveData.FlushToJsonl(jsonlPath);
        }

        // P0-1: |dpan|>180 위반 건수 검증 로그 (정상이면 0)
        int dpanViolations = saveData.CountDpanViolations();
        Debug.Log($"[CVlaPSImgMakerGameUI] 생성 완료. |dpan|>180 위반 건수: {dpanViolations} (정상=0)");

        // P2: 분포 리포트 저장
        saveData.WriteReportTxt(m_CurrentJsonlPath);

        m_bRunning = false;
        onComplete?.Invoke();
    }



    private IEnumerator Enum_CaptureAtPTZ(
        CObjCamera cam,
        float pan,
        float tilt,
        float zoom,
        SCamDir preset,
        CSavePtzSpaceData saveData,
        string prefix,
        Action<int, int> onProgress,
        int totalEpisodes)
    {
        // 1. PTZ 적용
        cam.pan = pan;
        cam.tilt = tilt;
        cam.zoom = zoom;

        // 2. 기존 차량 제거(메모리 풀로 반환) 후 새로 랜덤 배치 (요구사항 2)
        //    매 PTZ 조합마다 기존 차량을 모두 풀로 반환하고 깨끗한 상태에서 새로 배치한다.
        if (m_CarObjListUI != null) m_CarObjListUI.RemoveAll();

        int count = CVehicleCountRandomizer.Pick();
        //   groundY/맵박스를 전달 → 지면 높이 일치 + (콜라이더 없어도) 박스 안 배치 허용
        var vehicles = CPtzSpaceVehiclePlacer.PlaceVehicles(
            cam, m_CarObjListUI, count,
            groundY: m_GroundY,
            boxMin: m_MapBoxMin, boxMax: m_MapBoxMax);

        // 3. 렌더링 안정화 대기
        yield return WAIT_Capture;

        // 4. 전경 스크린샷 저장
        m_CamObjListUI.m_SelectedCamObj = cam;
        string overviewImg = $"{prefix}_{m_ImgCount + 1:D4}.jpg";
        SaveFile_ScreenShot(overviewImg, kImgFolder);
        m_ImgCount++;

        // 5. PTZ 스텝 레코드 생성 (P0-1: cam.pan 저장 직전 원형 정규화)
        float normCamPan = NormalizePan(pan);
        var record = new SPtzSpaceRecord
        {
            img = overviewImg,
            cam_pos = new SVector3(cam.transform.position),
            pan = normCamPan,
            tilt = tilt,
            zoom = zoom,
        };

        // 6. 각 차량 번호판 PTZ 포커스 + 스크린샷 저장
        var pendingPlates = new System.Collections.Generic.List<SPtzPlateData>();

        for (int vi = 0; vi < vehicles.Count && m_bRunning; vi++)
        {
            CObjCar vehicle = vehicles[vi];
            if (vehicle == null || vehicle.m_FrontPlateNo == null) continue;

            float fPan, fTilt, fFov;
            if (!CLicensePlateFocusCapture.CalcTargetPTZ(
                    cam, vehicle, useFrontPlate: true,
                    out fPan, out fTilt, out fFov))
                continue;

            CLicensePlateFocusCapture.FocusOnLicensePlate(cam, vehicle, useFrontPlate: true);
            yield return WAIT_Focus; ;  // 번호판 포커스 렌더링 반영 대기

            if (IsFocusImgFileSave)
            {
                string plateImg = $"{prefix}_plate_{m_ImgCount:D4}_{vi + 1:D2}.jpg";
                CLicensePlateFocusCapture.CaptureAndSave(cam, kPlateImgFolder, plateImg, filePrefix: "");
            }

            // ① 배치 시 거리 상한으로 36 포화를 원천 차단하므로 다운샘플 불필요.
            //   Clamp 안전장치는 예외적 잔존 포화 대비로 유지한다.
            //   fFov 는 CalcTargetPTZ 반환 수직 FOV → ZoomFromFov(수직→수평→탄젠트역산).
            float plateZoom = Mathf.Clamp(fFov > 0f ? cam.ZoomFromFov(fFov) : 1f, 1f, 36f);

            // P0-1: plate.pan 원형 정규화 + dpan 직접 산출
            float nPlatePan = NormalizePan(fPan);
            float dpan      = NormalizePan(nPlatePan - normCamPan);

            Vector3 platePos = vehicle.m_FrontPlateNo.transform.position;
            // P1-1: dist = 번호판↔카메라 월드 거리
            float dist = Vector3.Distance(platePos, cam.transform.position);

            pendingPlates.Add(new SPtzPlateData
            {
                //img  = plateImg,
                pan  = nPlatePan,
                tilt = fTilt,
                zoom = plateZoom,
                dpan = dpan,
                dist = dist,
                x    = platePos.x,
                y    = platePos.y,
                z    = platePos.z
            });
        }

        record.plates.AddRange(pendingPlates);

        //// 원본 PTZ 복원
        //cam.pan  = pan;
        //cam.tilt = tilt;
        //cam.zoom = zoom;

        // 7. 레코드 등록
        saveData.AddRecord(record);

        // 8. 임시 차량 제거
        CPtzSpaceVehiclePlacer.RemoveVehicles(vehicles, m_CarObjListUI);
        m_CurEpisode++;
        onProgress?.Invoke(m_CurEpisode, totalEpisodes);

    }

    // public void FocusPlateByVehicle(List<CObjCar> vehicles, CObjCamera cam, SPtzSpaceRecord record, string prefix)
    // {
    //     // 6. 각 차량 번호판 PTZ 포커스 + 스크린샷 저장
    //     for (int vi = 0; vi < vehicles.Count && m_bRunning; vi++)
    //     {
    //         CObjCar vehicle = vehicles[vi];
    //         if (vehicle == null || vehicle.m_FrontPlateNo == null) continue;

    //         float fPan, fTilt, fFov;
    //         if (!CLicensePlateFocusCapture.CalcTargetPTZ(
    //                 cam, vehicle, useFrontPlate: true,
    //                 out fPan, out fTilt, out fFov))
    //             continue;

    //         CLicensePlateFocusCapture.FocusOnLicensePlate(cam, vehicle, useFrontPlate: true);
    //         yield return WAIT_Focus; ;  // 번호판 포커스 렌더링 반영 대기

    //         if (IsFocusImgFileSave)
    //         {
    //             string plateImg = $"{prefix}_plate_{m_ImgCount:D4}_{vi + 1:D2}.jpg";
    //             CLicensePlateFocusCapture.CaptureAndSave(cam, kPlateImgFolder, plateImg, filePrefix: "");
    //         }

    //         float plateZoom = fFov > 0f ? CObjCamera.DEFAULT_FOV / fFov : 1f;
    //         plateZoom = Mathf.Clamp(plateZoom, 1f, 36f); // 카메라 36배줌 한도 보장
    //         Vector3 platePos = vehicle.m_FrontPlateNo.transform.position;
    //         record.plates.Add(new SPtzPlateData
    //         {
    //             //img  = plateImg,
    //             pan  = fPan,
    //             tilt = fTilt,
    //             zoom = plateZoom,
    //             x    = platePos.x,
    //             y    = platePos.y,
    //             z    = platePos.z
    //         });


    //     }
    // }


    // ══════════════════════════════════════════════════════════
    //  카메라 랜덤배치 순회 (StartRandomCamSweep)
    //  설계서: Docs/20260523_141843_카메라랜덤배치_VLA_설계서.md §6
    //  - 카메라를 맵 박스 안 랜덤 XZ + 높이 단계로 배치, 항상 중심 응시
    //  - (대수 × 높이)마다 차량 1~N대 재배치, base 기준 PTZ 미세순회
    //  - "전 차량 가시"일 때만 스샷 저장 + 차량 번호판 PTZ 레코드 추가
    //  - 기존 StartPtzSpaceSweep 경로와 독립(추가). m_bRunning/m_SweepCoroutine 공유로 상호 배타.
    // ══════════════════════════════════════════════════════════

    /// <summary>인스펙터 기본값(m_RC_*, m_MapBox*)으로 랜덤배치 순회를 시작한다.
    /// 버튼 OnClick 또는 컨텍스트 메뉴에서 바로 호출 가능.</summary>
    [ContextMenu("Start Random Cam Sweep (Inspector Defaults)")]
    public void StartRandomCamSweep_Inspector()
    {
        StartRandomCamSweep(
            m_RC_MakeMode, m_RC_CamCount,
            m_RC_HeightMin, m_RC_HeightMax, m_RC_HeightStep,
            m_RC_PanMin, m_RC_PanMax, m_RC_PanStep,
            m_RC_TiltMin, m_RC_TiltMax, m_RC_TiltStep,
            m_RC_ZoomMin, m_RC_ZoomMax, m_RC_ZoomStep,
            m_RC_MinVehicle, m_RC_MaxVehicle,
            m_RC_CaptureDelay, m_RC_MaxEpisodes);
    }

    /// <summary>카메라 랜덤배치 순회를 시작한다.</summary>
    public void StartRandomCamSweep(
        int makeMode, int camCount,
        float hMin, float hMax, float hStep,
        float panMin, float panMax, float panStep,
        float tiltMin, float tiltMax, float tiltStep,
        float zoomMin, float zoomMax, float zoomStep,
        int minVehicle, int maxVehicle,
        float captureDelay, int maxEpisodes = 0,
        Action<int, int> onProgress = null, Action onComplete = null)
    {
        if (m_bRunning)
        {
            Debug.LogWarning("[CVlaPSImgMakerGameUI] 이미 실행 중입니다. 먼저 Stop 을 호출하세요.");
            return;
        }

        m_eMakeMode    = (EMakeMode)makeMode;
        m_CaptureDelay = Mathf.Max(captureDelay, 0f);
        WAIT_Capture   = new WaitForSeconds(m_CaptureDelay);
        WAIT_Focus     = new WaitForSeconds(0.3f);   // 번호판 포커스 후 렌더 안정화(Enum_CaptureAtPTZ 에서 사용)

        m_ImgCount   = 0;
        m_CurEpisode = 0;
        m_MaxEpisode = maxEpisodes;   // 0 = 무제한(저장 레코드 상한)
        m_bRunning   = true;

        // P2: 세션 시작 시 RNG 시드 1회 고정 (재현성 확보 — GetRandomXZInBox는 더 이상 호출별 재시드하지 않음)
        int seed = (int)(System.DateTime.Now.Ticks & 0x7FFFFFFF);
        UnityEngine.Random.InitState(seed);
        Debug.Log($"[CVlaPSImgMakerGameUI] RNG seed={seed}");

        m_SweepCoroutine = StartCoroutine(Enum_RandomCamSweep(
            camCount,
            hMin, hMax, hStep,
            panMin, panMax, panStep,
            tiltMin, tiltMax, tiltStep,
            zoomMin, zoomMax, zoomStep,
            minVehicle, maxVehicle,
            onProgress, onComplete));
    }

    /// <summary>실행 중인 랜덤배치 순회를 즉시 정지한다.</summary>
    public void StopRandomCamSweep()
    {
        m_bRunning = false;
        if (m_SweepCoroutine != null)
        {
            StopCoroutine(m_SweepCoroutine);
            m_SweepCoroutine = null;
        }
    }

    /// 랜덤 배치 순회 코루틴: camCount 대수만큼, 각 대수마다 높이 단계로 랜덤 XZ 배치 → 중심 응시 판/틸트 기준으로 미세 스텝 순회 → 줌 단계 순회
    private IEnumerator Enum_RandomCamSweep(
        int camCount,
        float hMin, float hMax, float hStep,
        float panMin, float panMax, float panStep,
        float tiltMin, float tiltMax, float tiltStep,
        float zoomMin, float zoomMax, float zoomStep,
        int minVehicle, int maxVehicle,
        Action<int, int> onProgress, Action onComplete)
    {
        // 입력 정규화 (0/음수 스텝 → 무한루프 방지)
        hStep    = Mathf.Max(hStep, 0.1f);
        panStep  = Mathf.Max(panStep, 0.1f);
        tiltStep = Mathf.Max(tiltStep, 0.1f);
        zoomStep = Mathf.Max(zoomStep, 0.1f);
        camCount = Mathf.Max(camCount, 1);

        // Enum_CaptureAtPTZ 가 멤버 필드(m_MinVehicle/m_MaxVehicle)로 차량 수를 정하므로 여기서 반영
        m_MinVehicle = Mathf.Max(minVehicle, 1);
        m_MaxVehicle = Mathf.Max(maxVehicle, m_MinVehicle);

        // 저장 경로는 기존 PTZ 스윕/ Enum_CaptureAtPTZ 와 동일하게 사용
        //   (이미지: kImgFolder, 매니페스트: kJsonlDir/{prefix}_manifest.jsonl)
        string prefix       = GetMakeModePrefix();
        string manifestPath = Path.Combine(kJsonlDir, $"{prefix}_manifest.jsonl");
        if (!Directory.Exists(kJsonlDir)) Directory.CreateDirectory(kJsonlDir);
        if (File.Exists(manifestPath)) File.Delete(manifestPath);

        CObjCamera cam = m_CamObjListUI != null ? m_CamObjListUI.m_SelectedCamObj : null;
        if (cam == null)
            cam = m_CamObjListUI.GetObjCamera(0);

        if (cam == null)
        {
            Debug.LogError("[CVlaPSImgMakerGameUI] 선택된 카메라가 없습니다. 순회를 중단합니다.");
            m_bRunning = false;
            onComplete?.Invoke();
            yield break;
        }

        var saveData = new CSavePtzSpaceData();

        int heightCount = CountFloatSteps(hMin, hMax, hStep);
        int panCount = CountFloatSteps(panMin, panMax, panStep);
        int tiltCount = CountFloatSteps(tiltMin, tiltMax, tiltStep);
        int zoomCount = CountFloatSteps(zoomMin, zoomMax, zoomStep);

        // 진행률 분모: 전체 PTZ 조합 수(상한). maxEpisode 가 있으면 더 작은 값.
        int totalCombos = camCount
            * CountFloatSteps(hMin, hMax, hStep)
            * CountFloatSteps(panMin, panMax, panStep)
            * CountFloatSteps(tiltMin, tiltMax, tiltStep)
            * CountFloatSteps(zoomMin, zoomMax, zoomStep);
        int effectiveTotal = (m_MaxEpisode > 0) ? Mathf.Min(m_MaxEpisode, totalCombos) : totalCombos;
        onProgress?.Invoke(0, effectiveTotal);

        Debug.Log($"[CVlaPSImgMakerGameUI] 총 PTZ 조합 수: {totalCombos}, 유효 조합 수: {effectiveTotal}");
        Debug.Log($"[CVlaPSImgMakerGameUI] 높이 수:{heightCount}, Pan수:{panCount}, tilt수:{tiltCount}, zoom수:{zoomCount}");

        Vector3 vTarget = m_trTargetPos.position;


        for (int c = 0; c < camCount && m_bRunning; c++)
        {
            if (m_MaxEpisode > 0 && m_CurEpisode >= m_MaxEpisode) break;

            // 카메라마다 맵 박스 안 랜덤 XZ
            Vector3 xz = CPtzRandomCameraPlacer.GetRandomXZInBox(m_MapBoxMin, m_MapBoxMax, m_GroundY);

            for (float h = hMin; h <= hMax + 1e-3f && m_bRunning; h += hStep)
            {
                if (m_MaxEpisode > 0 && m_CurEpisode >= m_MaxEpisode) break;

                Vector3 vCamPos = cam.transform.position;
                vCamPos.x = xz.x;
                vCamPos.z = xz.z;
                vCamPos.y = h;  // 높이는 단계별로 적용
                cam.transform.position = vCamPos;
                cam.UpdatePolePosition();

                cam.getCamera.transform.LookAt(vTarget);  // 랜덤 XZ 배치 후 중심 응시 (base pan/tilt 계산 기준)

                Vector3 dirToTarget = (vTarget - cam.transform.position).normalized;

                float basePan = cam.pan;
                float baseTilt = cam.tilt;

                // base 중심 응시 기준으로 pan/tilt 미세 오프셋 + zoom 순회.
                //   PTZ 한 조합마다 Enum_CaptureAtPTZ 가 차량을 새로 랜덤 배치(현재 뷰 안)하고
                //   스샷·번호판 PTZ 레코드를 저장한 뒤 차량을 제거한다(요구사항 1·3).
                for (float pan = panMin; pan <= panMax + 1e-3f && m_bRunning; pan += panStep)
                for (float tilt = tiltMin; tilt <= tiltMax + 1e-3f && m_bRunning; tilt += tiltStep)
                for (float zoom = zoomMin; zoom <= zoomMax + 1e-3f && m_bRunning; zoom += zoomStep)
                {
                    if (m_MaxEpisode > 0 && m_CurEpisode >= m_MaxEpisode) break;

                    float p = basePan + pan;     // 중심 응시 + 오프셋
                    float t = baseTilt + tilt;
                    yield return Enum_CaptureAtPTZ(
                        cam, p, t, zoom,
                        preset: default,
                        saveData, prefix, onProgress, effectiveTotal);
                }

                // (대수 × 높이)마다 JSONL flush(메모리 절약)
                saveData.FlushToJsonl(manifestPath);
            }
        }

        // P0-1: |dpan|>180 위반 건수 검증 로그 (정상이면 0)
        int dpanViolations = saveData.CountDpanViolations();
        Debug.Log($"[CVlaPSImgMakerGameUI] 생성 완료. |dpan|>180 위반 건수: {dpanViolations} (정상=0)");

        // P2: 분포 리포트 저장
        saveData.WriteReportTxt(manifestPath);

        m_bRunning = false;
        onComplete?.Invoke();
    }

    // ── 데이터 생성 다이얼로그 ─────────────────────────────────

    /// <summary>CDRSim 데이터 생성 다이얼로그를 연다.</summary>
    public void OpenDataGenDlg()
    {
        //if (m_DataGenDlg == null)
        //{
        //    Debug.LogWarning("[CVlaPSImgMakerGameUI] m_DataGenDlg가 연결되지 않았습니다.");
        //    return;
        //}
        //m_DataGenDlg.OpenUI();
    }

    /// <summary>CDRSim 데이터 생성 다이얼로그를 닫는다.</summary>
    public void CloseDataGenDlg()
    {
        //m_DataGenDlg?.CloseUI();
    }

}
