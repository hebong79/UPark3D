using System;
using UnityEngine;
using UnityEngine.UI;

public class CPCamAddUI : MonoBehaviour
{
    [Space]
    public Dropdown   m_cboCameraList  = null;  // 카메라 리스트 드롭다운
    public InputField m_editCameraName = null;  // 카메라 이름 입력 필드 (선택)

    public Button m_btnAdd    = null;           // 카메라 추가 버튼
    //public Button m_btnRepair = null;           // 카메라 수정 버튼
    public Button m_btnDelete = null;           // 카메라 삭제 버튼

    [HideInInspector]
    public CPCamObjListUI m_CamObjListUI = null;  // 카메라 오브젝트 리스트 UI
    private CObjCamera     m_CurCamera    = null;  // 현재 선택된 카메라 오브젝트

    public Action<CObjCamera> OnCallbackChangedCamera = null; // 카메라 선택 변경 시 콜백
    public Action             OnCameraListChanged     = null; // 카메라 추가/삭제 후 콜백
    public Action<CObjCamera> OnCameraAdded           = null; // 신규 카메라 생성 완료 시 콜백 (UI 초기화용)


        // Start is called once before the first execution of Update after the MonoBehaviour is created
    void Start()
    {
        m_btnAdd.onClick.AddListener(OnClick_Add);
        //m_btnRepair.onClick.AddListener(OnClick_Repair);
        m_btnDelete.onClick.AddListener(OnClick_Delete);

    }

    public void Initialize(CPCamObjListUI kCamObjListUI )
    {
        m_CamObjListUI = kCamObjListUI;
        Init_DropdownCamera();
    }

    public void Init_DropdownCamera()
    {
        m_cboCameraList.ClearOptions();
        var options = new System.Collections.Generic.List<Dropdown.OptionData>();

        int i = 0;
        foreach (var cam in m_CamObjListUI.m_CamObjList)
        {
            options.Add(new Dropdown.OptionData(cam.GetCameraName()));
            i++;
        }
        m_cboCameraList.AddOptions(options);
        m_cboCameraList.onValueChanged.RemoveListener(OnValueChanged_Camera); // 중복 리스너 방지
        m_cboCameraList.onValueChanged.AddListener(OnValueChanged_Camera);
        m_cboCameraList.value = 0;

        OnChanged_Camera(0);

        //m_editPathName.text = "fileName";
       // m_InPresetID.text = "1";
    }
    public void OnValueChanged_Camera(int iNo)
    {
        OnChanged_Camera(iNo);
    }

    void OnChanged_Camera(int iNo)
    {
        if (iNo < 0 || iNo >= m_CamObjListUI.m_CamObjList.Count)
            return;

        // 카메라 오브젝트 설정
        CObjCamera kObjCam = m_CamObjListUI.GetObjCamera(iNo);
        if (kObjCam != null)
        {
            m_CurCamera = kObjCam;
        }

        OnCallbackChangedCamera?.Invoke(kObjCam);

        // 카메라 뷰어 설정
        m_CamObjListUI.SetSelect(iNo);

        // UI 설정
        //SetUI_Camera(m_CurCamera);
        //m_CamDistDlg.SetCurCarmera(m_CurCamera);

        //FromSaveData();
    }


    /// <summary>
    /// 새 PTZ 카메라를 추가한다.
    /// m_editCameraName 에 텍스트가 있으면 해당 이름을 사용하고, 없으면 "PTZCamera-N"으로 자동 생성한다.
    /// </summary>
    public void OnClick_Add()
    {
        if (m_CamObjListUI == null) return;

        int newIdx = m_CamObjListUI.m_CamObjList.Count+1;
        string sName = (m_editCameraName != null && !string.IsNullOrEmpty(m_editCameraName.text))
            ? m_editCameraName.text
            : $"PTZCamera-{newIdx}";

        CObjCamera newCam = m_CamObjListUI.SetCameraObjectAndRenderTexture(
            CBaseCamera.ECameraType.PTZ, sName);
        if (newCam == null) return;

        // 카메라별 폴대 생성 (m_PolePrefab 이 Inspector에 설정된 경우)
        if (m_CamObjListUI.m_PolePrefab != null)
            newCam.CreatePole(m_CamObjListUI.m_PolePrefab);

        // 저장 데이터에 새 카메라 슬롯과 기본 프리셋 추가
        int newCamIdx = m_CamObjListUI.m_CamObjList.Count - 1;
        var newCamPos = CDataMgr.Inst.m_SaveCameraPosData.GetCameraPos(newCamIdx+1);
        if (newCamPos.GetCount() == 0)
        {
            var defaultDir = new CSaveInitCampPos.SCamDir("Preset 1");
            defaultDir.cam_id   = newCam.Idx;
            defaultDir.preset_id = 1;
            newCamPos.Add(defaultDir);
        }

        string sDisplay = newCam.GetCameraName();
        m_cboCameraList.options.Add(new Dropdown.OptionData(sDisplay));

        int lastIdx = m_cboCameraList.options.Count - 1;
        m_cboCameraList.value = lastIdx;
        m_cboCameraList.RefreshShownValue();

        // OnCallbackChangedCamera 가 먼저 실행되어 m_CurCamera 가 교체된 뒤 OnCameraAdded 호출
        OnChanged_Camera(lastIdx);
        OnCameraListChanged?.Invoke();
        OnCameraAdded?.Invoke(newCam);
    }

    /// <summary>
    /// 현재 선택된 카메라의 이름을 m_editCameraName 의 텍스트로 수정한다.
    /// 입력 필드가 비어 있으면 기존 이름을 유지한다.
    /// </summary>
    //public void OnClick_Repair()
    //{
    //    if (m_CamObjListUI == null) return;

    //    int idx = m_cboCameraList.value;
    //    CObjCamera cam = m_CamObjListUI.GetObjCamera(idx);
    //    if (cam == null) return;

    //    string sNewName = (m_editCameraName != null && !string.IsNullOrEmpty(m_editCameraName.text))
    //        ? m_editCameraName.text
    //        : cam.GetCameraName();

    //    //cam.m_NameId          = sNewName;
    //    cam.gameObject.name   = sNewName;

    //    string sDisplay = cam.GetCameraName();

    //    m_cboCameraList.options[idx].text = sDisplay;
    //    m_cboCameraList.RefreshShownValue();

    //    OnCameraListChanged?.Invoke();
    //}

    /// <summary>
    /// 현재 선택된 카메라를 삭제한다.
    /// 카메라가 1개 이하인 경우 삭제하지 않는다.
    /// 삭제 후 드롭다운 이름을 재정렬하고 인접한 항목을 선택한다.
    /// </summary>
    public void OnClick_Delete()
    {
        if (m_CamObjListUI == null) return;

        if (m_CamObjListUI.m_CamObjList.Count <= 1)
        {
            Debug.LogWarning("[CPCamAddUI] 카메라가 1개 이하이므로 삭제할 수 없습니다.");
            return;
        }

        int idx = m_cboCameraList.value;
        bool bRemoved = m_CamObjListUI.RemoveCameraAt(idx);
        if (!bRemoved) return;

        // 저장 데이터에서 해당 카메라 슬롯 제거
        CDataMgr.Inst.m_SaveCameraPosData.RemoveCameraPos(idx);

        m_cboCameraList.options.RemoveAt(idx);

        // ID 재정렬 후 드롭다운 표시 이름 갱신
        for (int i = 0; i < m_CamObjListUI.m_CamObjList.Count; i++)
        {
            CObjCamera cam = m_CamObjListUI.m_CamObjList[i];
            //string sDisplay = cam.m_PCamera != null
            //    ? cam.m_PCamera.GetCameraName()
            //    : cam.m_NameId;

            string sDisplay = cam.GetCameraName();
            if (i < m_cboCameraList.options.Count)
                m_cboCameraList.options[i].text = sDisplay;
        }

        int newIdx = Mathf.Clamp(idx, 0, m_cboCameraList.options.Count - 1);
        m_cboCameraList.value = newIdx;
        m_cboCameraList.RefreshShownValue();

        OnChanged_Camera(newIdx);
        OnCameraListChanged?.Invoke();
    }


    // Update is called once per frame
    void Update()
    {
        
    }
}
