using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using mynet;
using Newtonsoft.Json;
using UnityEngine;
using static CParkingSpace3DTo2D;



public class CPSimBaseGameUI : MonoBehaviour
{
    [Space]
    public CPMakerParkSpaceUI m_ParkSpaceUI = null;
    public CCarObjListUI m_CarObjListUI = null;
    public CPCamObjListUI m_CamObjListUI = null;
    public CPoleObjListUI m_PoleObjListUI = null;
    [Space]
    public CParkingSpace3DTo2D m_PSConverter;
    public CParkingSpaceVisualizer m_PSVisualizer;
    public bool m_IsDrawPresetFace = true;          // 프리셋 주차면 그리기 여부

    [Space]
    public mynet_vpdzoom.CPSimVpdUI m_VpdUI = null;       // 인스펙터창에서  CPSimYoloVpdUI 와 CPSimVpdUI 노드 선택해서 사용
    public CPSimLpdUI m_LpdUI = null;
    public CPSimLprUI m_LprUI = null;

    //------------------- Environment Data ------------------
    public class SEnvObj
    {
        public List<Transform> parkingSlots = new();      // 3개 주차면 Transform
        public List<CObjCar> parkingVehicles = new();  // 주차면에 배치할 차량들
        public List<CObjCar> noiseVehicles = new();    // 노이즈 차량 (주차면 외부)
    }

    protected Dictionary<int, SEnvObj> m_EnvObjs = new();   // 환경셋팅을 위한 GameObject (Key: 프리셋 id)

    [HideInInspector]
    public Dictionary<int, List<CObjCar>> m_listObjCar = new();   // 프리셋 아이디별 차량 오브젝트 리스트 ( 프리셋 아이디 - 1 부터 시작 )


    //protected List<Transform> m_ParkingSlots = new();      // 3개 주차면 Transform
    //protected List<GameObject> m_ParkingVehicles = new();  // 주차면에 배치할 차량들
    //protected List<GameObject> m_NoiseVehicles = new();    // 노이즈 차량 (주차면 외부)

    protected int m_CurPresetID = 1;   // 현재 프리셋 ID (  )

    public int CurPresetID  {  get => m_CurPresetID; set => m_CurPresetID = value;  }


    public List<Transform> GetParkingSlots(int nPresetId)
    {
        if (m_EnvObjs.ContainsKey(nPresetId))
            return m_EnvObjs[nPresetId].parkingSlots;

        return null;
    }
    public List<CObjCar> GetParkingVehicles(int nPresetId)
    {
        if (m_EnvObjs.ContainsKey(nPresetId))
            return m_EnvObjs[nPresetId].parkingVehicles;

        return null;
    }
    public List<CObjCar> GetNoiseVehicles(int nPresetId)
    {
        if (m_EnvObjs.ContainsKey(nPresetId))
            return m_EnvObjs[nPresetId].noiseVehicles;

        return null;
    }


    public virtual void Initialize()
    {
        m_ParkSpaceUI.Initialize(false);
        //m_PoleObjListUI.Initialize();     // 잠시동안은 사용안함( 차후 수정하여 사용 예정 )
        m_CamObjListUI.Initialize();

        Init_VpdLpd();

        Camera kCamera = m_CamObjListUI.m_SelectedCamObj.getCamera;
        m_CarObjListUI.Initialize(kCamera);
        m_CamObjListUI.m_SelectedCamObj.SetCameraFOV( CObjCamera.DEFAULT_VERT_FOV );   //  수직:34.6348f, 수평:58.0f);

        // 프리셋 ID별로 차량 오브젝트 리스트 변환
        ConvertObjCarListByPresetID();
    }

    /// <summary>초기화 완료 후 ROI Visualizer를 표시 상태로 유지할지 여부. 기본 false(기존 씬 회귀 없음).</summary>
    protected virtual bool KeepRoiVisibleAfterInit => false;

    // 기본 초기화 후 환경 설정 및 주차면 2D 변환기 초기화
    protected virtual void Initialize_AfterDefaultSetting()
    {
        Camera kCamera = m_CamObjListUI.m_SelectedCamObj.getCamera;
        //SetToMainCamera(kCamera);

        //바닥 주차면라인 3D -> 2D 변환기 초기화
        m_PSConverter.Initialize(kCamera);
        Init_ParkSpace2dConverter();

        Init_Environment();

        m_PSVisualizer?.Show(KeepRoiVisibleAfterInit);
    }

    public virtual void Init_Environment()
    {
        SetEnvironment();
    }

    public void SetEnvironment()
    {
        m_EnvObjs.Clear();

        // 주의: 프리셋 갯수(m_ParkSpaceUI.m_PresetObjList)와 m_listObjCar.Count와 동일해야 한다.

        for (int i = 0; i < m_ParkSpaceUI.m_PresetObjList.Count; i++)
        {
            var kPresetObj = m_ParkSpaceUI.m_PresetObjList[i];
            SEnvObj kEnvObj = new SEnvObj();

            int nSlotCount = kPresetObj.m_listFaceRect.Count;
            for (int j = 0; j < kPresetObj.m_listFaceRect.Count; j++)
            {
                kEnvObj.parkingSlots.Add(kPresetObj.m_listFaceRect[j].transform);
            }
            m_EnvObjs.Add(i + 1, kEnvObj);
        }

        // 프리셋 별 설정
        // [버그수정] slotId=0 차량 포함: 저장 데이터에서 slotId=0으로 저장된 차량은
        //   0-indexed 실수로 첫 번째 주차면 차량임에도 noiseVehicle로 잘못 분류되었음.
        //   진짜 노이즈(슬롯 외부 배치)는 slotId=-1(m_iFaceSlot=-1)이므로
        //   조건을 >= 0으로 변경하여 slotId=0 차량도 parkingVehicles에 포함한다.
        foreach (KeyValuePair<int, List<CObjCar>> itor in m_listObjCar)
        {
            List<CObjCar> kCarObjList = itor.Value;
            for (int i = 0; i < kCarObjList.Count; i++)
            {
                CObjCar kObj = kCarObjList[i];
                if (m_EnvObjs.ContainsKey(kObj.m_iPresetId))
                {
                    if (kObj.m_iFaceSlot >= 0)  // 수정: > 0 → >= 0 (slotId=0 첫 번째 주차면 포함)
                        m_EnvObjs[kObj.m_iPresetId].parkingVehicles.Add(kObj);
                    else  // m_iFaceSlot == -1: 진짜 노이즈(슬롯 외부)
                        m_EnvObjs[kObj.m_iPresetId].noiseVehicles.Add(kObj);
                }
            }
        }

        // parkingVehicles를 m_iFaceSlot 오름차순으로 정렬:
        //   m_ObjCarList(JSON datas 순서)가 슬롯 번호 순서와 다를 수 있으므로
        //   Enum_CenterisingByFrame의 for 루프가 첫 번째 주차면 차량(index=0)부터 처리되도록 보장한다.
        //   slotId=0 차량은 정렬 후 index=0에 위치하여 첫 번째로 센터라이징됨.
        foreach (KeyValuePair<int, SEnvObj> itor in m_EnvObjs)
        {
            itor.Value.parkingVehicles.Sort((a, b) => a.m_iFaceSlot.CompareTo(b.m_iFaceSlot));
        }
    }
    // 프리셋 ID별로 차량 오브젝트 리스트 변환
    public virtual void ConvertObjCarListByPresetID()
    {
        m_listObjCar.Clear();
        List<CObjCar> kList = m_CarObjListUI.m_ObjCarList;
        for (int i = 0; i < kList.Count; i++)
        {
            CObjCar kObjCar = kList[i];
            int iPresetId = kObjCar.m_iPresetId;
            if (m_listObjCar.ContainsKey(iPresetId) == false)
                m_listObjCar.Add(iPresetId, new List<CObjCar>());

            m_listObjCar[iPresetId].Add(kObjCar);
        }
    }

    public CObjCar GetCarObject(int iPresetId, int iFaceSlotId)
    {
        if (m_listObjCar.ContainsKey(iPresetId))
        {
            return m_listObjCar[iPresetId].Find(x => x.m_iFaceSlot == iFaceSlotId);
        }
        return null;
    }

    protected void Init_VpdLpd()
    {
        m_VpdUI.Initialize(m_CamObjListUI);
        m_LpdUI.Initialize(m_CamObjListUI);
        m_LprUI.Initialize(m_CamObjListUI);
    }

    /// <summary>
    /// 서버 URL 설정( vpd, lpd, lpr ) 
    /// </summary>
    public void SetVpdURL(CPSimConfig.SPSConfig kConfig)
    {
        m_VpdUI.SetURL(kConfig.vpd_url);
        m_LpdUI.SetURL(kConfig.lpd_url);
        m_LprUI.SetURL(kConfig.lpr_url);
    }

    /// <summary>
    /// 차량 오브젝트 리스트 초기화 및 재생성 
    /// </summary>
    public virtual void ResetCarObjectList()
    {
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
    }

    // 차량 앞뒤 랜덤 선택
    // 차량 오브젝트의 회전과 위치를 약간 랜덤하게 조정하여 자연스러운 배치 효과를 주는 함수
    public void RandomCarObj_FrontBack(CObjCar kCarObj, bool isFront)
    {
        // 약간회전, 약간 앞뒤
        float fAngle = UnityEngine.Random.Range(-5f, 5f);
        //kCarObj.m_IsFront = UnityEngine.Random.value > 0.5f; // 차량 앞면이 보이는지 여부를 랜덤으로 결정
        kCarObj.m_IsFront = isFront;

        // [버그수정] 슬롯 실제 방향(m_faceRot)을 기준으로 전면/후면 회전 결정
        // 기존: 절대값 0/180도 고정 → 90도 방향 슬롯에서 차량이 90도 틀어지는 버그
        float baseRotY = GetSlotBaseRotY(kCarObj.m_iPresetId, kCarObj.m_iFaceSlot - 1);
        float targetRotY = baseRotY + (kCarObj.m_IsFront ? 180f : 0f) + fAngle;
        kCarObj.transform.localRotation = Quaternion.Euler(0, targetRotY, 0);

        // 약간 이동 배치 (약간의 오프셋)
        Vector3 slotPos = kCarObj.transform.localPosition;
        Vector3 offset = new Vector3(
            UnityEngine.Random.Range(-0.15f, 0.15f),  // X 방향 약간 틀어짐
            0,
            UnityEngine.Random.Range(-0.3f, 0.3f)     // Z 방향 앞뒤 약간 차이
        );
        kCarObj.transform.localPosition = slotPos + offset;

        // 차량 색상 무작위화
        //kCarObj.SetVehicleColor(CSaveCarPosData.GetRandomColor());
    }

    public void RandomFrontBack_CarPosDatas()
    {
        var kCarPosData = CDataMgr.Inst.m_SaveCarPosData;
        foreach (var data in kCarPosData.m_Data.datas)
        {
            if (data.slotId > 0)  // 주차면에 배치된 차량만 랜덤 적용
            {
                // 프리셋 ID와 슬롯 ID를 기준으로 해당 차량 오브젝트 찾기
                if (m_EnvObjs.ContainsKey(data.presetId))
                {
                    var kEnvObj = m_EnvObjs[data.presetId];
                    var kCarList = kEnvObj.parkingVehicles;
                    var kCarObj = kCarList.Find( x => x.m_iFaceSlot == data.slotId);
                    if (kCarObj != null)
                    {
                        data.isFront = UnityEngine.Random.value > 0.5f; // 차량 앞면이 보이는지 여부를 랜덤으로 결정
                        kCarObj.m_IsFront = data.isFront;
                        RandomCarObj_FrontBack(kCarObj, data.isFront);
                    }
                }
            }
        }
    }

    // 프리셋 ID에 해당하는 차량 오브젝트 리스트의 각 차량에 대해 앞뒤 랜덤 배치 적용
    public List<CObjCar> RandomFrontBack_CarObjectList(int presetId)
    {
        var kCarPosData = CDataMgr.Inst.m_SaveCarPosData;
        SEnvObj kEnvObj = m_EnvObjs[presetId];
        List<CObjCar> kCarList = kEnvObj.parkingVehicles;
        for (int i = 0; i < kCarList.Count; i++)
        {
            CObjCar kCarObj = kCarList[i];
            
            if (kCarObj.gameObject.activeSelf)
            {
                SObjectPos kCarPos = kCarPosData.m_Data.datas.Find(x => x.presetId == presetId && x.slotId == kCarObj.m_iFaceSlot);
                bool isFront = UnityEngine.Random.value > 0.5f; // 차량 앞면이 보이는지 여부를 랜덤으로 결정
                kCarPos.isFront = isFront;
                RandomCarObj_FrontBack(kCarObj, isFront);
            }
        }
        return kCarList;
    }

    /// <summary>
    /// 주차 슬롯의 기준 Y축 회전값을 반환한다.
    /// CFaceRect.m_faceRot 우선, 없으면 Transform.rotation.eulerAngles.y 사용.
    /// </summary>
    /// <param name="presetId">프리셋 ID (1-based)</param>
    /// <param name="slotIdx">슬롯 인덱스 (0-based)</param>
    private float GetSlotBaseRotY(int presetId, int slotIdx)
    {
        if (!m_EnvObjs.ContainsKey(presetId))
            return 0f;

        var slots = m_EnvObjs[presetId].parkingSlots;
        if (slotIdx < 0 || slotIdx >= slots.Count)
            return 0f;

        Transform slotTr = slots[slotIdx];
        CFaceRect faceRect = slotTr.GetComponent<CFaceRect>();
        if (faceRect != null)
            return faceRect.m_faceRot;

        return slotTr.localRotation.eulerAngles.y;
    }

    // 
    /// <summary>
    /// 약간의 위치 및 회전 변경  
    /// </summary>
    /// <param name="iSlotIndex"> 주차면 슬롯 순서 인덱스 ( slotId( 1부터 시작 )와 다름 ), 0부터 시작 </param>
    /// <param name="kParkingVehicle"></param>
    /// <param name="nPresetId"></param>
    /// <returns></returns>
    // return: 전면 주차 여부
    public bool SetRandomCarPos_ByParkSlot(int iSlotIndex, GameObject kParkingVehicle, int nPresetId = 1)
    {
        // [버그수정] 조건 반전(!  누락) 수정: 프리셋이 없을 때만 조기 반환
        if (!m_EnvObjs.ContainsKey(nPresetId))
        {
            Debug.Assert(false, $"{nPresetId}가 존재하지 않는다.!!!");
            return true;
        }


        List<Transform> parkingSlots = m_EnvObjs[nPresetId].parkingSlots;

        // 주차면 위치에 배치 (약간의 오프셋)
        Vector3 slotPos = parkingSlots[iSlotIndex].position;
        Vector3 offset = new Vector3(
            UnityEngine.Random.Range(-0.15f, 0.15f),  // X 방향 약간 틀어짐
            0,
            UnityEngine.Random.Range(-1.0f, 1.0f)     // Z 방향 앞뒤 약간 차이
        );
        kParkingVehicle.transform.position = slotPos + offset;

        bool isFront = true;

        // 전면 주차 or 후면 주차 무작위 결정
        float frontBackAngle = 0.0f;

        if (iSlotIndex >= 0)  // 주차면이 존재하는 경우에만 전면/후면 주차 결정
        {
            int nRes = UnityEngine.Random.Range(0, 5);
            if (nRes != 0)
            {
                // 전면 주차
                frontBackAngle = 180.0f;
                isFront = true;
            }
            else
            {
                // 후면 주차
                frontBackAngle = 0.0f;
                isFront = false;
            }
        }
        // [버그수정] 슬롯 실제 방향(m_faceRot)을 기준 회전으로 사용
        float slotBaseRotY = GetSlotBaseRotY(nPresetId, iSlotIndex);
        float rotationOffset = UnityEngine.Random.Range(-5f, 5f);
        kParkingVehicle.transform.localRotation = Quaternion.Euler(0, slotBaseRotY + frontBackAngle + rotationOffset, 0);

        var kObjCar = kParkingVehicle.GetComponent<CObjCar>();
        if( kObjCar != null )
            kObjCar.m_IsFront = isFront;

        Debug.Log($"[IsFront] 차량 {kObjCar.name} 전면 주차 여부: {isFront}");

        // 차량 색상 무작위화
        //RandomizeVehicleColor(m_ParkingVehicles[i]);

        return isFront;
    }

    /// <summary>
    /// 초기 3D 주차면 -> 2D 주차면 변환하기 
    /// </summary>
    protected virtual void Init_ParkSpace2dConverter()
    {
        if (m_PSConverter == null) return;

        Camera kCamera = m_CamObjListUI.m_SelectedCamObj.getCamera;
        m_PSConverter.Initialize(kCamera);

        for (int j = 0; j < m_ParkSpaceUI.m_PresetObjList.Count; j++)
        {
            var kPresetObjUI = m_ParkSpaceUI.m_PresetObjList[j];
            CParkingSpace3DTo2D.SPreset3D kPreset = new CParkingSpace3DTo2D.SPreset3D();

            for (int i = 0; i < kPresetObjUI.m_listFaceRect.Count; i++)
            {
                LineRenderer lr = kPresetObjUI.m_listFaceRect[i].GetLineRenderer();
                var tr = lr.GetComponent<Transform>();

                m_PSConverter.AddFaceFromLineRenderer(i, lr, tr.position, kPreset.parking_spaces);
            }

            m_PSConverter.AddPreset(j + 1, kPreset);
        }
        // 현재 프리셋은 0번재 프리셋을 기준으로 설정 ( presetId = 1 )
        var kCurPreset = m_PSConverter.m_PresetList.GetPresetByID(m_CurPresetID);
        m_PSConverter.SetCurPreset(kCurPreset);


        // 주차면 2D 라인 시각화 갱신
        if (m_PSVisualizer != null)
        {
            if (m_IsDrawPresetFace)
                m_PSVisualizer.RefreshVisualization(kCurPreset);

            // 주차면 2D 라인 파일 저장
            string dir = "Save/ParkSpaceData";
            if (!Directory.Exists(dir))
                Directory.CreateDirectory(dir);
            string outPath = Path.Combine(dir, $"Face_EV{kCurPreset.preset_idx}.json");

            m_PSConverter.SaveToJson(outPath, kCurPreset);
        }
    }

    //화면 스크린샷 저장
    public virtual void SaveFile_ScreenShot(string fileName = "", string sFolder = "Save/CamCapture")
    {
        Camera kCamera = m_CamObjListUI.m_SelectedCamObj.getCamera;
        if (kCamera != null)
        {
            Texture2D kTexture = CImageHelper.CaptureImage(kCamera);
            if (kTexture == null) return;

            string dir = sFolder; // "Save/CamCapture";
            if (!Directory.Exists(dir))
                Directory.CreateDirectory(dir);

            if (string.IsNullOrEmpty(fileName))
                fileName = $"CamCapture_{DateTime.Now:yyyyMMdd_HHmmss}.jpg";

            string outPath = Path.Combine(dir, fileName);
            CImageHelper.SaveTextureToFile(kTexture, outPath, true, 90);

            // ✅ [메모리 누수 수정] CaptureImage()로 생성된 Texture2D를 파일 저장 후 즉시 해제
            // Unity Texture2D는 UnityEngine.Object이므로 C# GC가 수집하지 않음.
            // Destroy()를 명시적으로 호출해야 GPU/native 메모리가 해제됨.
            UnityEngine.Object.Destroy(kTexture);
        }
    }

    // 메인 카메라 위치를 서브 카메라 위치로 설정 (VPD, LPD 등에서 검지 결과를 메인 카메라 뷰어에 반영하기 위해 사용) 
    public void SetToMainCamera(Camera kCamera)
    {
        if (kCamera != null)
        {
            Camera.main.transform.position = kCamera.transform.position;
            Camera.main.transform.rotation = kCamera.transform.rotation;
            Camera.main.fieldOfView = kCamera.fieldOfView;
        }
    }
    
    // 모든 차량 오브젝트 숨기기
    protected void HideAll_CarList()
    {
        List<CObjCar> kCarList = m_CarObjListUI.m_ObjCarList;
        foreach (var kCar in kCarList)
        {
            kCar.gameObject.SetActive(false);
        }
    }

    /// <summary>
    /// overlay 이미지 저장
    ///  -- kTexture, detections, "Save/OverlayImages", "LPD", true
    /// </summary>
    public static void SaveOverayImage(Texture2D kTexture, List<SDetectionData> kDectionList, string pathDir, string prefix, bool isDataSave = true)
    {
        if (kTexture == null) return;

        if (isDataSave)
            CImageHelper.SaveFileBoxInfo(kDectionList, pathDir, prefix);

        Texture2D overlayTex = CImageHelper.CreateOverlayTexture(kTexture, kDectionList, Color.red, 3);
        if (overlayTex == null)
        {
            Debug.LogWarning("SaveOverayImage!!!... (overlayTex == null )");
            return;
        }

        string dir = pathDir;
        if (!Directory.Exists(dir))
            Directory.CreateDirectory(dir);

        string outPath = Path.Combine(dir, $"{prefix}_overlay_{DateTime.Now:yyyyMMdd_HHmmss}.png");
        File.WriteAllBytes(outPath, overlayTex.EncodeToPNG());
        Debug.Log("Overlay image saved: " + outPath);
        Destroy(overlayTex);
    }

    /// <summary>
    /// 차량 무작위화 - RESET 시 호출
    /// 1. 주차 차량 개수 및 위치 무작위화
    /// 2. 노이즈 차량 배치 무작위화
    /// </summary>
    protected void RandomizeVehicles(int nPresetId, int seed, bool bRandomShow = true)
    {
        if (seed > 0)
        {
            UnityEngine.Random.InitState(seed);
        }

        List<Transform> kParkingSlots = GetParkingSlots(nPresetId);
        List<CObjCar> kParkingVehicles = GetParkingVehicles(nPresetId);
        List<CObjCar> kNoiseVehicles = GetNoiseVehicles(nPresetId);

        // 1. 주차 차량 무작위 배치 (주차면에 있는 차량)
        //if (m_ParkingVehicles != null && m_ParkingSlots != null)
        {
            // 각 주차면마다 차량 배치 여부 결정
            for (int i = 0; i < kParkingSlots.Count && i < kParkingVehicles.Count; i++)
            {
                if (kParkingVehicles[i] == null || kParkingSlots[i] == null) continue;

                // 50% 확률로 차량 배치

                bool hasVehicle = UnityEngine.Random.value > 0.5f;
                if (bRandomShow)
                    kParkingVehicles[i].Show(hasVehicle);
                else
                    hasVehicle = true;

                if (hasVehicle)
                {
                    SetRandomCarPos_ByParkSlot(i, kParkingVehicles[i].gameObject, nPresetId);

                    // 차량 색상 무작위화
                    //RandomizeVehicleColor(m_ParkingVehicles[i]);
                }
            }

            //// Orignal Truth 보관
            //for (int i = 0; i < m_ParkingVehicles.Length; i++)
            //{
            //    if (m_ParkingVehicles[i] != null )
            //        m_OrignalTruths[i] = m_ParkingVehicles[i].activeSelf ? 1 : 0;
            //    else
            //        m_OrignalTruths[i] = 0;
            //}
        }

        // 2. 노이즈 차량 배치 (주차면 외부, 도로, 기타 위치)
        //if (m_NoiseVehicles != null)
        {
            foreach (var noiseVehicle in kNoiseVehicles)
            {
                if (noiseVehicle == null) continue;

                // 30~50% 확률로 노이즈 차량 배치
                bool isActive = UnityEngine.Random.value > 0.6f;
                noiseVehicle.Show(isActive);

                if (isActive)
                {
                    // 노이즈 차량 색상 무작위화
                    //RandomizeVehicleColor(noiseVehicle);
                }
            }
        }

        // 3. 조명 약간 변화 (시간대 변화 효과)
        RenderSettings.ambientIntensity = UnityEngine.Random.Range(0.85f, 1.15f);

        Debug.Log($"[RandomizeVehicles] Seed={seed}, Parking vehicles randomized");
    }

}
