using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using UnityEngine;
using UnityEngine.UI;
using static CSaveInitCampPos;
using static UnityEngine.Rendering.DebugUI.Table;

public class CPCamObjListUI : MonoBehaviour
{
    // RenderTexture는 생성해서만 사용한다.
    public List<RenderTexture> m_RTList = new List<RenderTexture>(); // 생성된 RenderTexture 리스트

    public GameObject m_PrefabCamPTZ = null;                // PTZ 카메라 프리팹
    public GameObject m_PrefabCamStatic = null;             // Static 카메라 프리팹
    public Transform m_trCamParent = null;                  // 카메라 오브젝트들이 생성될 부모 오브젝트
    [Space]
    public List<CObjCamera> m_CamObjList = new List<CObjCamera>(); // 생성된 카메라 오브젝트 리스트
    public CPCamViewerUI m_CurCamViewerUI = null;                  // 단일 카메라 뷰어 UI (선택된 카메라의 RT를 표시)
    public CPoleObjListUI m_PoleObjListUI = null;                  // 폴대 관리 UI
    

    [HideInInspector] public CObjCamera m_SelectedCamObj = null;         // 현재 선택된 카메라 오브젝트
    //[HideInInspector] public CObjPole m_CurPole = null;                  // 현재 선택된 폴대

    [Header("카메라 타겟 위치 오브젝트")]
    public GameObject m_TargetPosObj = null;

    [Header("폴대 프리팹")]
    public GameObject m_PolePrefab = null;  // 카메라 폴대 프리팹 (CObjCamera.CreatePole() 에 전달)

    // ✅ RenderTexture 사용 중 플래그
    private bool m_IsCapturing = false;

    private void Awake()
    {
        EnsureCurCamViewer();
    }

    private bool EnsureCurCamViewer()
    {
        // Inspector 미설정 시 자동 탐색 (자식 우선 → 씬 전체)
        if (m_CurCamViewerUI == null)
            m_CurCamViewerUI = GetComponentInChildren<CPCamViewerUI>(true);

        //if (m_CurCamViewerUI == null)
        //{
        //    var viewers = FindObjectsOfType<CPCamViewerUI>(true);
        //    if (viewers != null && viewers.Length > 0)
        //        m_CurCamViewerUI = viewers[0];
        //}

        // RawImage 연결 보정
        if (m_CurCamViewerUI != null && m_CurCamViewerUI.m_RawImage == null)
            m_CurCamViewerUI.m_RawImage = m_CurCamViewerUI.GetComponentInChildren<RawImage>(true);

        return m_CurCamViewerUI != null && m_CurCamViewerUI.m_RawImage != null;
    }

    // Start is called once before the first execution of Update after the MonoBehaviour is created
    void Start()
    {
        //Initialize();
    }

    public float GetMaxZoom()
    {
        return CDataMgr.Inst.simConfig.GetMaxZoom();
    }

    public CObjCamera GetCurObjCamera()
    {
        return m_SelectedCamObj;
    }

    public CObjCamera GetObjCamera(int idx)
    {
        if (idx >= 0 && idx < m_CamObjList.Count)
            return m_CamObjList[idx];
        
        return null;
    }

    public CObjCamera GetObjCameraByID(int id)
    {
       var camObj = m_CamObjList.Find(x => x.Idx == id);
       if (camObj != null)
           return camObj;

        return null;
    }


    public void Initialize(Action<bool, CObjCamera> camLoadComplete=null)
    {
        CSaveInitCampPos kSaveData = CDataMgr.Inst.m_SaveCameraPosData;
        if (kSaveData.Count() > 0)
        {
            ResetCameraList();
        }
        else
        {
            m_CamObjList.Clear();
            ReleaseRenderTextures(); // 주의: 파일에 있는것도 지운다.

            SetCameraObjectAndRenderTexture(CBaseCamera.ECameraType.PTZ, "PTZCamera-0");
        }

        //Init_PoleInstall();

        ResetOrderCamerID();

        SetSelect(0);

        m_SelectedCamObj = this.GetObjCamera(0);
        // SetSelect 시점에 m_TargetRenderTexture가 아직 미설정인 경우를 대비해 명시적으로 갱신
        RefreshViewerTexture();

        StartCoroutine(Enum_InitFromSaveData(camLoadComplete));

        //Invoke("Init_FromSaveData", 1.0f);
    }

    public void ResetOrderCamerID()
    {
        for (int i = 0; i < m_CamObjList.Count; i++)
        {
            CObjCamera kCamObj = m_CamObjList[i];
            if (kCamObj != null)
            {
                kCamObj.Idx = i+1;
                //kCamObj.m_NameId = 
            }
        }
    }

    /// <summary>
    /// 선택된 카메라에 저장된 정보를 셋팅한다.
    /// </summary>
    IEnumerator Enum_InitFromSaveData(Action<bool, CObjCamera> camLoadComplete)
    {
        if (camLoadComplete != null)
        {
            camLoadComplete.Invoke(true, m_SelectedCamObj);
        }

        CSaveInitCampPos kSaveData = CDataMgr.Inst.m_SaveCameraPosData;
        if (kSaveData.GetCameraPos().datas.Count == 0)
        {
            yield break;
        }

        SCamDir kCamDir = kSaveData.GetCameraPos().datas[0];

        m_SelectedCamObj.SetZoomByFOV(kCamDir.zoom, GetMaxZoom());

        yield return null; // 한 프레임 대기


        //m_SelectedCamObj.transform.localPosition = kCamPos.Pos();
        //m_SelectedCamObj.transform.localEulerAngles = kCamPos.Rot();
        //float fVal = kCamPos.zoom;

        //// 자식 카메라 트랜스폼 (실제 카메라 컴포넌트가 붙어있는 오브젝트)
        //Transform camTr = m_SelectedCamObj.m_PCamera.transform;

        //// 카메라가 '현재 바라보고 있는 방향'을 월드 기준 forward로 가져옴
        //Vector3 lookDir = camTr.forward;
        //if (lookDir.sqrMagnitude <= 0.0001f)
        //{
        //    // 예외적으로 forward가 유효하지 않다면 부모의 forward를 사용
        //    lookDir = m_SelectedCamObj.transform.forward;
        //}
        //lookDir.Normalize();

        //// 부모(폴대) 위치에서 카메라가 바라보는 방향으로 fVal 만큼 이동
        //Vector3 targetWorldPos = m_SelectedCamObj.transform.position + lookDir * fVal;

        //// 카메라가 부모의 자식이라면 월드 위치를 설정해도 localPosition이 자동으로 계산됨.
        //camTr.position = targetWorldPos;
    }

    public bool IsCreateTargetPoint()
    {
        if (m_TargetPosObj != null && m_TargetPosObj.activeSelf)
            return true;

        if( m_TargetPosObj == null)
            Debug.LogWarning("Start Point 오브젝트가 없습니다. (m_TargetPosObj == null,)");

        return false;
    }

    // 카메라와 타겟 위치 사이의 거리를 계산한다.
    public float CalcualteDistanceToCameraBottom()
    {
        if (m_TargetPosObj == null || m_SelectedCamObj == null) return 0;

       Vector3 vStartPos = m_TargetPosObj.transform.position;
        Vector3 vCamPos = m_SelectedCamObj.transform.position;
        
        float fDist = Vector3.Distance(vStartPos, vCamPos);
        return fDist;
    }

    // 카메라와 타겟 위치 사이의 거리를 계산한다. (XZ 평면에서)
    public float CalcualteDistanceToCamera()
    {
        if (m_TargetPosObj == null || m_SelectedCamObj == null) return 0;

        Vector3 vStartPos = m_TargetPosObj.transform.position;
        Vector3 vCamPos = m_SelectedCamObj.transform.position;
        vCamPos.y = 0; // 카메라의 높이는 무시하고 XZ 평면에서의 거리 계산
        float fDist = Vector3.Distance(vStartPos, vCamPos);
        return fDist;
    }


    public void SetCameraPos(Vector3 vPos)
    {
        m_SelectedCamObj.transform.position = vPos;
    }

    public void SetCameraRotate(float pen, float tilt)
    {
        m_SelectedCamObj.SetPenTilt(pen, tilt);
    }

    public void SetCameraZoom(float fZoom, float fMaxZoom = 36.0f)
    {
        m_SelectedCamObj.SetZoomByFOV(fZoom, fMaxZoom);
    }

    public void SetSelect(int idx)
    {
        CObjCamera selectedCam = GetObjCamera(idx);
        if (selectedCam == null) return;

        m_SelectedCamObj = selectedCam;

        // CObjCamera 는 항상 자신의 m_TargetRenderTexture 로만 렌더링한다.
        // targetTexture = null 로 돌리면 화면에 직접 출력되어 메인카메라를 덮게 되므로 절대 null 설정하지 않는다.
        // 선택 여부는 enabled 로만 제어한다.
        foreach (var cam in m_CamObjList)
        {
            if (cam == null) continue;
            Camera kCam = cam.m_PCamera != null ? cam.getCamera : null;
            if (kCam == null) continue;
            if (kCam == Camera.main) continue;  // 메인카메라는 절대 건드리지 않는다

            // RT가 아직 연결되지 않은 경우 보정
            if (kCam.targetTexture != cam.m_TargetRenderTexture)
                kCam.targetTexture = cam.m_TargetRenderTexture;

            kCam.enabled = (cam == selectedCam);
        }

        // m_CurCamViewerUI: 뷰어가 비활성화 상태라면 활성화 후 RT 교체
        if (EnsureCurCamViewer())
        {
            if (!m_CurCamViewerUI.gameObject.activeInHierarchy)
                m_CurCamViewerUI.Show(true);
            m_CurCamViewerUI.m_RawImage.texture = selectedCam.m_TargetRenderTexture;
        }
    }

    /// <summary>
    /// 선택된 카메라의 RT를 m_CurCamViewerUI에 강제로 다시 연결한다.
    /// Initialize 또는 카메라 전환 후 뷰어가 공백으로 보일 때 호출한다.
    /// </summary>
    public void RefreshViewerTexture()
    {
        if (!EnsureCurCamViewer() || m_SelectedCamObj == null) return;
        m_CurCamViewerUI.m_RawImage.texture = m_SelectedCamObj.m_TargetRenderTexture;

        // 카메라도 활성 상태 보정
        Camera kCam = m_SelectedCamObj.m_PCamera != null ? m_SelectedCamObj.getCamera : null;
        if (kCam != null && kCam != Camera.main)
        {
            if (kCam.targetTexture != m_SelectedCamObj.m_TargetRenderTexture)
                kCam.targetTexture = m_SelectedCamObj.m_TargetRenderTexture;
            kCam.enabled = true;
        }
    }

    //카메라 오브젝트 생성 + 랜더 텍스쳐 생성
    public CObjCamera SetCameraObjectAndRenderTexture(CBaseCamera.ECameraType eType, string sName, bool bUseRenderTexure = true)
    {
        //1. 카메라 오브젝트 생성
        CObjCamera kCamObj = CreateCameraObject(eType, sName);

        if (bUseRenderTexure)
        {
            //2. 랜더 텍스쳐 생성 후 CObjCamera에 연결
            RenderTexture rt = CreateRenderTexture(1920, 1080, m_RTList.Count);
            kCamObj.SetTargetRenderTexture(rt);

            // 첫 번째 카메라이거나 현재 선택된 카메라가 없는 경우 뷰어에도 즉시 연결
            if (EnsureCurCamViewer() && (m_SelectedCamObj == null || m_SelectedCamObj == kCamObj))
                m_CurCamViewerUI.m_RawImage.texture = rt;
        }
        return kCamObj;
    }

    // 카메라 오브젝트 생성
    public CObjCamera CreateCameraObject(CBaseCamera.ECameraType eCamType, string sName)
    {
        GameObject goPrefab = (eCamType == CBaseCamera.ECameraType.PTZ) ? m_PrefabCamPTZ : m_PrefabCamStatic;

        if (goPrefab != null)
        {
            GameObject go = Instantiate(goPrefab, m_trCamParent);
            go.transform.localScale = goPrefab.transform.localScale;
            go.name = sName;
            CObjCamera kCamObj = go.GetComponent<CObjCamera>();
            if (kCamObj != null)
            {
                //kCamObj.m_NameId = sName;
                kCamObj.Initilaize( GetMaxZoom() );
                kCamObj.Idx = m_CamObjList.Count + 1; // 1부터 시작하는 ID
                m_CamObjList.Add(kCamObj);
               // CDataMgr.Inst.m_PtzCameraList.Add(kCamObj.m_PCamera);
                return kCamObj;
            }
        }
        return null;
    }

    // RenderTexture 생성
    public RenderTexture CreateRenderTexture(int width = 1920, int height = 1080, int idx=0)
    {
        RenderTexture rt = new RenderTexture(width, height, 24);
        rt.name = $"RenderTextrue-{idx}";
        rt.antiAliasing = 1;  // MSAA 비활성화 (성능 향상)
        rt.useMipMap = false;  // Mipmap 비활성화
        rt.autoGenerateMips = false;
        rt.Create();
        m_RTList.Add(rt);
        return rt;
    }

    // RenderTexture 해제
    public void ReleaseRenderTextures()
    {
        for (int i = 0; i < m_RTList.Count; i++)
        {
            RenderTexture rt = m_RTList[i] as RenderTexture;
            if (rt != null)
            {
                rt.Release();
                Destroy(rt, 0.5f);
            }
        }
        m_RTList.Clear();
    }


    public void SetSelectCamera(CObjCamera kCamObj)
    {
        // CObjCamera 는 항상 자신의 RT로만 렌더링한다.
        // targetTexture 를 null 로 돌리면 화면에 직접 출력되어 메인카메라를 덮으므로 절대 null 설정하지 않는다.
        // 이전 선택 카메라는 enabled = false 로만 비활성화
        if (m_SelectedCamObj != null && m_SelectedCamObj != kCamObj)
        {
            Camera prevCam = m_SelectedCamObj.m_PCamera != null ? m_SelectedCamObj.getCamera : null;
            if (prevCam != null && prevCam != Camera.main)
                prevCam.enabled = false;
        }

        m_SelectedCamObj = kCamObj;

        if (m_SelectedCamObj != null)
        {
            Camera curCam = m_SelectedCamObj.m_PCamera != null ? m_SelectedCamObj.getCamera : null;
            if (curCam != null && curCam != Camera.main)
            {
                // RT 연결이 빠져 있으면 보정
                if (curCam.targetTexture != m_SelectedCamObj.m_TargetRenderTexture)
                    curCam.targetTexture = m_SelectedCamObj.m_TargetRenderTexture;
                curCam.enabled = true;
            }

            if (EnsureCurCamViewer())
                m_CurCamViewerUI.m_RawImage.texture = m_SelectedCamObj.m_TargetRenderTexture;
        }
        else
        {
            if (EnsureCurCamViewer())
                m_CurCamViewerUI.m_RawImage.texture = null;
        }
    }

    public void RemoveAll_CameraObj()
    {
        for( int i = 0; i < m_CamObjList.Count; i++)
        {
            CObjCamera kCamObj = m_CamObjList[i];
            if (kCamObj != null)
            {
                Destroy(kCamObj.gameObject);
            }
        }
        m_CamObjList.Clear();
    }

    /// <summary>
    /// 지정된 리스트 인덱스의 카메라를 제거한다.
    /// RenderTexture 해제, 뷰어 초기화, GameObject 파괴, ID 재정렬을 수행한다.
    /// 카메라가 1개 이하인 경우 제거하지 않는다.
    /// </summary>
    public bool RemoveCameraAt(int listIdx)
    {
        if (listIdx < 0 || listIdx >= m_CamObjList.Count) return false;
        if (m_CamObjList.Count <= 1)
        {
            Debug.LogWarning("[CPCamObjListUI] 카메라가 1개 이하이므로 삭제할 수 없습니다.");
            return false;
        }

        CObjCamera kCamObj = m_CamObjList[listIdx];

        // 제거 대상이 현재 선택된 카메라라면 뷰어 텍스처 초기화
        if (kCamObj == m_SelectedCamObj && m_CurCamViewerUI != null)
            m_CurCamViewerUI.m_RawImage.texture = null;

        // RenderTexture 해제
        if (kCamObj.m_TargetRenderTexture != null)
        {
            int rtIdx = m_RTList.IndexOf(kCamObj.m_TargetRenderTexture);
            if (rtIdx >= 0)
                m_RTList.RemoveAt(rtIdx);
            kCamObj.m_TargetRenderTexture.Release();
            Destroy(kCamObj.m_TargetRenderTexture, 0.5f);
        }

        // 리스트에서 제거 후 GameObject 파괴
        m_CamObjList.RemoveAt(listIdx);
        Destroy(kCamObj.gameObject);

        ResetOrderCamerID();

        // 현재 선택된 카메라가 제거된 경우 첫 번째 카메라로 갱신
        if (m_SelectedCamObj == kCamObj || m_SelectedCamObj == null)
        {
            m_SelectedCamObj = m_CamObjList.Count > 0 ? m_CamObjList[0] : null;
            if (m_CurCamViewerUI != null)
                m_CurCamViewerUI.m_RawImage.texture = m_SelectedCamObj?.m_TargetRenderTexture;
        }

        return true;
    }

    public void ResetCameraList()
    {
        RemoveAll_CameraObj();
        ReleaseRenderTextures();

        var kCamPosDatas = CDataMgr.Inst.m_SaveCameraPosData;
        //List<CSaveInitCampPos> kCamPosList = CDataMgr.Inst.m_SaveCamPosList;
        //var kDataList = kSaveData.m_CamDirList.datas;
        for (int i = 0; i < kCamPosDatas.Count(); i++)
        {
            SCameraPos kCamPos = kCamPosDatas.GetCameraPos(i + 1);

            CObjCamera kCamObj = SetCameraObjectAndRenderTexture(CBaseCamera.ECameraType.PTZ, $"Camera-{i}");
            if (kCamObj != null)
            {
                //if( kCamPos.GetCameraPos(0).datas.Count > 0)
                for( int j = 0; j < kCamPos.GetCount(); j++)
                {
                    SCamDir kCamDir = kCamPos.Get(j);

                    kCamObj.transform.localPosition = kCamDir.Pos();
                    kCamObj.m_PCamera.transform.localRotation = Quaternion.Euler(kCamDir.Rot());
                    kCamObj.SetZoomByFOV(kCamDir.zoom, GetMaxZoom());
                    kCamObj.Idx = kCamDir.cam_id;
                    kCamObj.UpdatePolePosition();
                } 
            }
        }


        //CSaveInitCampPos kSaveData = CDataMgr.Inst.m_SaveCameraPosData;
        //var kDataList = kSaveData.m_CamDirList.datas;
        //for (int i = 0; i < kDataList.Count; i++)
        //{
        //    CSaveInitCampPos.SCamDir kCamPos = kDataList[i];

        //    CObjCamera kCamObj = SetCameraObjectAndRenderTexture(CBaseCamera.ECameraType.PTZ, kCamPos.sname);
        //    if (kCamObj != null)
        //    {
        //        kCamObj.transform.localPosition = kCamPos.Pos();
        //        kCamObj.m_PCamera.transform.localRotation = Quaternion.Euler(kCamPos.Rot());
        //        kCamObj.SetZoomByFOV(kCamPos.zoom, GetMaxZoom());
        //        kCamObj.m_Idx = kCamPos.cam_id;
        //    }
        //}
    }

    public void Init_PoleInstall()
    {
        var kListUI = m_PoleObjListUI;
        for (int i = 0; i < kListUI.m_ObjPoleList.Count; i++)
        {
            CObjPole kPole = kListUI.m_ObjPoleList[i];

            if (i >= m_CamObjList.Count)
            {
                SetCameraObjectAndRenderTexture(CBaseCamera.ECameraType.PTZ, $"PTZCamera-{i}");
            }
            CObjCamera kObjCamera = GetObjCamera(i);
            AttachCameraToPole(kObjCamera, kPole);
        }
    }

    // 카메라를 폴대에 부착
    public void AttachCameraToPole(CObjCamera kObjCamera, CObjPole kPole)
    {
        if (kObjCamera != null && kPole != null)
        {
            // 카메라 폴대의 높이를 카메라 높이에 맞춘다.
            float fCamHeight = kObjCamera.transform.localPosition.y;
            kPole.SetPoleHeight(fCamHeight);         // 폴대가 카메라보다 약간 더 높게

            kObjCamera.transform.SetParent(kPole.m_trCamPos);
            kObjCamera.transform.localPosition = Vector3.zero;
            kObjCamera.transform.localEulerAngles = Vector3.zero;
        }
    }

    

    /// <summary>
    /// 현재 카메라 화면의 텍스쳐를 파일에 저장    
    /// </summary>
    public Texture2D SaveCurViewerTexture(ref string sOutFileName, string sPrefix = "vpd", string sPath="Save/Images")
    {
        if (m_CurCamViewerUI == null) return null;

        DateTime now = System.DateTime.Now;
        string sFileName = $"{sPath}/{sPrefix}_img_{now.ToString("yyyyMMdd_HHmmss")}.jpg";
        
        Texture2D texOri = CaptureCurrentViewerTexture(m_CurCamViewerUI.m_RawImage);

        if (texOri == null)
        {
            CLogger.Inst.Error("텍스처 캡처 실패!", "Capture");
            return null;
        }

        //이미지 크기 변경 = 640 * 360
        Texture2D tex = CImageHelper.ResizeTexture(texOri, 640, 360, false, 64);

        if (tex != null)
        {
            CImageHelper.SaveTextureToJPG(tex, sFileName);
            sOutFileName = sFileName;
            
            // ✅ 수정: tex는 즉시 파괴 (저장 완료)
            Destroy(tex);
        }
        else
        {
            CLogger.Inst.Error("텍스처 리사이즈 실패!", "Capture");
            // ✅ texOri도 해제
            Destroy(texOri);
            return null;
        }

        // ✅ 중요: texOri 반환 - 호출자가 사용 후 반드시 Destroy 해야 함
        return texOri;
    }

    /// <summary>
    /// RenderTexture를 안전하게 캡처 (GPU 완전 동기화 + 중복 방지)
    /// </summary>
    public Texture2D CaptureCurrentViewerTexture(RawImage rowImage)
    {
        var captureStopwatch = System.Diagnostics.Stopwatch.StartNew();
        
        // ✅ 이미 캡처 중이면 경고
        if (m_IsCapturing)
        {
            CLogger.Inst.Warning("이미 캡처 진행 중입니다! 중복 호출 무시", "Capture");
            return null;
        }

        m_IsCapturing = true;  // ✅ 캡처 시작
        CLogger.Inst.Debug($"🎥 [CaptureTexture] 시작 (Frame: {Time.frameCount})", "Capture");

        try
        {
            var raw = rowImage;
            if (raw == null || raw.texture == null)
            {
                CLogger.Inst.Warning("뷰어의 텍스처가 없습니다.", "Capture");
                return null;
            }

            // RenderTexture에서 읽기
            if (raw.texture is RenderTexture rt)
            {
                // ✅ RenderTexture가 유효한지 확인
                if (!rt.IsCreated())
                {
                    CLogger.Inst.Error("RenderTexture가 생성되지 않았습니다!", "Capture");
                    return null;
                }

                CLogger.Inst.Debug($"   RenderTexture: {rt.name}, Size={rt.width}x{rt.height}, Created={rt.IsCreated()}", "Capture");

                RenderTexture prev = RenderTexture.active;
                
                try
                {
                    // ✅ 핵심 1: 해당 카메라를 명시적으로 렌더링 (RenderTexture 강제 업데이트)
                    if (m_SelectedCamObj != null && m_SelectedCamObj.getCamera != null)
                    {
                        var camera = m_SelectedCamObj.getCamera;
                        CLogger.Inst.Debug($"   카메라 명시적 렌더링: {camera.name}", "Capture");
                        camera.Render();
                    }
                    
                    // ✅ 핵심 2: GPU 명령 완전 플러시 + 동기화
                    GL.Flush();
                    GL.InvalidateState();
                    
                    // ✅ 핵심 3: AsyncGPUReadback 대기 (GPU 파이프라인 완료)
                    UnityEngine.Rendering.AsyncGPUReadback.WaitAllRequests();
                    
                    //CLogger.Inst.Debug($"   GPU 동기화 완료 ({captureStopwatch.ElapsedMilliseconds}ms)", "Capture");
                    
                    // ✅ RenderTexture 활성화
                    RenderTexture.active = rt;
                    
                    var tex = new Texture2D(rt.width, rt.height, TextureFormat.RGB24, false);
                    tex.ReadPixels(new Rect(0, 0, rt.width, rt.height), 0, 0);
                    tex.Apply(false);
                    
                    //CLogger.Inst.Debug($"   ReadPixels 완료 ({captureStopwatch.ElapsedMilliseconds}ms)", "Capture");
                    
                    // ✅ 픽셀 데이터 검증
                    Color32[] pixels = tex.GetPixels32();
                    if (pixels == null || pixels.Length == 0)
                    {
                        CLogger.Inst.Error("픽셀 데이터가 비어있습니다!", "Capture");
                        Destroy(tex);
                        return null;
                    }
                    
                    // ✅ 간단한 검증: 모든 픽셀이 검黑색이 아닌지 확인
                    int nonBlackPixels = 0;
                    for (int i = 0; i < Mathf.Min(100, pixels.Length); i++)
                    {
                        if (pixels[i].r > 0 || pixels[i].g > 0 || pixels[i].b > 0)
                            nonBlackPixels++;
                    }
                    
                    if (nonBlackPixels == 0)
                    {
                        CLogger.Inst.Warning("⚠️ 텍스처가 완전히 검정색입니다! (렌더링 미완료 가능성)", "Capture");
                    }
                    
                    captureStopwatch.Stop();
                    CLogger.Inst.Info($"✅ 텍스처 캡처 성공: {tex.width}x{tex.height}, {pixels.Length}px ({captureStopwatch.ElapsedMilliseconds}ms)", "Capture");
                    
                    return tex;
                }
                catch (Exception ex)
                {
                    CLogger.Inst.Error($"RenderTexture 캡처 실패: {ex.Message}", "Capture");
                    return null;
                }
                finally
                {
                    RenderTexture.active = prev;
                }
            }

            // 이미 Texture2D인 경우 복사본 반환
            if (raw.texture is Texture2D t2)
            {
                var copy = new Texture2D(t2.width, t2.height, t2.format, false);
                try
                {
                    copy.SetPixels(t2.GetPixels());
                    copy.Apply();
                }
                catch
                {
                    Graphics.CopyTexture(t2, copy);
                }
                return copy;
            }

            CLogger.Inst.Warning("지원되지 않는 텍스처 타입입니다.", "Capture");
            return null;
        }
        finally
        {
            m_IsCapturing = false;  // ✅ 캡처 완료
            CLogger.Inst.Debug($"🎥 [CaptureTexture] 종료", "Capture");
        }
    }

    /// <summary>
    /// 현재 선택된 뷰어를 캡처하여 PNG로 저장합니다.
    /// filePath가 비어있으면 Assets 폴더에 타임스탬프 파일로 저장됩니다.
    /// 성공 시 저장된 전체 경로 반환, 실패 시 null 반환.
    /// </summary>
    public Texture2D SaveFile_ToPNG(Texture2D tex, string filePath = null)
    {
        if (tex == null)
            return null;

        if (string.IsNullOrEmpty(filePath))
        {
            var folder = Application.dataPath;
            filePath = Path.Combine(folder, $"ViewerScreenshot_{System.DateTime.Now:yyyyMMdd_HHmmss}.png");
        }

        try
        {
            // 저장 폴더가 없으면 생성
            string directory = Path.GetDirectoryName(filePath);
            if (!string.IsNullOrEmpty(directory) && !Directory.Exists(directory))
            {
                Directory.CreateDirectory(directory);
                Debug.Log($"CPCamObjListUI: Created directory: {directory}");
            }

            File.WriteAllBytes(filePath, tex.EncodeToPNG());
            Debug.Log($"CPCamObjListUI: Saved viewer screenshot to {filePath}");
            
            // 임시로 생성한 Texture2D 해제
            // 외부에서 반드시 삭제해야 한다.
            // Destroy(tex);
            return tex;
        }
        catch (System.Exception ex)
        {
            Debug.LogError($"CPCamObjListUI: Failed to save PNG screenshot: {ex.Message}");
            Destroy(tex);
            return null;
        }
    }

    /// <summary>
    /// 현재 선택된 뷰어를 캡처하여 JPG로 저장합니다.
    /// filePath가 비어있으면 Assets 폴더에 타임스탬프 파일로 저장됩니다.
    /// quality: JPG 품질 (1~100, 기본값 85)
    /// 성공 시 저장된 전체 경로 반환, 실패 시 null 반환.
    /// </summary>
    public Texture2D SaveFile_ToJPG(Texture2D tex, string filePathName = null, int quality = 85)
    {
        if (tex == null)
            return null;

        // 품질 범위 제한
        quality = Mathf.Clamp(quality, 1, 100);

        if (string.IsNullOrEmpty(filePathName))
        {
            var folder = Application.dataPath;
            filePathName = Path.Combine(folder, $"Screen_{System.DateTime.Now:yyyyMMdd_HHmmss}.jpg");
        }

        try
        {
            // 저장 폴더가 없으면 생성
            string directory = Path.GetDirectoryName(filePathName);
            if (!string.IsNullOrEmpty(directory) && !Directory.Exists(directory))
            {
                Directory.CreateDirectory(directory);
                Debug.Log($"CPCamObjListUI: Created directory: {directory}");
            }

            File.WriteAllBytes(filePathName, tex.EncodeToJPG(quality));
            Debug.Log($"CPCamObjListUI: Saved viewer screenshot to {filePathName} (quality: {quality})");
            
            // 임시로 생성한 Texture2D 해제
            // 외부에서 반드시 삭제해야 한다.
            // Destroy(tex);
            return tex;
        }
        catch (System.Exception ex)
        {
            Debug.LogError($"CPCamObjListUI: Failed to save JPG screenshot: {ex.Message}");
            Destroy(tex);
            return null;
        }
    }

    // 카메라 위치/회전/줌 정보로 카메라 설정
    public void SetCurCamera(CObjCamera kCurCamera, SCamDir kCamPos)
    {
        // 카메라 위치 정보로 카메라 설정
        Vector3 vPos = kCurCamera.transform.position;  // 카메라 높이가 월드 높이 이어야 한다. 폴대의 자식오브젝트이므로 local로 하면 안됨
        vPos.y = kCamPos.pos.y;
        kCurCamera.transform.position = vPos;

        // 카메라 x,z 위치는 local로 설정
        Vector3 vLocalPos = kCurCamera.transform.localPosition;
        vLocalPos.x = kCamPos.pos.x;
        vLocalPos.z = kCamPos.pos.z;
        kCurCamera.transform.localPosition = vLocalPos;

        // 카메라 위치가 바뀌었으므로 폴대 위치 동기화
        kCurCamera.UpdatePolePosition();

        // 카메라 회전/줌 정보로 카메라 설정
        kCurCamera.pan = kCamPos.rot.y;
        kCurCamera.tilt = kCamPos.rot.x;
        kCurCamera.zoom = kCamPos.zoom;
    }

    // 초기 카메라 셋팅
    public void SetInitCurCamera()
    {
        CObjCamera kObjCamera = null;
        SCamDir kCamDir = null;
        if (m_CamObjList.Count > 0)
        {
            kObjCamera = m_CamObjList[0];
            var kSaveCamPosList = CDataMgr.Inst.m_SaveCameraPosData;
            if( kSaveCamPosList.Count() > 0 )
            {
                if (kSaveCamPosList.GetCameraPos().datas.Count > 0)
                {
                    kCamDir = kSaveCamPosList.GetCameraPos().datas[0];

                    SetCurCamera(kObjCamera, kCamDir);
                }
            }
        }
    }



    private void Update()
    {
        if (Input.GetKey(KeyCode.LeftControl))
        {
            if (Input.GetKeyDown(KeyCode.F2))
            {
                DateTime now = System.DateTime.Now;
                string sFileName = $"Save/Images/vpd_img_{now.ToString("yyyy.MM.dd_HH.mm.ss")}.jpg";

                Texture2D tex = CaptureCurrentViewerTexture(m_CurCamViewerUI.m_RawImage);
                CImageHelper.SaveTextureToJPG(tex, sFileName);

                Debug.Log("CPCamObjListUI: F2 pressed - saved current viewer to PNG.");
            }
        }

    }
}

