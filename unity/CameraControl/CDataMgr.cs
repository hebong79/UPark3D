using System;
using System.Collections.Generic;
using UnityEngine;
using static CPSimConfig;
using static CSaveInitCampPos;

public class CDataMgr
{
    static CDataMgr _inst = null;
    public static CDataMgr Inst {
        get {
            if( _inst == null)
                _inst = new CDataMgr();

            return _inst; 
        } 
    }

    private CDataMgr() {  }

    public const string DSAVE_PATH_3D = "./Save/3D";
    public const string DCAR_PREFAB_PATH = "Prefabs/Car/New";
    public const string DCAMERAOBJ_PREFAB_PATH = "Prefabs/CameraObj";
    public const string DSAVE_PATH_PRESET = "./Save/3D/Preset";
    public const string DSAVE_PATH_CARPOS = "./Save/3D/CarPos";
    public const string DSAVE_PATH_CAMERAPOS = "./Save/3D/CameraPos";
    public const string DSAVE_PATH_POLEPOS = "./Save/3D/PolePos";
    public const string DSAVE_PATH_MAP = "./Save/3D/Map";
    public const string DSAVE_PATH_OVERAP_IMG = "./Save/3D/PresetImage";
    
    public const string DSAVE_PRESET_FILENAME = "PresetList.json";
    public const string DSAVE_CARPOS_FILENAME = "CarPosList.json";

    public CPresetMakerScene pmakerScene { get; set; } = null;      // 프리셋 메이커 씬
    public CPSimScene simScene { get; set; } = null;                // 시뮬레이션 씬
    public CPSimConfig simConfig { get => m_SimConfig;  }              // 시뮬레이션 설정 정보
    public CImgDataMakerScene imgDataMakerScene { get; set; } = null; // 이미지 데이터 메이커 씬

    public CSavePresetData m_SavePresetData = new CSavePresetData();            // 프리셋 저장
    public CSaveCarPosData m_SaveCarPosData = new CSaveCarPosData();            // 차량 배치 저장
    public CSaveMapSizeData m_SaveMapSizeData = new CSaveMapSizeData();   // 맵 선택 저장 (현재 저장은 수동으로 json 파일을 만들어 사용한다.) 
    public CSavePolePosData m_SavePolePosData = new CSavePolePosData();       // 카메라 폴대 오브젝트 배치 저장

    public CKeyControlFSM m_KeyControlFSM = new CKeyControlFSM();               // 키 컨트롤 제어 FSM    

    public CBaseCamera m_CurPtzCamera = null;                                     // 현재 PTZ, static 카메라
    public List<CBaseCamera> m_PtzCameraList = new List<CBaseCamera>();           // 씬에 존재하는 PTZ, static 카메라 리스트

    public CSaveInitCampPos m_SaveCameraPosData = new CSaveInitCampPos();         // 카메라 초기 위치 저장 (CPCamControlDlg에서 사용)
    //public List<CSaveInitCampPos> m_SaveCamPosList = new List<CSaveInitCampPos>();    // 카메라 초기 위치 리스트 (CPSimScene, CPSimGameUI 에서 사용)

    public CPSimConfig m_SimConfig = new CPSimConfig();     // 시뮬레이션 설정 정보 (맵 정보, 차량 정보, 카메라 정보 등)

    public CVLMimgMakerInfo m_VlmImgMakeInfo = new CVLMimgMakerInfo(); // VLM 이미지 메이커 정보 (프리셋 이름, 차량 위치 이름, 카메라 위치 이름 리스트)

    public void Initialize()
    {
        m_KeyControlFSM.Initialize(OnEnter_Normal, OnEnter_PresetMake, OnEnter_CarPlacement, OnEnter_CameraPicking);
    }

    public void OnUpdate(float fElapsedTime)
    {
        m_KeyControlFSM.OnUpdate();    
    }


    public void SetNormalState() { m_KeyControlFSM.SetNormalState(); }
    public void SetPresetMakeState() { m_KeyControlFSM.SetPresetMakeState();  }
    public void SetCarPlacementState(){ m_KeyControlFSM.SetCarPlacementState();  }
    public void SetCamObjPlacementState() { m_KeyControlFSM.SetPolePlacementState(); }
       

    public bool IsNormalState() { return m_KeyControlFSM.IsNormalState(); }
    public bool IsPresetMakeState() { return m_KeyControlFSM.IsPresetMakeState(); }
    public bool IsCarPlacementState() { return m_KeyControlFSM.IsCarPlacementState(); }
    public bool IsCamObjPlacementState() { return m_KeyControlFSM.IsPolePlacementState(); }


    void OnEnter_Normal()
    {
        pmakerScene.m_HudUI.m_PresetMakerDlg.Exit_PresetMakeState();
        pmakerScene.m_HudUI.m_CarPlacementDlg.Exit_CarPlacement();
        //pmakerScene.m_HudUI.m_PolePlacementDlg.Exit_PolePlacement();
        pmakerScene.m_HudUI.m_CamControlDlg.Exit_CameraPicking();
    }
    void OnEnter_PresetMake()
    {
        pmakerScene.m_HudUI.m_PresetMakerDlg.Enter_PresetMakeState();
    }

    void OnEnter_CarPlacement()
    {
        pmakerScene.m_HudUI.m_CarPlacementDlg.Enter_CarPlacement();
    }
    //void OnEnter_PolePlacement()
    //{
    //    pmakerScene.m_HudUI.m_PolePlacementDlg.Enter_PolePlacement();
    //}

    void OnEnter_CameraPicking()
    {
        pmakerScene.m_HudUI.m_CamControlDlg.Enter_CameraPicking();
    }


    public void LoadConfig(string sPathName = "Config/simulator_config.json", Action<SPSConfig> cbLoadComplete = null)
    {
        m_SimConfig.LoadConfig(sPathName, cbLoadComplete);
    }


    public void LoadConfig2(string sPathName = "Config/simulator_config.json", Action<SPSConfig> cbLoadComplete = null)
    {
        m_SimConfig.LoadConfig2(sPathName, cbLoadComplete);
    }


    //프리셋 파일 저장 ----------------------------------------------------------------------------
    public void SavePresetFile(string sPath, string sFileName)
    {
        if (!sFileName.Contains(".json") && !sFileName.Contains(".JSON"))
            sFileName += ".json";

        m_SavePresetData.SaveToJson(sPath, sFileName);
    }

    public bool LoadPresetFile(string sPath, string sFileName)
    {
        if (!sFileName.Contains(".json") && !sFileName.Contains(".JSON"))
            sFileName += ".json";

        return m_SavePresetData.LoadFromJson(sPath, sFileName);
    }
    public bool LoadPresetFileFromDataMgr(string sPathName)
    {
        //sFileName += ".json";
        return m_SavePresetData.LoadFromJson("", sPathName);
    }

    public void RemoveAll_PresetDataList()
    {
        m_SavePresetData.RemoveAll();
    }

    //차량 오브젝트 위치 저장 ----------------------------------------------------------------------------
    public void SaveCarPosFile(string sPath, string sFileName)
    {
        if (!sFileName.Contains(".json") && !sFileName.Contains(".JSON"))
            sFileName += ".json";

        m_SaveCarPosData.SaveToJson(sPath, sFileName);
    }

    public bool LoadCarPosFile(string sPath, string sFileName)
    {
        if (!sFileName.Contains(".json") && !sFileName.Contains(".JSON"))
            sFileName += ".json";

        return m_SaveCarPosData.LoadFromJson(sPath, sFileName);
    }
    public bool LoadCarPosFileFromDataMgr(string sPathName)
    {
        //sFileName += ".json";
        return m_SaveCarPosData.LoadFromJson("", sPathName);
    }

    public void RemoveAll_CarPosList()
    {
        m_SaveCarPosData.Clear();
    }

    public SObjectPos AddCarPosData(Vector3 vPos, float rotY, int prefabId, int carListCount)
    {
        int iCount = carListCount;
        string sTime = DateTime.Now.ToString(("HH.mm.ss"));

        SObjectPos kCarPos = new SObjectPos();

        kCarPos.id = $"{iCount}-{sTime}";
        kCarPos.prefabId = prefabId;
        kCarPos.pos.Set(vPos);
        kCarPos.rotY = rotY;

        m_SaveCarPosData.Add(kCarPos);

        return kCarPos;
    }


    //카메라 위치 저장 (폴대 위치 기준 Local Postion) -------------------------------------------
    public void SaveCameraPosFile(string sPath, string sFileName)
    {
        if (!sFileName.Contains(".json") && !sFileName.Contains(".JSON"))
            sFileName += ".json";

        m_SaveCameraPosData.SaveToJson(sPath, sFileName);
    }
    

    // 카메라 위치 전체로딩일경우는 리스트를 반드시 초기화 해야 한다. 
    public bool LoadCameraPosFile(string sPath, string sFileName)
    {
        if (!sFileName.Contains(".json") && !sFileName.Contains(".JSON"))
            sFileName += ".json";

        CSaveInitCampPos kCamData = new CSaveInitCampPos();
        if (kCamData.LoadFromJson(sPath, sFileName))
        {
            //m_SaveCamPosList.Add(kCamData);
            m_SaveCameraPosData = kCamData;
            return true;
        }
        return false;
    }
    public bool LoadCameraPosFile_FullPathName(string sPathName)
    {
        //sFileName += ".json";
        CSaveInitCampPos kCamData = new CSaveInitCampPos();
        if (kCamData.LoadFromJson("", sPathName))
        {
            //m_SaveCamPosList.Add(kCamData);
            m_SaveCameraPosData = kCamData;
            return true;
        }
        return false;
    }

    public void RemoveAll_CameraPosList()
    {
        m_SaveCameraPosData.Clear();
    }

    //폴대 오브젝트 위치 저장 -------------------------------------------------------------------
    public void SavePolePosFile(string sPath, string sFileName)
    {
        if (!sFileName.Contains(".json") && !sFileName.Contains(".JSON"))
            sFileName += ".json";

        m_SavePolePosData.SaveToJson(sPath, sFileName);
    }

    public bool LoadPolePosFile(string sPath, string sFileName)
    {
        if (!sFileName.Contains(".json") && !sFileName.Contains(".JSON"))
            sFileName += ".json";

        return m_SavePolePosData.LoadFromJson(sPath, sFileName);
    }
    public bool LoadPolePosFile_FullPathName(string sPathName)
    {
        //sFileName += ".json";
        return m_SavePolePosData.LoadFromJson("", sPathName);
    }

    public void RemoveAll_PolePosList()
    {
        m_SavePolePosData.Clear();
    }




    //맵 저장/열기 ----------------------------------------------------------------------------
    public void SaveMapSizeFile(string sFileName)
    {
        sFileName += ".json";
        m_SaveMapSizeData.SaveToJson(DSAVE_PATH_MAP, sFileName);
    }

    public bool LoadMapSizeFile(string sFileName)
    {
        sFileName += ".json";
        return m_SaveMapSizeData.LoadFromJson(DSAVE_PATH_MAP, sFileName);
    }
    public bool LoadMapSizeFileFromDataMgr(string sPathName)
    {
        //sFileName += ".json";
        return m_SaveMapSizeData.LoadFromJson("", sPathName);
    }

    /// <summary>
    /// Map Data 로딩
    ///  - sFileName은 확장자 없는 파일명 ( 패스도 없음)
    /// </summary>
    /// <param name="sFileName"></param>
    /// <returns></returns>
    public SSaveMapInfo LoadMapData(string sFileName)
    {
        if (LoadMapSizeFile(sFileName))
        {
            var kMapData = m_SaveMapSizeData.m_Data;
            //this.LoadPresetFileFromDataMgr(kMapData.preset);
            //this.LoadCarPosFileFromDataMgr(kMapData.car);
            return kMapData;
        }
        return null;
    }

}
