using System;
using System.Collections;
using System.Collections.Generic;
using SFB;
using System.IO;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.UI;
using static CSaveInitCampPos;
using UnityEngine.Rendering;

// (추가 작업)
// 저장 데이터 기준으로 UI를 재구성 해야 된다.

public class CPCamControlDlg : CDialogUI, IBeginDragHandler, IDragHandler, IEndDragHandler
{
    [Serializable]
    public class SCamControl
    {
        public InputField m_inMin;
        public InputField m_inCur;
        public InputField m_inMax;

        public Slider m_Slider;
        public CObjCamera m_Camera = null;
        public Action<float> OnActionOnValueChanged = null;

        public void Init(float fMin, float fMax, float fCur, CObjCamera kCamera)
        {
            m_Camera = kCamera;

            m_inMin.text = fMin.ToString("F1");
            m_inMax.text = fMax.ToString("F1");
            m_inCur.text = fCur.ToString("F1");
            m_Slider.minValue = fMin;
            m_Slider.maxValue = fMax;
            m_Slider.value = fCur;
            m_inMin.onSubmit.AddListener(OnEndEdit_Min);
            m_inMax.onSubmit.AddListener(OnEndEdit_Max);
            m_inCur.onSubmit.AddListener(OnEndEdit_Cur);
            m_Slider.onValueChanged.AddListener(OnSliderValueChanged);
        }

        /// <summary>
        /// 슬라이더 범위와 InputField 텍스트를 동시에 갱신한다.
        /// 현재 값이 새 범위를 벗어나면 클램프한다.
        /// </summary>
        public void SetMinMax(float fMin, float fMax)
        {
            m_Slider.minValue = fMin;
            m_Slider.maxValue = fMax;
            m_inMin.text = fMin.ToString("F1");
            m_inMax.text = fMax.ToString("F1");

            if (m_Slider.value < fMin)      m_Slider.value = fMin;
            else if (m_Slider.value > fMax) m_Slider.value = fMax;
        }

        public float GetCurValue()
        {
            return m_Slider.value;
        }

        public float GetCurTextValue()
        {
            if (float.TryParse(m_inCur.text, out float fVal))
                return fVal;

            return m_Slider.value;
        }

        void OnEndEdit_Min(string strVal)
        {
            if (float.TryParse(strVal, out float fVal))
            {
                m_Slider.minValue = fVal;
                if (m_Slider.value < fVal)
                {
                    m_Slider.value = fVal;
                    m_inMin.text = m_Slider.value.ToString("F1");
                }
            }
        }

        void OnEndEdit_Max(string strVal)
        {
            if (float.TryParse(strVal, out float fVal))
            {
                m_Slider.maxValue = fVal;
                if (m_Slider.value > fVal)
                {
                    m_Slider.value = fVal;
                    m_inMax.text = m_Slider.value.ToString("F1");
                }
            }
        }

        void OnEndEdit_Cur(string strVal)
        {
            if (float.TryParse(strVal, out float fVal))
            {
                if (fVal < m_Slider.minValue)
                    fVal = m_Slider.minValue;
                else if (fVal > m_Slider.maxValue)
                    fVal = m_Slider.maxValue;
                m_Slider.value = fVal;
            }
            m_inCur.text = m_Slider.value.ToString("F1");
        }
        void OnSliderValueChanged(float fVal)
        {
            m_inCur.text = fVal.ToString("F1");
            OnActionOnValueChanged?.Invoke(fVal);
        }
    }

    public CPCamObjListUI m_CamObjListUI = null;  // 카메라 오브젝트 리스트 UI
    public CPoleObjListUI m_PoleObjListUI = null; // 폴대 오브젝트 리스트 UI
    public CPCamDistDlg m_CamDistDlg = null;       // 카메라 거리 측정 UI
    public CPCamAddUI m_CamAddUI = null;             // 카메라 추가 UI
    public bool m_UseAttachCamera = false;        // 초기에 카메라를 폴에 부착할지 여부
    [Space]
    public Text m_txtFileName;                     // 파일 이름 표시 텍스트

    [Space]
    public Dropdown m_cboPresetList = null;     // 프리셋 리스트 드롭다운
    public InputField m_InPresetID = null;      // 프리셋 아이디 표시 텍스트
    public Button m_btnAddPreset = null;       // 프리셋 추가 버튼
    public Button m_btnRepairPreset = null;     // 프리셋  수정 버튼
    public Button m_btnDeletePreset = null;     // 프리셋 삭제 버튼


    [Space]
    public SCamControl m_CamHeight;     // 카메라 높이
    public SCamControl m_CamX;          // 카메라 x 위치    
    public SCamControl m_CamZ;          // 카메라 z 위치
    public SCamControl m_CamPan;        // 카메라 팬 (좌우 회전)
    public SCamControl m_CamTilt;       // 카메라 틸트 (상하 회전)
    public SCamControl m_CamZoom;       // 카메라 줌 (FOV 구현 )

    [Header("Buttons")]
    public Button m_btnExit;            // 종료 버튼
    public Button m_btnSave;            // 저장 버튼
    public Button m_btnLoad;            // 불러오기 버튼
    public Button m_btnClear;           // 초기화 버튼
    public Button m_btnStartPicking;    // 위치 선택 시작 버튼 
    [Space]
    public Button m_btnShowCamPos;        // 폴대 보여주기/숨기기 버튼

    private CObjCamera m_CurCamera = null;  // 현재 선택된 카메라 오브젝트
    //private CObjPole m_CamPole = null;      // 카메라 폴대 오브젝트 ( 카메라 위치 )

    string m_DefaultFileName = "fileName";

    private bool m_bShowPole = false; // 폴대 보여주기 여부

    // SCamControl(Height/X/Z/Pan/Tilt/Zoom) InputField Tab 이동 순서.
    // OnEnable 에서 채워진다.
    List<InputField> m_TabOrder = null;

    private bool m_bInitOpen = true; // UI가 처음 열릴 때 초기화 플래그 (필요 시 사용)



    // 현재 선택된 카메라의 SCameraPos를 반환한다.
    SCameraPos GetCurrentCamPos()
    {
        int camIdx = m_CamAddUI.m_cboCameraList.value;
        return CDataMgr.Inst.m_SaveCameraPosData.GetCameraPos(camIdx+1);
    }


    // ── 폴대 선택 강조 ──
    // 마우스 좌클릭 피킹과 카메라 드롭다운 선택 모두 동일한 인스펙터 색상으로 폴대를 강조한다.
    [Header("폴대 선택 색상")]
    [SerializeField] private Color m_CamSelectColor = new Color(1.0f, 0.35f, 0.35f);   // 폴대 선택 강조색(인스펙터 조정)

    private CObjCamera m_SelectedPoleCamera = null;                                    // 현재 선택된 폴대를 소유한 카메라
    private readonly List<Renderer> m_SelectedPoleRenderers = new List<Renderer>();    // 선택 폴대의 렌더러 목록
    private readonly List<Color>    m_SelectedPoleOrgColors = new List<Color>();       // 선택 전 원본 색상 백업

    // Start is called once before the first execution of Update after the MonoBehaviour is created
    void Start()
    {
        m_btnExit.onClick.AddListener(OnClick_Exit);
        m_btnSave.onClick.AddListener(OnClick_Save);
        m_btnLoad.onClick.AddListener(OnClick_Load);
        m_btnClear.onClick.AddListener(OnClick_Clear);
        m_btnStartPicking.onClick.AddListener(OnClick_StartPicking);
        m_btnAddPreset.onClick.AddListener(OnClick_AddPreset);
        m_btnRepairPreset.onClick.AddListener(OnClick_RepairPreset);
        m_btnDeletePreset.onClick.AddListener(OnClick_DeletePreset);
        m_btnShowCamPos.onClick.AddListener(OnClicked_ShowPole);

        // 배타 피킹 모드 조정: CPCamDistDlg가 카메라 피킹을 강제 해제할 수 있도록 자신을 주입한다.
        if (m_CamDistDlg != null)
            m_CamDistDlg.m_CamControlDlg = this;

        Initiaize();

        Invoke("Init_DropdownCamera", 0.5f);
    }

    void Update()
    {
        CheckPickingPositionByMouse();
        CheckPickingPoleByMouse();
        HandleTabNavigation();
    }

    // ─────────────────────────────────────────────────────────────
    // SCamControl InputField Tab 네비게이션
    //   Height → X → Z → Pan → Tilt → Zoom 순으로,
    //   각 SCamControl 내부는 Min → Cur → Max 순서로 이동한다.
    // ─────────────────────────────────────────────────────────────

    void OnEnable()
    {
        if (m_TabOrder == null || m_TabOrder.Count == 0)
            InitTabOrder();

        if (isActiveAndEnabled)
            StartCoroutine(FocusDefaultFieldNextFrame());
    }

    void InitTabOrder()
    {
        m_TabOrder = new List<InputField>();
        // Preset Id (디폴트 첫 필드)
        m_TabOrder.Add(m_InPresetID);
        // SCamControl 들 (각각 Min → Cur → Max)
        AddCamControlFields(m_CamHeight);
        AddCamControlFields(m_CamX);
        AddCamControlFields(m_CamZ);
        AddCamControlFields(m_CamPan);
        AddCamControlFields(m_CamTilt);
        AddCamControlFields(m_CamZoom);
    }

    void AddCamControlFields(SCamControl kCtrl)
    {
        if (kCtrl == null) return;
        m_TabOrder.Add(kCtrl.m_inMin);
        m_TabOrder.Add(kCtrl.m_inCur);
        m_TabOrder.Add(kCtrl.m_inMax);
    }

    IEnumerator FocusDefaultFieldNextFrame()
    {
        // UI 활성화 직후 1프레임 대기 후 포커스 설정 (EventSystem 안정화)
        yield return null;
        InputField first = GetFirstValidField();
        if (first != null)
            FocusInputField(first);
    }

    InputField GetFirstValidField()
    {
        if (m_TabOrder == null) return null;
        for (int i = 0; i < m_TabOrder.Count; i++)
            if (m_TabOrder[i] != null) return m_TabOrder[i];
        return null;
    }

    void HandleTabNavigation()
    {
        if (!Input.GetKeyDown(KeyCode.Tab)) return;
        if (m_TabOrder == null || m_TabOrder.Count == 0) return;

        bool reverse = Input.GetKey(KeyCode.LeftShift) || Input.GetKey(KeyCode.RightShift);
        int count = m_TabOrder.Count;
        int step  = reverse ? -1 : 1;

        GameObject selected = EventSystem.current != null
                            ? EventSystem.current.currentSelectedGameObject
                            : null;

        // 현재 포커스가 m_TabOrder 안의 InputField 가 아니면 Tab 처리 안 함.
        int currentIdx = -1;
        if (selected != null)
        {
            for (int i = 0; i < count; i++)
            {
                if (m_TabOrder[i] != null && m_TabOrder[i].gameObject == selected)
                {
                    currentIdx = i;
                    break;
                }
            }
        }
        if (currentIdx < 0) return;

        int nextIdx = (currentIdx + step + count) % count;

        // 인스펙터에서 비어있을 수 있는 슬롯은 건너뛴다
        for (int tries = 0; tries < count; tries++)
        {
            InputField target = m_TabOrder[nextIdx];
            if (target != null)
            {
                FocusInputField(target);
                return;
            }
            nextIdx = (nextIdx + step + count) % count;
        }
    }

    void FocusInputField(InputField field)
    {
        if (field == null || EventSystem.current == null) return;
        EventSystem.current.SetSelectedGameObject(field.gameObject);
        field.ActivateInputField();
        field.MoveTextEnd(false); // 커서를 텍스트 끝으로
    }

    void Init_DropdownCamera()
    {
        m_CamAddUI.Initialize(m_CamObjListUI);
        m_CamAddUI.OnCallbackChangedCamera = (kCamera) =>
        {
            m_CurCamera = kCamera;
            SetUI_Camera(m_CurCamera);
            // 새 카메라의 프리셋 드롭다운 재구성 + 저장 데이터(위치/회전/줌/min/max)로 UI 및 CObjCamera 갱신
            Init_DropdownPreset();
            m_CamDistDlg.SetCurCarmera(m_CurCamera);
            // 드롭다운에서 선택된 카메라의 실제 폴대를 강조 (폴대가 표시 중일 때만 시각적 효과)
            SelectPole(m_CurCamera);
        };

        // 카메라 추가/삭제 후 CPCamControlDlg의 m_cboCameraList 동기화
        // (m_cboCameraList 가 m_CamAddUI.m_cboCameraList 와 다른 참조일 때만 실제로 동기화됨)
        m_CamAddUI.OnCameraListChanged = () =>
        {

        };

        // 신규 카메라 생성 시 UI 컨트롤 전체를 초기값으로 리셋
        m_CamAddUI.OnCameraAdded = (kCamera) =>
        {
            ResetUI_ToNewCamera();
        };

        m_CurCamera = m_CamObjListUI.GetObjCamera(0);
        m_InPresetID.text = "1";
    }

    /// <summary>
    /// 새 카메라 생성 시 슬라이더/입력 필드를 CObjCamera 초기값으로 리셋한다.
    /// 슬라이더 값 변경 → OnActionOnValueChanged → 카메라 위치/회전/줌도 동시에 초기화된다.
    /// </summary>
    void ResetUI_ToNewCamera()
    {
        m_CamHeight.m_Slider.value = CObjCamera.DEFAULT_CAM_HEIGHT; // 5.0f
        m_CamX.m_Slider.value      = 0f;
        m_CamZ.m_Slider.value      = 0f;
        m_CamPan.m_Slider.value    = 0f;
        m_CamTilt.m_Slider.value   = 0f;
        m_CamZoom.m_Slider.value   = 1f;

        m_CamDistDlg.PrintDistanceToCamera(CObjCamera.DEFAULT_CAM_HEIGHT);
    }

    void Init_DropdownPreset()
    {
        var currentCamPos = GetCurrentCamPos();
        int nCount = currentCamPos.GetCount();
        if(nCount == 0) {
            // 프리셋이 하나도 없는 경우 기본 프리셋 추가
            CSaveInitCampPos.SCamDir defaultPreset = new CSaveInitCampPos.SCamDir("Preset 1");
            currentCamPos.Add(defaultPreset);
        }

        m_cboPresetList.ClearOptions();
        var options = new System.Collections.Generic.List<Dropdown.OptionData>();
        int i = 0;
        foreach (var preset in currentCamPos.datas)
        {
            options.Add(new Dropdown.OptionData(preset.sname));
            i++;
        }
        m_cboPresetList.AddOptions(options);
        m_cboPresetList.onValueChanged.RemoveListener(OnValueChanged_Preset); // 중복 리스너 방지
        m_cboPresetList.onValueChanged.AddListener(OnValueChanged_Preset);
        m_cboPresetList.value = 0;
        OnChanged_Preset(0);

        SCamDir kCamDir = currentCamPos.Get(0);
        if (kCamDir != null) {
            if (kCamDir.preset_id == 0)
                kCamDir.preset_id = 1;
        }
        m_InPresetID.text = kCamDir.preset_id.ToString();
    }

    // 계획 (의사코드 - 자세히 설명)
    // 1. 카메라 줌 슬라이더의 값(fVal)을 이용해 실제 카메라(자식 노드)의 위치를 조정.
    // 2. '현재 카메라가 바라보고 있는 방향'은 자식 카메라의 transform.forward(월드 기준)로 계산.
    // 3. 카메라를 부모(m_CurCamera)의 위치에서 자식 카메라의 forward 방향으로 fVal 만큼 이동시킨다.
    //    - 월드 좌표로 위치를 계산: targetPosition = parent.position + cam.forward * fVal
    //    - 자식 트랜스폼이 부모의 자식이라면 cam.position = targetPosition으로 설정하면 내부적으로 localPosition이 갱신됨.
    //    - 안전 체크: m_CurCamera와 m_CurCamera.m_PCamera가 null이 아닌지 확인.
    // 4. 적용: Initiaize() 메서드 내부의 m_CamZoom.OnActionOnValueChanged 대리자 블록을 대체.
    // 5. 부가: 원하는 경우 localPosition으로 직접 설정하려면 parent.InverseTransformPoint(targetPosition)를 사용.
    //
    // 아래는 Initiaize() 메서드의 수정된 블록입니다. 기존 다른 설정은 그대로 유지합니다.

    public void Initiaize()
    {
        m_CamHeight.Init(3.0f, 15.0f, 5.0f, m_CurCamera);
        m_CamX.Init(-50.0f, 50.0f, 0.0f, m_CurCamera);
        m_CamZ.Init(-50.0f, 50.0f, 0.0f, m_CurCamera);
        m_CamPan.Init(-180.0f, 180.0f, 0.0f, m_CurCamera);
        m_CamTilt.Init(-90.0f, 90.0f, 0.0f, m_CurCamera);
        m_CamZoom.Init(1f, 36.0f, 1f, m_CurCamera);


        m_CamHeight.OnActionOnValueChanged = (fVal) =>
        {
            if (m_CurCamera != null)
            {
                Vector3 vPos = m_CurCamera.transform.position;  // 카메라 높이가 월드 높이 이어야 한다. 폴대의 자식오브젝트이므로 local로 하면 안됨
                vPos.y = fVal;
                m_CurCamera.transform.position = vPos;
                
                //string sH = m_CamHeight.m_inCur.text;
                //float fHeight = sH == "" ? 0 : float.Parse(sH);
                float fHeight = m_CamHeight.GetCurTextValue();
                m_CamDistDlg.SetCameraHeight(fHeight);
            }
        };

        m_CamX.OnActionOnValueChanged = (fVal) =>
        {
            if (m_CurCamera != null)
            {
                Vector3 vPos = m_CurCamera.transform.localPosition;
                vPos.x = fVal;
                m_CurCamera.transform.localPosition = vPos;
                m_CurCamera.UpdatePolePosition();

                float fHeight = m_CamHeight.GetCurTextValue();
                m_CamDistDlg.SetCameraHeight(fHeight);
            }
        };

        m_CamZ.OnActionOnValueChanged = (fVal) =>
        {
            if (m_CurCamera != null)
            {
                Vector3 vPos = m_CurCamera.transform.localPosition;
                vPos.z = fVal;
                m_CurCamera.transform.localPosition = vPos;
                m_CurCamera.UpdatePolePosition();
                
                float fHeight = m_CamHeight.GetCurTextValue();
                m_CamDistDlg.SetCameraHeight(fHeight);
            }
        };

        m_CamPan.OnActionOnValueChanged = (fVal) =>
        {
            if (m_CurCamera != null)
            {
                Vector3 vRot = m_CurCamera.getCamera.transform.localEulerAngles;
                vRot.y = fVal;
                m_CurCamera.SetPenTilt(vRot.y, vRot.x);

                float fHeight = m_CamHeight.GetCurTextValue();
                m_CamDistDlg.SetCameraHeight(fHeight);
            }
        };
        m_CamTilt.OnActionOnValueChanged = (fVal) =>
        {
            if (m_CurCamera != null)
            {
                Vector3 vRot = m_CurCamera.getCamera.transform.localEulerAngles;
                vRot.x = fVal;
                m_CurCamera.SetPenTilt(vRot.y, vRot.x);
                //m_CurCamera.getCamera.transform.localEulerAngles = vRot;
                float fHeight = m_CamHeight.GetCurTextValue();
                m_CamDistDlg.SetCameraHeight(fHeight);
            }
        };

        // 카메라 줌 슬라이더 값 변경 시 호출되는 대리자
        m_CamZoom.OnActionOnValueChanged = (fVal) =>
        {
            if (m_CurCamera != null && m_CurCamera.m_PCamera != null)
            {
                //float fMax = CObjCamera.DMAX_ZOOM; // 최대 줌 배율 (휴컴스 카메라 기준 36배줌 )
                float fMax = 36; // 디폴트 최대값
                float.TryParse(m_CamZoom.m_inMax.text, out fMax);
                 
                //float zoom = Mathf.Min(36.0f, fMax - (fVal - 1.0f) );// + 1.0f; // 슬라이더 값이 1~36 이므로, 이를 36~1로 변환 => 함수안에서 변환함.
                m_CurCamera.SetZoomByFOV(fVal, fMax); // 최대 36배 줌 기준으로 FOV 설정

                float zoom = m_CurCamera.zoom;
                Debug.Log( $"fval = {fVal}, zoom = {zoom}" );

                float fHeight = m_CamHeight.GetCurTextValue();
                m_CamDistDlg.SetCameraHeight(fHeight);
                //SetZoomByCamZ(fVal);
            }
        };

    }


    // 카메라 줌 ( 카메라 z축 이동 )  
    public void SetZoomByCamZ(float fValue)
    {
        if (m_CurCamera != null && m_CurCamera.m_PCamera != null)
        {
            // 자식 카메라 트랜스폼 (실제 카메라 컴포넌트가 붙어있는 오브젝트)
            Transform camTr = m_CurCamera.m_PCamera.transform;

            // 카메라가 '현재 바라보고 있는 방향'을 월드 기준 forward로 가져옴
            Vector3 lookDir = camTr.forward;
            if (lookDir.sqrMagnitude <= 0.0001f)
            {
                // 예외적으로 forward가 유효하지 않다면 부모의 forward를 사용
                lookDir = m_CurCamera.transform.forward;
            }
            lookDir.Normalize();

            // 부모(폴대) 위치에서 카메라가 바라보는 방향으로 fVal 만큼 이동
            Vector3 targetWorldPos = m_CurCamera.transform.position + lookDir * fValue;

            // 카메라가 부모의 자식이라면 월드 위치를 설정해도 localPosition이 자동으로 계산됨.
            camTr.position = targetWorldPos;

            // 만약 명시적으로 localPosition을 설정하고 싶다면 아래처럼 할 수도 있음:
            // camTr.localPosition = m_CurCamera.transform.InverseTransformPoint(targetWorldPos);
        }
    }

    public override void OpenUI()
    {
        base.OpenUI();

        if (m_bInitOpen)
        {
            m_bInitOpen = false;
            Init_DropdownCamera();
            Init_DropdownPreset();
        }
        m_CamDistDlg.OpenUI();
        m_CamObjListUI.m_CurCamViewerUI?.Show(true);
        
    }

    public override void CloseUI()
    {
        base.CloseUI();
        CDataMgr.Inst.SetNormalState();
        m_CamDistDlg.CloseUI(); 
    }

    void OnValueChanged_Preset(int iNo)
    {
        OnChanged_Preset(iNo);
    }
    void OnChanged_Preset(int iNo)
    {
        if (iNo < 0 || iNo >= GetCurrentCamPos().datas.Count)
            return;

        SCamDir kCamPos = FromSaveData(iNo);

        //// 프리셋 데이터에서 카메라 위치/회전/줌 정보 가져오기
        SetCurCamera(kCamPos);

        m_CamDistDlg.PrintDistanceToCamera(m_CamHeight.GetCurTextValue());
    }

    // 카메라 위치/회전/줌 정보로 카메라 설정
    void SetCurCamera(SCamDir kCamPos)
    {
        // 카메라 위치 정보로 카메라 설정
        Vector3 vPos = m_CurCamera.transform.position;  // 카메라 높이가 월드 높이 이어야 한다. 폴대의 자식오브젝트이므로 local로 하면 안됨
        vPos.y = kCamPos.pos.y;
        m_CurCamera.transform.position = vPos;

        // 카메라 x,z 위치는 local로 설정
        Vector3 vLocalPos = m_CurCamera.transform.localPosition;
        vLocalPos.x = kCamPos.pos.x;
        vLocalPos.z = kCamPos.pos.z;
        m_CurCamera.transform.localPosition = vLocalPos;

        // 카메라 위치가 바뀌었으므로 폴대 위치 동기화
        m_CurCamera.UpdatePolePosition();

        // 카메라 회전/줌 정보로 카메라 설정
        m_CurCamera.pan = kCamPos.Pan();
        m_CurCamera.tilt = kCamPos.Tilt();
        m_CurCamera.zoom = kCamPos.zoom;
    }


    void SetUI_Camera(CObjCamera kObjCamera)
    {
        m_CamHeight.m_Camera = kObjCamera;
        m_CamX.m_Camera = kObjCamera;
        m_CamZ.m_Camera = kObjCamera;
        m_CamPan.m_Camera = kObjCamera;
        m_CamTilt.m_Camera = kObjCamera;
        m_CamZoom.m_Camera = kObjCamera;
    }


    public void OnClick_AddPreset()
    {
        var currentCamPos = GetCurrentCamPos();
        int count = currentCamPos.datas.Count + 1;
        SCamDir kCamDir = new SCamDir($"Preset {count}");
        kCamDir.preset_id = count;
        currentCamPos.Add(kCamDir);

        ToSaveData(kCamDir, count-1, false );

        //dropdown 프리셋 리스트 업데이트
        Dropdown.OptionData option = new Dropdown.OptionData(kCamDir.sname);
        m_cboPresetList.options.Add(option);
        m_cboPresetList.value = count - 1;

        int newPresetId = currentCamPos.datas.Count; // 새 프리셋 ID는 현재 개수
        m_InPresetID.text = newPresetId.ToString();

         if( m_cboPresetList.value == 0)
            OnChanged_Preset(0);

        m_bInitOpen = false;


    }

    public void OnClick_RepairPreset()
    {
        int idx = m_cboPresetList.value;
        var currentCamPos = GetCurrentCamPos();
        if (idx < 0 || idx >= currentCamPos.datas.Count)
            return;

        SCamDir kCamDir = currentCamPos.Get(idx);
        ToSaveData(kCamDir, idx, true);
        
        // 프리셋 이름 업데이트
        m_cboPresetList.options[idx].text = kCamDir.sname;
        m_cboPresetList.RefreshShownValue();

        SetCurCamera(kCamDir);
        m_bInitOpen = false;
    }

    public void OnClick_DeletePreset()
    {
        int idx = m_cboPresetList.value;
        var currentCamPos = GetCurrentCamPos();
        if (idx < 0 || idx >= currentCamPos.datas.Count)
            return;

        // 프리셋 데이터에서 삭제
        currentCamPos.datas.RemoveAt(idx);
        // 드롭다운 옵션에서 삭제
        m_cboPresetList.options.RemoveAt(idx);
        m_cboPresetList.RefreshShownValue();

        // 프리셋 ID 업데이트
        for (int i = 0; i < currentCamPos.datas.Count; i++)
        {
            currentCamPos.datas[i].preset_id = i + 1;
            m_cboPresetList.options[i].text = currentCamPos.datas[i].sname;
        }

        int prv_idx = idx - 1;
        m_cboPresetList.value = prv_idx;
        if( prv_idx <= 0) {
            m_InPresetID.text = "0";
        }

        // if(m_cboPresetList.options.Count == 0)
        //     m_cboPresetList.value = -1; // 프리셋이 하나도 없는 경우 드롭다운 선택 해제

        m_bInitOpen = false;
    }



    void OnClick_Exit()
    {
        CloseUI();
        m_CamDistDlg.CloseUI();
    }

    bool m_bStartPicking = false;

    void OnClick_StartPicking()
    {
        // 위치 선택 모드 토글 (켜기/끄기) - "위치 피킹 중 ..." 글씨변경(빨강색)     
        // 위치 선택 모드에서는 마우스 클릭으로 카메라 위치를 지정할 수 있다.
        // 폴대 오브젝트의 위치도 마우스 클릭위치로 변경된다. (카메라가 폴대의 자식오브젝트이므로 카메라 위치도 함께 변경됨)
        // 
        // 위치 선택 모드가 켜지면 안내 메시지를 표시.
        if ( m_bStartPicking)
        {
            m_bStartPicking = false;
            CDataMgr.Inst.SetNormalState(); // 위치 선택 모드 끄기
        }
        else
        {
            // 배타 모드: 타겟점/타겟라인 피킹을 끈다.
            if (m_CamDistDlg != null)
            {
                m_CamDistDlg.ForceStopTargetPoint();
                m_CamDistDlg.ForceStopTargetLine();
            }

            m_bStartPicking = true;
            CDataMgr.Inst.SetCamObjPlacementState(); // 위치 선택 모드 켜기
        }
    }

    /// <summary>
    /// 외부(CPCamDistDlg)에서 카메라 위치 피킹을 강제로 끈다. (배타 피킹 모드 보장)
    /// 이미 꺼져 있으면 아무 동작도 하지 않는다.
    /// </summary>
    public void ForceStopCameraPicking()
    {
        if (!m_bStartPicking) return;
        m_bStartPicking = false;
        CDataMgr.Inst.SetNormalState(); // FSM Normal 진입 → Exit_CameraPicking()으로 버튼 UI 원복
    }

    public void Enter_CameraPicking()
    {
        Text kText = m_btnStartPicking.GetComponentInChildren<Text>();
        kText.text = "위치 피킹 중...";
        kText.color = Color.white;
        m_btnStartPicking.GetComponent<Image>().color = Color.red;
    }

    public void Exit_CameraPicking()
    {
        Text kText = m_btnStartPicking.GetComponentInChildren<Text>();
        kText.text = "위치 피킹 시작";
        kText.color = Color.white;

        m_btnStartPicking.GetComponent<Image>().color = Color.royalBlue;
    }


    void OnClick_Save()
    {
        CDataMgr.Inst.m_SaveCameraPosData.m_CamPosList.ResetPanTilt();
        //ToSaveData();

        //string sFileName = m_editPathName.text;
        //CDataMgr.Inst.SaveCameraPosFile(CDataMgr.DSAVE_PATH_CAMERAPOS, sFileName);
        string sFilePath = StandaloneFileSave_Json();
        if (sFilePath.Length > 0)
        {
            string sFileName = Path.GetFileName(sFilePath);
            m_DefaultFileName = sFileName;
            //ToSaveData();
            CDataMgr.Inst.SaveCameraPosFile("", sFilePath);
        }
    }

    void OnClick_Load()
    {
        string sFilePath = StandaloneFileOpen_Json();
        if (sFilePath.Length > 0)
        {
            CDataMgr.Inst.LoadCameraPosFile_FullPathName(sFilePath);

            // 파일의 카메라 수에 맞게 씬 카메라 오브젝트 동기화
            SyncSceneCamerasToSaveData();

            // 카메라 드롭다운 재구성 (씬 카메라 기준)
            m_CamAddUI.Init_DropdownCamera();
            // CPCamControlDlg.m_cboCameraList 가 별도 위젯일 때 동기화
            m_CamAddUI.OnCameraListChanged?.Invoke();

            // 첫 번째 카메라 기준 UI 갱신
            FromSaveData(0, bUpdateCamera: true);
            Init_DropdownPreset();

            string sFileName = Path.GetFileName(sFilePath);
            m_DefaultFileName = sFileName;
            if( m_txtFileName != null)
                m_txtFileName.text = sFileName;

            m_bInitOpen = false;
        }
    }

    void OnClick_Clear()
    {
        m_bInitOpen = true; // UI가 처음 열릴 때 초기화 플래그 재설정
        CDataMgr.Inst.m_SaveCameraPosData.Clear();
        SyncSceneCamerasToSaveData();
        m_CamAddUI.Init_DropdownCamera();
        FromSaveData(0, bUpdateCamera: true);
        Init_DropdownPreset();
        m_DefaultFileName = "fileName";
        if (m_txtFileName != null)
            m_txtFileName.text = m_DefaultFileName;
    }

    /// <summary>
    /// 로드된 저장 데이터의 카메라 수에 맞게 씬의 CObjCamera 오브젝트를 추가/제거하고
    /// 각 카메라에 저장된 첫 번째 프리셋 데이터를 적용한다.
    /// </summary>
    void SyncSceneCamerasToSaveData()
    {
        int fileCount = CDataMgr.Inst.m_SaveCameraPosData.m_CamPosList.GetCount();

        // 부족한 카메라 추가
        while (m_CamObjListUI.m_CamObjList.Count < fileCount)
        {
            int newIdx = m_CamObjListUI.m_CamObjList.Count + 1;
            CObjCamera newCam = m_CamObjListUI.SetCameraObjectAndRenderTexture(
                CBaseCamera.ECameraType.PTZ, $"PTZCamera-{newIdx}");
            if (newCam == null) break;

            if (m_CamObjListUI.m_PolePrefab != null)
                newCam.CreatePole(m_CamObjListUI.m_PolePrefab);
        }

        // 초과 카메라 제거 (최소 1개 유지)
        while (m_CamObjListUI.m_CamObjList.Count > fileCount && m_CamObjListUI.m_CamObjList.Count > 1)
            m_CamObjListUI.RemoveCameraAt(m_CamObjListUI.m_CamObjList.Count - 1);

        // 각 카메라에 저장된 첫 번째 프리셋 위치/회전/줌 적용
        for (int i = 0; i < m_CamObjListUI.m_CamObjList.Count; i++)
        {
            CObjCamera kCam = m_CamObjListUI.m_CamObjList[i];
            var kCamDir = CDataMgr.Inst.m_SaveCameraPosData.GetCameraPos(i+1).Get(0);
            if (kCamDir != null)
                m_CamObjListUI.SetCurCamera(kCam, kCamDir);
        }
    }


    void ToSaveData(SCamDir kCamDir, int index, bool bForcUpdatePresetId = false)
    {
        //CSaveInitCampPos kSaveData = CDataMgr.Inst.m_SaveCameraPosData;
        float fHeight = float.Parse(m_CamHeight.m_inCur.text);
        float fX = float.Parse(m_CamX.m_inCur.text);
        float fZ = float.Parse(m_CamZ.m_inCur.text);
        float fPan = float.Parse(m_CamPan.m_inCur.text);
        float fTilt = float.Parse(m_CamTilt.m_inCur.text);
        float fZoom = float.Parse(m_CamZoom.m_inCur.text);
        float fMinPan = 0, fMaxPan = 0, fMinTilt = 0, fMaxTilt = 0, fMinZoom = 0, fMaxZoom = 0;
        float.TryParse(m_CamZoom.m_inMin.text, out fMinZoom);
        float.TryParse(m_CamZoom.m_inMax.text, out fMaxZoom); 
        float.TryParse(m_CamPan.m_inMin.text, out fMinPan);
        float.TryParse(m_CamPan.m_inMax.text, out fMaxPan);
        float.TryParse(m_CamTilt.m_inMin.text, out fMinTilt);
        float.TryParse(m_CamTilt.m_inMax.text, out fMaxTilt);

        if (bForcUpdatePresetId)
        {
            kCamDir.preset_id = int.Parse(m_InPresetID.text);
        }

        kCamDir.idx = index;
        kCamDir.cam_id = m_CamAddUI.m_cboCameraList.value+1; // 현재 선택된 카메라 인덱스
        kCamDir.sname = $"Preset {kCamDir.preset_id}";     // m_CurCamera.m_PCamera.GetCameraName(idx);
        Vector3 vPos = m_CurCamera.transform.position;
        Vector3 vRot = m_CurCamera.transform.eulerAngles;
        kCamDir.Pos(new Vector3(fX, fHeight, fZ));
        kCamDir.Rot(new Vector3(fTilt, fPan, 0.0f));
        kCamDir.pan = fPan;
        kCamDir.tilt = fTilt;
        kCamDir.zoom = fZoom;
        kCamDir.ptzmin.Set(fMinPan, fMinTilt, fMinZoom);
        kCamDir.ptzmax.Set(fMaxPan, fMaxTilt, fMaxZoom);

    }

    SCamDir FromSaveData(int nCamDirIdx = 0, bool bUpdateCamera = false)
    {
        int idx = nCamDirIdx;
        SCamDir kCamDir = null;
        kCamDir = GetCurrentCamPos().Get(idx);

        m_InPresetID.text = kCamDir.preset_id.ToString();
        m_cboPresetList.value = idx;

        // 파일 로드 시에만 카메라 드롭다운을 프리셋의 cam_id로 동기화한다.
        if (bUpdateCamera)
            m_CamAddUI.m_cboCameraList.value = ( kCamDir.cam_id - 1) < 0 ? 0 : kCamDir.cam_id - 1;

        // min/max를 먼저 갱신해야 슬라이더 value 설정 시 올바른 범위로 클램프된다.
        m_CamPan.SetMinMax(kCamDir.ptzmin.p, kCamDir.ptzmax.p);
        m_CamTilt.SetMinMax(kCamDir.ptzmin.t, kCamDir.ptzmax.t);
        // 구버전 저장 파일에서 ptzmax.z 기본값이 360으로 잘못 저장된 경우를 보정한다.
        const float MAX_ZOOM = 36.0f;
        float zoomMax = (kCamDir.ptzmax.z <= 0f || kCamDir.ptzmax.z > MAX_ZOOM)
            ? MAX_ZOOM
            : kCamDir.ptzmax.z;
        m_CamZoom.SetMinMax(kCamDir.ptzmin.z, zoomMax);

        m_CamHeight.m_Slider.value = kCamDir.pos.y;
        m_CamX.m_Slider.value      = kCamDir.pos.x;
        m_CamZ.m_Slider.value      = kCamDir.pos.z;
        m_CamPan.m_Slider.value = kCamDir.Pan();// kCamDir.rot.y;
        m_CamTilt.m_Slider.value = kCamDir.Tilt();// kCamDir.rot.x;
        m_CamZoom.m_Slider.value   = kCamDir.zoom;

        return kCamDir;
    }

    public string StandaloneFileOpen_Json()
    {
        string baseDir = CDataMgr.DSAVE_PATH_CAMERAPOS;

        return CMyUtil.StandaloneFileOpen(baseDir, "Json Files", "json");
    }

    public string StandaloneFileSave_Json()
    {
        string baseDir = CDataMgr.DSAVE_PATH_CAMERAPOS;
        string sDefaultFileName = m_DefaultFileName;// m_editPathName.text;

        return CMyUtil.StandaloneFileSave(baseDir, sDefaultFileName, "Json Files", "json");
    }


    /// <summary>
    /// 전체 카메라의 폴대를 일괄 보여주기/숨기기 토글.
    /// 하나라도 폴대가 활성화 상태이면 전체 숨기기, 모두 비활성이면 전체 보여주기.
    /// 폴대가 없는 카메라는 CPCamObjListUI.m_PolePrefab 으로 생성 후 표시한다.
    /// </summary>
    void OnClicked_ShowPole()
    {
        if (m_CamObjListUI == null) return;

        var camList = m_CamObjListUI.m_CamObjList;
        if (camList == null || camList.Count == 0) return;

        //// 하나라도 보이면 → 전체 숨기기 / 모두 숨겨져 있으면 → 전체 보여주기
        //bool bAnyVisible = camList.Exists(c => c != null && c.m_goPole != null && c.m_goPole.activeSelf);
        //bool bNextVisible = !bAnyVisible;

        m_bShowPole = !m_bShowPole;

        GameObject polePrefab = m_CamObjListUI.m_PolePrefab;

        foreach (CObjCamera cam in camList)
        {
            if (cam == null) continue;

            if (m_bShowPole)
            {
                if (cam.m_goPole == null)
                {
                    if (polePrefab == null)
                    {
                        Debug.LogWarning("[CPCamControlDlg] CPCamObjListUI.m_PolePrefab이 설정되지 않았습니다.");
                        continue;
                    }
                    cam.CreatePole(polePrefab);
                }
                else
                {
                    // 카메라가 이동되었을 수 있으므로 위치 먼저 갱신
                    cam.UpdatePolePosition();
                }
                cam.ShowPole(true);
            }
            else
            {
                cam.ShowPole(false);
            }
        }

        SetPoleButtonUI(m_bShowPole);
    }

    void SetPoleButtonUI(bool bVisible)
    {
        if (m_btnShowCamPos == null) return;
        Text kText = m_btnShowCamPos.GetComponentInChildren<Text>();
        if (kText == null) return;
        kText.text = bVisible ? "폴대 숨기기" : "폴대 보여주기";
        kText.color = bVisible ? Color.red : Color.black;
    }
    

    public LayerMask m_SelectableLayer; // 선택할 오브젝트가 속한 레이어

    public void CheckPickingPositionByMouse()
    {
        // ── 배타 피킹: 모든 피킹은 "Ctrl + 좌클릭"으로 통일하고 UI 위 클릭은 무시한다. ──
        //    (카메라위치/타겟점/타겟라인은 한 번에 하나만 활성화되므로 동시 발동이 없다.)
        if (!Input.GetKey(KeyCode.LeftControl)) return;
        if (!Input.GetMouseButtonDown(0)) return;
        if (EventSystem.current != null && EventSystem.current.IsPointerOverGameObject()) return;
        if (Camera.main == null) return;

        // 타겟점 이동 모드
        if (m_CamObjListUI.IsCreateTargetPoint())
        {
            Ray ray = Camera.main.ScreenPointToRay(Input.mousePosition);
            if (Physics.Raycast(ray, out RaycastHit hit, Mathf.Infinity, m_SelectableLayer)
                && hit.collider.CompareTag("Floor"))
            {
                m_CamObjListUI.m_TargetPosObj.transform.position = hit.point;
                m_CamDistDlg.PrintDistanceToCamera(m_CamHeight.GetCurTextValue());
            }
            return;
        }

        // 카메라 위치 피킹 모드
        if (m_bStartPicking)
        {
            Ray ray = Camera.main.ScreenPointToRay(Input.mousePosition);
            if (Physics.Raycast(ray, out RaycastHit hit, Mathf.Infinity, m_SelectableLayer)
                && hit.collider.CompareTag("Floor"))
            {
                SetCameraPositionByPicking(hit.point);
            }
        }
    }

    /// <summary>
    /// 위치 피킹 모드에서 마우스 클릭 hit 지점을 카메라/폴대 위치에 반영한다.
    /// 카메라 부모 좌표계로 변환한 X,Z 를 슬라이더에 설정하면
    /// 기존 OnActionOnValueChanged 콜백이 카메라 localPosition 갱신과
    /// UpdatePolePosition()(폴대 동기화)을 수행한다.
    /// 슬라이더의 min/max 범위를 벗어나면 자동 클램프된다.
    /// </summary>
    void SetCameraPositionByPicking(Vector3 vWorldHit)
    {
        if (m_CurCamera == null) return;

        Transform parent = m_CurCamera.transform.parent;
        float fHeight = m_CamHeight.GetCurTextValue();
        Vector3 vWorldTarget = new Vector3(vWorldHit.x, fHeight, vWorldHit.z);
        Vector3 vLocalTarget = (parent != null)
            ? parent.InverseTransformPoint(vWorldTarget)
            : vWorldTarget;

        // 슬라이더 값 갱신 → 콜백이 카메라 localPosition 변경 + 폴대 위치 동기화
        m_CamX.m_Slider.value = vLocalTarget.x;
        m_CamZ.m_Slider.value = vLocalTarget.z;
    }

    /// <summary>
    /// 일반 좌클릭(Ctrl 미사용)으로 폴대(CObjCamera.m_goPole)를 피킹 선택한다.
    /// 선택된 폴대는 하늘색 계열로 강조하고, 이전 선택은 원래 색으로 복원한다.
    /// UI 위 클릭과 Ctrl 조합(위치/타겟 피킹 모드)일 때는 무시한다.
    /// 폴대 콜라이더는 "Pole" 태그를 가지며, hit 콜라이더에서 소유 카메라를 역추적한다.
    /// </summary>
    void CheckPickingPoleByMouse()
    {
        if (Input.GetKey(KeyCode.LeftControl) || Input.GetKey(KeyCode.RightControl)) return; // Ctrl+클릭은 위치/타겟 피킹 전용
        if (!Input.GetMouseButtonDown(0)) return;
        if (EventSystem.current != null && EventSystem.current.IsPointerOverGameObject()) return;
        if (Camera.main == null) return;

        Ray ray = Camera.main.ScreenPointToRay(Input.mousePosition);
        if (!Physics.Raycast(ray, out RaycastHit hit, Mathf.Infinity)) return;
        if (!hit.collider.CompareTag("Pole")) return;

        CObjCamera kOwner = FindPoleOwner(hit.collider.transform);
        if (kOwner != null)
        {
            // 클릭한 폴대의 소유 카메라를 카메라 드롭다운에서도 함께 선택한다.
            // value 변경 시 OnValueChanged_Camera → OnCallbackChangedCamera 가 호출되어
            // m_CurCamera·UI·폴대 강조가 갱신된다.
            SyncCameraDropdownTo(kOwner);
            // 동일 카메라 재클릭 등으로 드롭다운 콜백이 발생하지 않는 경우에도 강조를 보장.
            SelectPole(kOwner);
        }
    }

    /// <summary>
    /// 클릭으로 선택된 폴대의 소유 카메라를 카메라 드롭다운(m_CamAddUI.m_cboCameraList)에서도
    /// 선택되도록 value 를 동기화한다. 드롭다운 인덱스는 m_CamObjListUI.m_CamObjList 순서와 일치한다.
    /// </summary>
    void SyncCameraDropdownTo(CObjCamera kCamera)
    {
        if (kCamera == null) return;
        if (m_CamAddUI == null || m_CamAddUI.m_cboCameraList == null) return;
        if (m_CamObjListUI == null || m_CamObjListUI.m_CamObjList == null) return;

        int idx = m_CamObjListUI.m_CamObjList.IndexOf(kCamera);
        if (idx < 0) return;

        if (m_CamAddUI.m_cboCameraList.value != idx)
            m_CamAddUI.m_cboCameraList.value = idx; // onValueChanged 발화 → 카메라 선택 동기화
    }

    /// <summary>
    /// hit 콜라이더의 트랜스폼이 어느 카메라의 m_goPole(또는 그 자식)인지 역추적한다.
    /// </summary>
    CObjCamera FindPoleOwner(Transform hitTr)
    {
        if (m_CamObjListUI == null) return null;
        var camList = m_CamObjListUI.m_CamObjList;
        if (camList == null) return null;

        foreach (CObjCamera cam in camList)
        {
            if (cam == null || cam.m_goPole == null) continue;
            Transform poleTr = cam.m_goPole.transform;
            if (hitTr == poleTr || hitTr.IsChildOf(poleTr))
                return cam;
        }
        return null;
    }

    /// <summary>
    /// 폴대를 선택 상태로 만든다. 이전 선택은 원래 색으로 복원하고,
    /// 새 폴대의 모든 렌더러 색을 백업한 뒤 인스펙터 강조색(m_CamSelectColor)으로 변경한다.
    /// 마우스 클릭 피킹과 카메라 드롭다운 선택 모두 동일한 색을 사용한다.
    /// </summary>
    void SelectPole(CObjCamera kCamera)
    {
        if (kCamera == null || kCamera.m_goPole == null) return;
        if (m_SelectedPoleCamera == kCamera) return; // 이미 선택된 폴대

        ClearPoleSelection(); // 이전 선택 색 복원

        m_SelectedPoleCamera = kCamera;
        Renderer[] renderers = kCamera.m_goPole.GetComponentsInChildren<Renderer>(true);
        foreach (Renderer r in renderers)
        {
            if (r == null) continue;
            m_SelectedPoleRenderers.Add(r);
            m_SelectedPoleOrgColors.Add(r.material.color); // material 접근 시 인스턴스화 → 공유 머티리얼 보호
            r.material.color = m_CamSelectColor;
        }
    }

    /// <summary>
    /// 현재 선택된 폴대의 렌더러 색을 백업해 둔 원본 색으로 복원하고 선택을 해제한다.
    /// </summary>
    void ClearPoleSelection()
    {
        for (int i = 0; i < m_SelectedPoleRenderers.Count; i++)
        {
            Renderer r = m_SelectedPoleRenderers[i];
            if (r != null)
                r.material.color = m_SelectedPoleOrgColors[i];
        }
        m_SelectedPoleRenderers.Clear();
        m_SelectedPoleOrgColors.Clear();
        m_SelectedPoleCamera = null;
    }

    public void OnBeginDrag(PointerEventData data)
    {
    }
    public void OnDrag(PointerEventData data)
    {
    }
    public void OnEndDrag(PointerEventData data)
    {
    }

}
