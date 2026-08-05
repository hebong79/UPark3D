using UnityEngine;

public class CPresetMakerScene : MonoBehaviour
{
    public static string DCONFIG_PATH = "Config/config01_preset_maker.json";

    public CPMakerGameUI m_GameUI = null;
    public CPMakerHudUI m_HudUI = null;
    public CCamMouseControl m_CamMouseControl = null;
    public CCamKeybordControl m_CamKeyControl = null;
    public MsgBoxUI m_MsgBoxUI = null;

    private void Awake()
    {
        Application.runInBackground = true;

        CDataMgr.Inst.pmakerScene = this;
        LoadInitialData();
    }

    private void LoadInitialData()
    {
        CDataMgr.Inst.LoadConfig2(DCONFIG_PATH, (kConfig) =>
        {
            if (!CDataMgr.Inst.LoadPresetFile(CDataMgr.DSAVE_PATH_PRESET, kConfig.preset_file))
                Debug.LogWarning("프리셋 파일 로드 실패!!!");

            if (!CDataMgr.Inst.LoadCameraPosFile(CDataMgr.DSAVE_PATH_CAMERAPOS, kConfig.camerapos_file))
                Debug.LogWarning($"카메라위치 로드 실패!!! ({kConfig.camerapos_file})");

            if (!CDataMgr.Inst.LoadCarPosFile(CDataMgr.DSAVE_PATH_CARPOS, kConfig.carpos_file))
                Debug.LogWarning("차량 위치 로드 실패!!!");
        });
    }
    // Start is called once before the first execution of Update after the MonoBehaviour is created
    void Start()
    {
        Initialize();
    }


    public void Initialize()
    {
        CDataMgr.Inst.Initialize();

        m_GameUI.Initialize();
        m_HudUI.Initialize();
    }

    public void SetSelectTargetObject(GameObject go)
    {
        m_CamMouseControl.m_TargetObject = go.transform;
    }

    // Update is called once per frame
    void Update()
    {
        CDataMgr.Inst.OnUpdate(Time.deltaTime);
    }
}
