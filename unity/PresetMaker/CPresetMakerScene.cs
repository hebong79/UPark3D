using UnityEngine;

public class CPresetMakerScene : MonoBehaviour
{
    public CPMakerGameUI m_GameUI = null;
    public CPMakerHudUI m_HudUI = null;
    public CCamMouseControl m_CamMouseControl = null;
    public CCamKeybordControl m_CamKeyControl = null;
    public MsgBoxUI m_MsgBoxUI = null;

    private void Awake()
    {
        Application.runInBackground = true;

        CDataMgr.Inst.pmakerScene = this;
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
