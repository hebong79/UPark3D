
using UnityEngine;
using UnityEngine.SceneManagement;
using UnityEngine.UI;


/*
 *  Preset Maker Main Menu
 * 
 */
public class CPMakerMenuDlg : CDialogUI
{
    public CPresetMakerDlg m_PresetMakerDlg;
    public CCarPlacementDlg m_CarPlacementDlg;
    public CPCamControlDlg m_CamContrlDlg;
    public CMapSizeDlg m_MapSelectDlg;
    public CPolePlacementDlg m_PolePlacementDlg;
    //public CPresetMappingDlg m_PresetMappingDlg;

    [Space]
    public Button m_btnPresetMaker;
    public Button m_btnCarPlacement;
    public Button m_btnCamObjPlacement;
    public Button m_btnCamControl;
    public Button m_btnMapSelect;
    [Space]
    public Button m_btnVlaImgMaker;
    public Button m_btnVlaSimulator;
    public Button m_btnExit;
    //public Button m_btnPresetMapping;

    // Start is called once before the first execution of Update after the MonoBehaviour is created
    void Start()
    {
        m_btnExit.onClick.AddListener(() => { Application.Quit(); });

        m_btnPresetMaker?.onClick.AddListener(OnClicked_PresetMaker);
        m_btnCarPlacement?.onClick.AddListener(OnClicked_CarPlacement);
        m_btnCamObjPlacement?.onClick.AddListener(OnClicked_PolePlacement);

        m_btnCamControl?.onClick.AddListener(OnClicked_CamControl);

        m_btnMapSelect?.onClick.AddListener(OnClicked_MapSelect);
        //m_btnPresetMapping?.onClick.AddListener(OnClicked_PresetMapping);

        m_btnVlaImgMaker.onClick.AddListener(() => { 
            SceneManager.LoadScene("04_ParkSimMaker_VLA"); 
        });
        m_btnVlaSimulator.onClick.AddListener(() => { 
            SceneManager.LoadScene("06_PtzCameraSimulator"); 
        });
    }

    void OnClicked_PresetMaker()
    {
        CPresetMakerDlg kDlg = m_PresetMakerDlg; 
        if(kDlg.IsShow())
            kDlg.CloseUI();
        else
            kDlg.OpenUI();

        m_CamContrlDlg.CloseUI();
        m_CarPlacementDlg.CloseUI();
        m_MapSelectDlg.CloseUI();
        //m_PresetMappingDlg.CloseUI();
        //m_PolePlacementDlg.CloseUI();

    }

    void OnClicked_CarPlacement()
    {
        CCarPlacementDlg kDlg = m_CarPlacementDlg; 
        if (kDlg.IsShow())
            kDlg.CloseUI();
        else
            kDlg.OpenUI();

        m_CamContrlDlg.CloseUI();
        m_PresetMakerDlg.CloseUI();
        m_MapSelectDlg.CloseUI();
    }

    void OnClicked_PolePlacement()
    {
        CPolePlacementDlg kDlg = m_PolePlacementDlg; 
        if (kDlg.IsShow())
            kDlg.CloseUI();
        else
            kDlg.OpenUI();
    }
    void OnClicked_CamControl()
    {
        CPCamControlDlg kDlg = m_CamContrlDlg; 
        if (kDlg.IsShow())
            kDlg.CloseUI();
        else
            kDlg.OpenUI();

        m_PresetMakerDlg.CloseUI();
        m_CarPlacementDlg.CloseUI();
        m_MapSelectDlg.CloseUI();
    }


    void OnClicked_MapSelect()
    {
        CMapSizeDlg kDlg = m_MapSelectDlg;// CDataMgr.Inst.pmakerScene.m_HudUI.m_MapSelectDlg;
        if (kDlg.IsShow())
            kDlg.CloseUI();
        else
            kDlg.OpenUI();

        m_PresetMakerDlg.CloseUI();
        m_CamContrlDlg.CloseUI();
        m_CarPlacementDlg.CloseUI();
    }


    //void OnClicked_PresetMapping()
    //{
    //    CPresetMappingDlg kDlg = m_PresetMappingDlg;// CDataMgr.Inst.pmakerScene.m_HudUI.m_MapSelectDlg;
    //    if (kDlg.IsShow())
    //        kDlg.CloseUI();
    //    else
    //        kDlg.OpenUI();

    //}
}


