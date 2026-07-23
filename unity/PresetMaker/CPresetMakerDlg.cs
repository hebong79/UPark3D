using UnityEngine;
using UnityEngine.UI;
using UnityEngine.EventSystems;
//using EasyRoads3Dv3;
using System.Collections;
using System.Collections.Generic;
using System.IO;


public class CPresetMakerDlg :CDialogUI, IBeginDragHandler, IDragHandler, IEndDragHandler
{
    [Header("Preset List")]
    public ScrollRect m_PresetScrollView = null;  // 프리셋 리스트
    public GameObject m_PrefabItem = null;          // 프리셋 리스트 아이템 프리팹
    public Text m_txtFileName;                  // 파일 이름 텍스트

    [Space]
    public Button m_btnAddPreset = null;         // 프리셋 추가      
    public Button m_btnRepairPreset = null;     // 프리셋 수정
    public Button m_btnDeletePreset = null;         // 프리셋 삭제
    public Button m_btnResetPreset = null;          // 프리셋 초기화
    public Toggle m_toHideSelectBar = null;         // 선택된 아이템의 하이라이트 바를 숨길지 여부


    [Header("Preset Data")]
    public InputField m_editPresetIdx = null;      // 프리셋 인덱스
    public InputField m_editFaceCount = null;      // 주차면 갯수
    public InputField m_editOffsetX = null;        // 주차면 박스 시작 위치
    public InputField m_editOffsetY = null;
    public InputField m_editOffsetZ = null;
    //public Button m_btnOffsetMove = null;           // 오프셋으로 이동하기 버튼
    [Space]
    public InputField m_editGroupRotate = null;   // 프리셋 회전값(주차면그룹)   
    public InputField m_editFaceRotate = null;     // 주차면 회전값
    public InputField m_editBoxSizeX = null;       // 주차면 가로 길이
    public InputField m_editBoxSizeZ = null;       // 주차면 세로 길이 ( z값 )
    [Space]
    public Dropdown m_dropDirType = null;          // Dir Type 은 기본으로 Direction을 선택한다.
    public Toggle m_toUseBaseWidth = null;         // xSize를 폭으로 사용할지 여부 ( true : xSize를 폭으로 사용, false : zSize를 폭으로 사용 )

    public InputField m_editCameraIdx = null;      // 카메라 인덱스 : 기본은 0
    public Toggle m_toUse3D = null;                // 3D 육면체 보여주기 여부 
    //public Toggle m_toTempMaking = null;           // 생성 버튼을 독립적으로 사용 여부( 저장 안됨 ) 
    public InputField m_editPresetName = null;     // 프리셋 이름

    //[Header("Camera Pos")]
    //public InputField m_editCamPosX = null;       // 카메라 위치
    //public InputField m_editCamPosY = null;       // 카메라 위치
    //public InputField m_editCamPosZ = null;       // 카메라 위치
    //[Space]
    //public InputField m_editCamRotX = null;       // 카메라 회전
    //public InputField m_editCamRotY = null;       // 카메라 회전
    //public InputField m_editCamRotZ = null;       // 카메라 회전
    //[Space]
    //public InputField m_editCamFOV = null;        // 카메라 FOV
    //[Space]
    //public Button m_btnMoveToCamPos;        // 카메라 위치로 이동,회전

    [Header("File IO Buttons")]
    public Button m_btnSave;
    public Button m_btnLoad;
    public Button m_btnClear;           // 모든 작업을 삭제한다. (주의)
    public Button m_btnMaker;           // 프리셋 생성하기
    public Button m_btnOffsetPick;      // 프리셋 시작 위치를 마우스로 picking 하기

    [Header("Title")]
    public Button m_btnExit = null;

    [Header("Used Class")]
    public CPMakerParkSpaceUI m_ParkSpaceUI = null;     // ParkSpaceUI
    public GameObject m_OffsetPosObj = null;            // 위치 표시용 오브젝트
    public CCamMouseControl m_CamMouseControl = null;   // 카메라 마우스 컨트롤러
    public Camera m_Camera = null;                      // 카메라
    public CPresetMoveUI m_PresetMoveUI = null;         // 프리셋 이동 컨트롤 UI
    public CDistanceMarkerViz m_DistanceMarkerViz = null;  // Distance 위치 마커 시각화
    [Space]
    public Text m_txtFaceWidth;                         // 주차면 폭 출력 (사선도 계산하여 보여준다.)
    public Toggle m_toShowMarker;


    //int m_iCurPresetIdx = 0;            // 스크롤 아이템듸 프리셋 id
    int m_iCurScrollItemIdx = 0;        // 스크롤 아이템의 순서 인덱스
    CItemText m_CurItemUI = null;
    Vector3 m_HitOffsetPoint = Vector3.zero;

    // 다중선택 상태 (런타임 전용 — [SerializeField] 없음, 씬 직렬화 영향 0)
    private List<CItemText> m_SelectedItems = new List<CItemText>();
    private CItemText m_AnchorItem = null;

    // Tab 키로 이동할 InputField 순서. OnEnable 에서 채워진다.
    List<InputField> m_TabOrder = null;
    
    protected bool m_IsShowMarker = false;            // Distance 마커 보여주기 여부

    float m_FaceRotate = 0f;
    public float FaceRotate => m_FaceRotate;

    public float CurrentPresetSavedFaceRot
    {
        get
        {
            int idx = m_CurItemUI == null ? -1 : m_CurItemUI.m_Idx;
            SDPresetInfo info = CDataMgr.Inst.m_SavePresetData.GetPresetInfo(idx);
            return info != null ? info.faceRot : 0f;
        }
    }

    // Start is called once before the first execution of Update after the MonoBehaviour is created
    void Start()
    {
        m_btnAddPreset.onClick.AddListener(OnClicked_AddPreset);
        m_btnRepairPreset.onClick.AddListener(OnClicked_RepairPreset);
        m_btnDeletePreset.onClick.AddListener(OnClicked_DeletePreset);
        m_btnResetPreset.onClick.AddListener(OnClicked_ResetPreset);
        
        m_btnSave.onClick.AddListener(OnClicked_Save);
        m_btnLoad.onClick.AddListener(OnClicked_Load);
        m_btnClear.onClick.AddListener(OnClicked_Clear);
        
        m_btnMaker.onClick.AddListener(OnClicked_Make);
        m_btnOffsetPick.onClick.AddListener(OnClicked_OffsetPick);
        m_btnExit.onClick.AddListener(OnClicked_Exit);

        m_toUseBaseWidth.onValueChanged.AddListener((bool isOn) =>
        {
        });

        m_toHideSelectBar.onValueChanged.AddListener((bool isOn) =>
        {
            m_ParkSpaceUI.ShowHighlightBar(!isOn);
        }); 


        m_toUse3D.onValueChanged.AddListener(OnValueChanged_Use3D);

        //m_btnMoveToCamPos.onClick.AddListener(OnClicked_MoveCamPos);



        if (m_Camera == null)
            m_Camera = Camera.main;

        m_PresetMoveUI.onAction_Move += (Vector3 pos) =>
        {
            m_editOffsetX.text = string.Format("{0:N3}", pos.x);
            m_editOffsetY.text = string.Format("{0:N3}", pos.y);
            m_editOffsetZ.text = string.Format("{0:N3}", pos.z);
        };


        m_PresetMoveUI.onAction_Rotate += (Vector3 vRot, SDPresetInfo kPresetInfo) =>
        {
            if (m_dropDirType.value == (int)EFaceDirType.Dir)
            {
                return;
            }

            m_FaceRotate = vRot.y;
            m_editFaceRotate.text = string.Format("{0:N3}", vRot.y);

        };


        m_toShowMarker?.onValueChanged.AddListener((bool isOn) =>
        {
            m_IsShowMarker = isOn;
            if (!isOn) {
                m_DistanceMarkerViz?.ClearMarkers();
            }
        });

        //InitDropdwon_PresetFiles();
    }

    // 프리셋 정보가 로드 되어있을 때 호출하면 된다.
    public void Initialize()
    {
        //스크롤뷰 아이템을 재 셋팅한다.
        Reset_CreatePresetItem();

        //CPMakerParkSpaceUI kParkSpaceUI = CDataMgr.Inst.pmakerScene.m_GameUI.m_ParkSpaceUI;
        // 새롭게 프리셋 주차면을 생성한다.
        m_ParkSpaceUI.MakePresetList(m_toUse3D.isOn);
        //m_ParkSpaceUI.CameraMoveToPreset();
        Invoke("Callabck_Initialize", 0.5f);
    }

    void Callabck_Initialize()
    {
        if(m_PresetScrollView.content.childCount == 0 )
        {
            m_CurItemUI = null;
            return;
        }
        CItemText kItem = m_PresetScrollView.content.GetChild(0).GetComponent<CItemText>();
        OnSelected_PresetItem(kItem, 0);
    }


    void Update()
    {
        if(CDataMgr.Inst.IsPresetMakeState())  //(m_bOffsetPickingState)
        {
            CheckPickingPositionByMouse();
        }
        HandleTabNavigation();
        //Camera.main.fieldOfView = 20.0f * (16.0f / 9);
    }

    // ─────────────────────────────────────────────────────────────
    // InputField Tab 네비게이션
    // ─────────────────────────────────────────────────────────────

    void OnEnable()
    {
        // 다이얼로그가 열릴 때마다 기본 InputField(Preset Idx) 에 포커스
        if (m_TabOrder == null || m_TabOrder.Count == 0)
            InitTabOrder();

        if (isActiveAndEnabled)
            StartCoroutine(FocusDefaultFieldNextFrame());
    }

    void InitTabOrder()
    {
        m_TabOrder = new List<InputField>
        {
            // Preset Data (Preset Idx 가 디폴트 첫 필드)
            m_editPresetIdx,
            m_editFaceCount,
            m_editOffsetX,
            m_editOffsetY,
            m_editOffsetZ,
            m_editGroupRotate,
            m_editFaceRotate,
            m_editBoxSizeX,
            m_editBoxSizeZ,
            m_editCameraIdx,
            m_editPresetName,
            // Camera Pos
            //m_editCamPosX,
            //m_editCamPosY,
            //m_editCamPosZ,
            //m_editCamRotX,
            //m_editCamRotY,
            //m_editCamRotZ,
            //m_editCamFOV,
        };
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

        // 현재 포커스가 InputField 외부면 첫(or 마지막) 으로 진입
        int nextIdx = currentIdx < 0
                    ? (reverse ? count - 1 : 0)
                    : (currentIdx + step + count) % count;

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

    //public void SetCamPosFromCamera()
    //{
    //    //Vector3 vPos = m_Camera.transform.position; 
    //    //Vector3 vRot = m_Camera.transform.localEulerAngles;
    //    //m_editCamPosX.text = string.Format("{0:0.###}", vPos.x);
    //    //m_editCamPosY.text = string.Format("{0:0.###}", vPos.y);
    //    //m_editCamPosZ.text = string.Format("{0:0.###}", vPos.z);

    //    //m_editCamRotX.text = string.Format("{0:0.###}", vRot.x);
    //    //m_editCamRotY.text = string.Format("{0:0.###}", vRot.y);
    //    //m_editCamRotZ.text = string.Format("{0:0.###}", vRot.z);
    //}

    void OnClicked_Exit()
    {
        this.CloseUI();
    }

    public override void CloseUI()
    {
        base.CloseUI();
        
        CDataMgr.Inst.SetNormalState();
        m_PresetMoveUI.ExitMoveState();
    }


    // 프리셋 추가
    void OnClicked_AddPreset()
    {
        //SetCamPosFromCamera();

        int idx = CDataMgr.Inst.m_SavePresetData.Count()+1;
        // 이미 존재하는 인덱스면 겹치지 않는 인덱스를 찾을 때까지 1씩 증가시킨다.
        while (CDataMgr.Inst.m_SavePresetData.IsExistPresetIdx(idx))
            idx++;
        m_editPresetIdx.text = idx.ToString();

        SDPresetInfo kPresetInfo = new SDPresetInfo();

        ToData(kPresetInfo);

        kPresetInfo.idx = idx;
        CDataMgr.Inst.m_SavePresetData.Add(kPresetInfo);

        //int idx = CDataMgr.Inst.m_SavePresetData.Count()-1;
        // 스크롤뷰에 추가
        CItemText kItem = CreatePresetItem(kPresetInfo, idx, true);

        OnSelected_PresetItem(kItem, idx);

        m_ParkSpaceUI.MakePreset(kPresetInfo, m_toUse3D.isOn);
        UpdatePresetObj();
        UpdateFaceWidthDisplay(kPresetInfo);

        // 새로 추가된 아이템이 스크롤뷰 화면에 보이도록 끝으로 스크롤
        ScrollToLastItem();
    }

    /// <summary>
    /// 프리셋 스크롤뷰를 마지막 아이템 위치로 이동시킨다.
    /// 수직/수평 방향 모두 지원하며, 호출 직전에 추가된 아이템의
    /// Layout 이 반영되도록 ForceUpdateCanvases 를 먼저 수행한다.
    /// </summary>
    void ScrollToLastItem()
    {
        if (m_PresetScrollView == null) return;

        Canvas.ForceUpdateCanvases();
        if (m_PresetScrollView.vertical)
            m_PresetScrollView.verticalNormalizedPosition = 0f;     // 수직: 0 = 최하단
        if (m_PresetScrollView.horizontal)
            m_PresetScrollView.horizontalNormalizedPosition = 1f;   // 수평: 1 = 최우측
    }

    // 프리셋 수정
    void OnClicked_RepairPreset()
    {
        //SetCamPosFromCamera();

        if( m_CurItemUI == null )
        {
            string sMsg = "수정할 프리셋을 선택하세요.";
            CDataMgr.Inst.pmakerScene.m_MsgBoxUI.OpenUI(sMsg, MBtnType.OK);
            return;
        }
        int iRepairIdx = int.Parse(m_editPresetIdx.text);
        int iPresetIdx = m_CurItemUI.m_Idx;
        if( iRepairIdx != iPresetIdx )
        {
            var kPreset = CDataMgr.Inst.m_SavePresetData.m_Datas.datas.Find(x => x.idx == iRepairIdx);
            if (kPreset != null)
            {
                string sMsg = string.Format("동일한 프리셋 인덱스가 2개 존재합니다. idx={0}", iRepairIdx);
                CDataMgr.Inst.pmakerScene.m_MsgBoxUI.OpenUI(sMsg, MBtnType.OK);
                return;
            }
        }

        SDPresetInfo kPresetInfo = CDataMgr.Inst.m_SavePresetData.GetPresetInfo(iPresetIdx);
        if (kPresetInfo == null) return;

        //스크롤뷰 아이템의 프리셋 이름과 인덱스 갱신
        m_CurItemUI.m_Idx = m_editPresetIdx.text == "" ? -1 : int.Parse(m_editPresetIdx.text);
        m_CurItemUI.m_TxtName.text = m_editPresetName.text;


        // 데이터 갱신
        ToData(kPresetInfo);
        m_ParkSpaceUI.MakePreset(kPresetInfo, m_toUse3D.isOn);
        UpdatePresetObj();
        UpdateFaceWidthDisplay(kPresetInfo);

        // id값을 변경할경우 새롭게 생성되브로 기존 아이디 프리셋을 제거해야 한다.
        // 스크롤 아이템의 버튼 인덱스 정보가 바뀌므로 버튼 리스너도 새롭게 등록한다.
        if (iRepairIdx != iPresetIdx)
        {
            // 람다가 인스턴스 필드(m_CurItemUI)를 직접 캡처하면 이후 다른 아이템을
            // 선택했을 때 m_CurItemUI 값이 바뀌어 잘못된 아이템이 전달된다.
            // 반드시 로컬 변수로 복사한 뒤 캡처할 것.
            CItemText kCapturedItem = m_CurItemUI;
            int       iCapturedIdx  = iRepairIdx;
            Button btn = m_CurItemUI.GetComponent<Button>();
            btn.onClick.RemoveAllListeners();
            btn.onClick.AddListener(() => OnClicked_PresetItem(kCapturedItem, iCapturedIdx));
            m_ParkSpaceUI.RemovePresetObj(iPresetIdx);
        }
    }

    // 프리셋 삭제
    void OnClicked_DeletePreset()
    {
        if (m_CurItemUI == null) return;

        int iPresetIdx = m_CurItemUI.m_Idx;
        Transform deletingTr = m_CurItemUI.transform;
        Transform contentTr  = m_PresetScrollView.content;

        // ── 삭제 전에 다음 선택 후보를 미리 탐색 (위 우선, 없으면 아래) ──
        // sibling ±1 만 보지 않고 CItemText 컴포넌트를 가진 가장 가까운
        // 살아있는 형제를 직접 찾는다. 이렇게 하면:
        //   - content 자식 리스트에 CItemText 외 GameObject 가 섞여 있어도 안전
        //   - 이전 삭제로 Destroy 큐에 들어간 시체(컴포넌트가 분리된 객체) 건너뜀
        //   - 띄엄띄엄 삭제 후 첫 아이템 삭제 시 다음 아이템을 안정적으로 찾음
        int siblingIdx = deletingTr.GetSiblingIndex();
        CItemText nextItem = FindAdjacentItem(contentTr, deletingTr, siblingIdx);

        // ── 삭제 처리 ──────────────────────────────────────────────
        CDataMgr.Inst.m_SavePresetData.Remove(iPresetIdx);
        m_ParkSpaceUI.RemovePresetObj(iPresetIdx);
        RemoveScrollItem(iPresetIdx);
        m_CurItemUI = null;
        // 다중선택 집합에서 파괴 대상 참조 제거
        m_SelectedItems.Remove(m_CurItemUI);   // m_CurItemUI는 이미 null이므로 무해
        // deletingTr 기반으로 확실하게 제거
        {
            CItemText deletingItem = deletingTr.GetComponent<CItemText>();
            if (deletingItem != null) m_SelectedItems.Remove(deletingItem);
            if (m_AnchorItem != null && m_AnchorItem == deletingItem) m_AnchorItem = null;
        }

        // ── 다음 아이템 선택 ─────────────────────────────────────
        if (nextItem != null)
            OnSelected_PresetItem(nextItem, nextItem.m_Idx);
    }

    void OnClicked_ResetPreset()
    {
        Initialize();
    }

    /// <summary>
    /// 삭제 대상(deletingTr) 의 형제 중 CItemText 를 가진 가장 가까운
    /// 위/아래 아이템을 찾아 반환한다. 위가 없으면 아래로 fallback.
    /// </summary>
    CItemText FindAdjacentItem(Transform contentTr, Transform deletingTr, int siblingIdx)
    {
        // 위쪽으로 가장 가까운 CItemText 탐색
        for (int i = siblingIdx - 1; i >= 0; i--)
        {
            Transform tr = contentTr.GetChild(i);
            if (tr == null || tr == deletingTr) continue;
            CItemText kItem = tr.GetComponent<CItemText>();
            if (kItem != null) return kItem;
        }
        // 위가 없으면 아래쪽으로 가장 가까운 CItemText 탐색
        int childCount = contentTr.childCount;
        for (int i = siblingIdx + 1; i < childCount; i++)
        {
            Transform tr = contentTr.GetChild(i);
            if (tr == null || tr == deletingTr) continue;
            CItemText kItem = tr.GetComponent<CItemText>();
            if (kItem != null) return kItem;
        }
        return null;
    }
    
    // 프리셋 저장 
    void OnClicked_Save()
    {
        string sPathName = StandaloneFileSave_Json();
        if ( sPathName.Length > 0 )
        {
            string sFileName = Path.GetFileName(sPathName);
            m_DefaultFileName = sFileName;
            CDataMgr.Inst.SavePresetFile(CDataMgr.DSAVE_PATH_PRESET, sFileName);
        }
    }

    public string StandaloneFileSave_Json()
    {
        string baseDir = CDataMgr.DSAVE_PATH_PRESET;
        string sDefaultFileName = m_DefaultFileName == "" ? "preset_name" : m_DefaultFileName;

        return CMyUtil.StandaloneFileSave(baseDir, sDefaultFileName, "Json Files", "json");
    }

    public string StandaloneFileOpen_Json()
    {
        string baseDir = CDataMgr.DSAVE_PATH_PRESET;

        return CMyUtil.StandaloneFileOpen(baseDir, "Json Files", "json");
    }

    string m_DefaultFileName = "";

    // 프리셋 열기
    void OnClicked_Load()
    {
        string sFilePath = StandaloneFileOpen_Json();
        if (sFilePath.Length > 0)
        {
            m_DefaultFileName = Path.GetFileName(sFilePath);
            m_txtFileName.text = m_DefaultFileName;

            if (CDataMgr.Inst.LoadPresetFile("", sFilePath))
            {
                Initialize();
            }
        }
    }

    // 모두 초기화(삭제)
    void OnClicked_Clear()
    {
        // 오브젝트 파괴
        //CPMakerParkSpaceUI kParkSpaceUI = CDataMgr.Inst.pmakerScene.m_GameUI.m_ParkSpaceUI;
        m_ParkSpaceUI.RemoveAll();

        // 데이터 클리어
        CDataMgr.Inst.RemoveAll_PresetDataList();

        //스크롤뷰 클리어
        RemoveAll_ScrollItems();
        m_CurItemUI = null;
        m_SelectedItems.Clear();
        m_AnchorItem = null;
    }

    void OnValueChanged_Use3D(bool isOn)
    {
        //CPMakerParkSpaceUI kParkSpaceUI = CDataMgr.Inst.pmakerScene.m_GameUI.m_ParkSpaceUI;
        m_ParkSpaceUI.ShowQubeLineList(isOn);
    }

    void OnClicked_Make()
    {
        m_ParkSpaceUI.MakePresetList(m_toUse3D.isOn);

        // 재생성 후 선택 상태 복원
        if (m_CurItemUI == null)
            return;

        UpdatePresetObj(true);

        int iPresetIdx = m_CurItemUI.m_Idx;
        SDPresetInfo kPreset = CDataMgr.Inst.m_SavePresetData.GetPresetInfo(iPresetIdx);
        UpdateFaceWidthDisplay(kPreset);
    }

    // 주차면 폭/프리셋 폭 계산하여 m_txtFaceWidth에 출력 ( 대각선 회전 폭 포함 )
    private void UpdateFaceWidthDisplay(SDPresetInfo kPreset)
    {
        // 주차면 폭 계산하여 보여주기
        if ( kPreset != null && kPreset.dirType != (int)EFaceDirType.Dir )
        {
            float faceRot = kPreset.faceRot;
            float xSize = kPreset.xSize;
            float zSize = kPreset.zSize;

            if (!kPreset.useBaseWidth )//   == (int)EFaceDirType.ZPlus || kPreset.dirType == (int)EFaceDirType.ZMinus)
            {
                xSize = kPreset.zSize;
                zSize = kPreset.xSize;
            }

            if (kPreset.dirType != (int)EFaceDirType.Dir)
            {

                var dis2 = CParkingGeometry.GetSingleParkingDimensions(faceRot, xSize, zSize, 0);
                Debug.Log(string.Format("[Single] 각도: {0:0.###}도 | 왼쪽 Slope 폭: {1:0.###}m  |  Box X폭: {2:0.###}m  |  세로: {3:0.###}m",
                    faceRot, dis2.leftSlopeW, dis2.boxWidth, dis2.boxHeight));

                var dist = CParkingGeometry.GetMultiParkingDimensions(kPreset.faceCount, faceRot, xSize, zSize, 0);
                m_txtFaceWidth.text = string.Format(
                    "각도: {0:0.###}도 |  1개 step폭: {1:0.###}m  |  전체 X폭: {2:0.###}m  |  세로폭: {3:0.###}m",
                    faceRot, dist.stepW, dist.totWidth, dist.oneH);

                Debug.Log("[Multi] " + m_txtFaceWidth.text);
            }
            else
            {
                // Dir 타입은 회전이 프리셋 전체로 변경되므로 각도는 0으로 계산한다.
                var dis2 = CParkingGeometry.GetSingleParkingDimensions(0, xSize, zSize, 0);
                Debug.Log(string.Format("[Single] 각도: {0:0.###}도 | 왼쪽 Slope 폭: {1:0.###}m  |  Box X폭: {2:0.###}m  |  세로: {3:0.###}m",
                    dis2.leftSlopeW, dis2.boxWidth, dis2.boxHeight));

                var dist = CParkingGeometry.GetMultiParkingDimensions(kPreset.faceCount, 0, xSize, zSize, 0);
                m_txtFaceWidth.text = string.Format(
                    "1개 step폭: {1:0.###}m  |  전체 X폭: {2:0.###}m  |  세로폭: {3:0.###}m",
                    dist.stepW, dist.totWidth, dist.oneH);

            }
        }
        else
        {
            // Dir 타입이거나 프리셋이 없으면 마커 제거
            m_DistanceMarkerViz?.ClearMarkers();
        }
    }

    // 프리셋 오브젝트를 갱신한다. ( 주로 프리셋 수정 후에 호출한다. )
    public void UpdatePresetObj(bool bUseHighlight=true)
    {
        int iPresetIdx = m_CurItemUI.m_Idx;
        if (bUseHighlight)
            m_ParkSpaceUI.HighlightPreset(iPresetIdx);

        SDPresetInfo kPreset = CDataMgr.Inst.m_SavePresetData.GetPresetInfo(iPresetIdx);

        var kPresetObj = m_ParkSpaceUI.GetPresetObj();
        if (kPresetObj != null)
            m_PresetMoveUI.Initialize(kPresetObj, kPreset);

    }

    // 시작위치를 바닥면에 피킹하여 얻는다.
    void OnClicked_OffsetPick()
    {
        if (!CDataMgr.Inst.IsPresetMakeState())
        {
            CDataMgr.Inst.SetPresetMakeState();
            m_PresetMoveUI.EnterMoveState();
        }
        else
        {
            CDataMgr.Inst.SetNormalState();
            m_PresetMoveUI.ExitMoveState();
        }
    }

    public void Enter_PresetMakeState()
    {
        Text kText = m_btnOffsetPick.GetComponentInChildren<Text>();

        kText.text = "Picking ...";
        kText.color = Color.red;
        m_OffsetPosObj?.SetActive(true);
    }

    public void Exit_PresetMakeState()
    {
        Text kText = m_btnOffsetPick.GetComponentInChildren<Text>();
        if (kText != null)
        {
            kText.text = "Offset Pick";
            kText.color = Color.black;
            m_OffsetPosObj?.SetActive(false);
        }
    }

    // 시작위치를 바닥면에 피킹하여 얻는다.
    //void OnClicked_OffsetMove()
    //{
    //    Vector3 vTarget = m_OffsetPosObj.transform.position;
    //    Vector3 vPos = m_Camera.transform.position;
    //    vPos = vTarget;

    //    m_Camera.transform.position = vPos;
    //}

    //void OnClicked_MoveCamPos()
    //{
    //    Vector3 vPos = this.GetCameraPosByUI();
    //    Vector3 vRot = this.GetCameraRotByUI();
    //    m_Camera.transform.position = vPos;
    //    m_Camera.transform.localRotation = Quaternion.Euler(vRot);
    //    float fov = float.Parse(m_editCamFOV.text);
    //    // 수평시야각 = fov * 화면비율( fov * 16.0f / 9 ) 인데
    //    // camera의 fov는 수직시야각 기준으로 계산이 되므로 화면 비율을 반대로 곱한다.
    //    m_Camera.fieldOfView = fov * 9.0f / 16;    
    //}

    public void ToData( SDPresetInfo kPresetInfo )
    {
        //SDPresetInfo kPresetInfo = CDataMgr.Inst.m_SavePresetData.GetPresetInfo(m_iCurPresetIdx);
        kPresetInfo.idx = int.Parse(m_editPresetIdx.text);
        kPresetInfo.faceCount = int.Parse(m_editFaceCount.text);
        kPresetInfo.faceRot = float.Parse(m_editFaceRotate.text);
        kPresetInfo.groupRot = float.Parse(m_editGroupRotate.text);

        kPresetInfo.offsetPos.x = float.Parse(m_editOffsetX.text);
        kPresetInfo.offsetPos.y = float.Parse(m_editOffsetY.text);
        kPresetInfo.offsetPos.z = float.Parse(m_editOffsetZ.text);

        kPresetInfo.xSize = float.Parse(m_editBoxSizeX.text);
        kPresetInfo.zSize = float.Parse(m_editBoxSizeZ.text);
        kPresetInfo.dirType = m_dropDirType.value;
        kPresetInfo.useBaseWidth = m_toUseBaseWidth.isOn;
        kPresetInfo.camIdx = int.Parse(m_editCameraIdx.text);


        kPresetInfo.presetName = m_editPresetName.text;
    }

    public void FromData(SDPresetInfo kPresetInfo)
    {
        //SDPresetInfo kPresetInfo = CDataMgr.Inst.m_SavePresetData.GetPresetInfo(m_iCurPresetIdx);
        m_editPresetIdx.text = kPresetInfo.idx.ToString();
        m_editFaceCount.text = kPresetInfo.faceCount.ToString();
        m_editFaceRotate.text = string.Format("{0:F3}", kPresetInfo.faceRot);
        m_FaceRotate = kPresetInfo.faceRot;
        m_editGroupRotate.text = string.Format("{0:F3}", kPresetInfo.groupRot);

        m_editOffsetX.text = string.Format("{0:F3}", kPresetInfo.offsetPos.x); 
        m_editOffsetY.text = string.Format("{0:F3}", kPresetInfo.offsetPos.y);
        m_editOffsetZ.text = string.Format("{0:F3}", kPresetInfo.offsetPos.z);

        m_editBoxSizeX.text = string.Format("{0:F3}", kPresetInfo.xSize);
        m_editBoxSizeZ.text = string.Format("{0:F3}", kPresetInfo.zSize);
        m_dropDirType.value = kPresetInfo.dirType;
        m_toUseBaseWidth.isOn = kPresetInfo.useBaseWidth;
        m_editCameraIdx.text = kPresetInfo.camIdx.ToString();

        m_editPresetName.text = kPresetInfo.presetName;
    }

    //Vector3 GetCameraPosByUI()
    //{
    //    Vector3 camPos = Vector3.zero;
    //    camPos.x = float.Parse(m_editCamPosX.text);
    //    camPos.y = float.Parse(m_editCamPosY.text);
    //    camPos.z = float.Parse(m_editCamPosZ.text);

    //    return camPos;
    //}

    //Vector3 GetCameraRotByUI()
    //{
    //    Vector3 camRot = Vector3.zero;
    //    camRot.x = float.Parse(m_editCamRotX.text);
    //    camRot.y = float.Parse(m_editCamRotY.text);
    //    camRot.z = float.Parse(m_editCamRotZ.text);

    //    return camRot;
    //}


    void Reset_CreatePresetItem()
    {
        RemoveAll_ScrollItems();
        m_CurItemUI = null;
        m_SelectedItems.Clear();
        m_AnchorItem = null;
        //CDataMgr.Inst.m_SavePresetData.m_Datas.datas
        SDPresetDatas kDatas = CDataMgr.Inst.m_SavePresetData.m_Datas;
        //PresetId 순으로 정렬하여 스크롤뷰에 아이템 생성
        kDatas.datas.Sort((a, b) => a.idx.CompareTo(b.idx));

        for (int i = 0; i < kDatas.datas.Count; i++)
        {
            SDPresetInfo kPreset = kDatas.datas[i];
            CreatePresetItem(kPreset, i+1, bForceIdxName: false);
        }
    }

    // 스클롤뷰 아이템 생성 
    // iOrderIdx : 스크롤 뷰의 순서 인덱스
    CItemText CreatePresetItem(SDPresetInfo kPreset, int iOrderIdx, bool bForceIdxName)
    {
        GameObject go = Instantiate(m_PrefabItem, m_PresetScrollView.content);
        go.transform.localPosition = Vector3.zero;
        
        CItemText kItem = go.GetComponent<CItemText>();
        if(bForceIdxName)
            kPreset.presetName = string.Format("Preset {0}", kPreset.idx);

        //string sName = string.Format("Preset {0}", kPreset.idx);
        //if( kPreset.presetName.Length > 0)
        //    sName = kPreset.presetName;
        //else
        //kPreset.presetName = sName;

        kItem.Initialize(kPreset.presetName, kPreset.idx);
        Button btn = kItem.GetComponent<Button>();

        btn.onClick.AddListener(() =>
        {
            OnClicked_PresetItem(kItem, iOrderIdx);
        });
        return kItem;
    }

    // iOrderIdx : 스크롤 뷰의 순서 인덱스
    void OnSelected_PresetItem(CItemText kItem, int iOrderIdx)
    {
        // 기존 다중선택 전부 해제 (프로그램적 단일선택 경로 보장)
        for (int i = 0; i < m_SelectedItems.Count; i++)
            if (m_SelectedItems[i] != null) m_SelectedItems[i].SetSelectd(false);
        m_SelectedItems.Clear();

        if (m_CurItemUI != null)
        {
            m_CurItemUI.SetSelectd(false);
            //SDPresetInfo kPrevPreset = CDataMgr.Inst.m_SavePresetData.GetPresetInfo(m_CurItemUI.m_Idx);
            //if (kPrevPreset != null)
            //    ToData(kPrevPreset);
        }

        m_CurItemUI = kItem;
       // m_iCurPresetIdx = kItem.m_Idx;
        m_ParkSpaceUI.m_iCurPresetIdx = kItem.m_Idx;

        m_iCurScrollItemIdx = iOrderIdx;
        kItem.SetSelectd(true);

        // 프리셋 정보 갱신
        SDPresetInfo kPreset = CDataMgr.Inst.m_SavePresetData.GetPresetInfo(kItem.m_Idx);
        if (kPreset != null)
        {
            FromData(kPreset);
            UpdateFaceWidthDisplay(kPreset);
        }

        // Preset Move 초기화
        //CPMakerParkSpaceUI kParkSpaceUI = CDataMgr.Inst.pmakerScene.m_GameUI.m_ParkSpaceUI;
        var kPresetObj = m_ParkSpaceUI.GetPresetObj(kItem.m_Idx);
        m_PresetMoveUI.Initialize(kPresetObj, kPreset);

        // 선택된 프리셋 하이라이트
        m_ParkSpaceUI.HighlightPreset(kItem.m_Idx);

        // 단일선택 상태로 다중선택 집합 동기화
        m_SelectedItems.Add(kItem);
        m_AnchorItem = kItem;
    }

    // 버튼 onClick 에 연결되는 라우터 — 모디파이어 키로 단일/Ctrl/Shift 분기
    void OnClicked_PresetItem(CItemText kItem, int iOrderIdx)
    {
        bool ctrl  = Input.GetKey(KeyCode.LeftControl)  || Input.GetKey(KeyCode.RightControl);
        bool shift = Input.GetKey(KeyCode.LeftShift)    || Input.GetKey(KeyCode.RightShift);

        if (shift && m_AnchorItem != null)
        {
            // Shift: 앵커~현재 범위 선택 (앵커 갱신 안 함)
            SelectRange(m_AnchorItem, kItem);
            m_CurItemUI = kItem;
            m_ParkSpaceUI.m_iCurPresetIdx = kItem.m_Idx;
            m_iCurScrollItemIdx = iOrderIdx;
            SDPresetInfo kRangePreset = CDataMgr.Inst.m_SavePresetData.GetPresetInfo(kItem.m_Idx);
            if (kRangePreset != null) { FromData(kRangePreset); UpdateFaceWidthDisplay(kRangePreset); }
            RefreshSelectionVisualsAndTargets();
        }
        else if (ctrl)
        {
            // Ctrl: 토글 추가/제거
            if (m_SelectedItems.Contains(kItem))
            {
                // 선택 해제
                kItem.SetSelectd(false);
                m_SelectedItems.Remove(kItem);
                // primary를 남은 마지막으로 재지정
                m_CurItemUI = m_SelectedItems.Count > 0 ? m_SelectedItems[m_SelectedItems.Count - 1] : null;
                if (m_CurItemUI != null)
                {
                    m_ParkSpaceUI.m_iCurPresetIdx = m_CurItemUI.m_Idx;
                    SDPresetInfo kPrimPreset = CDataMgr.Inst.m_SavePresetData.GetPresetInfo(m_CurItemUI.m_Idx);
                    if (kPrimPreset != null) { FromData(kPrimPreset); UpdateFaceWidthDisplay(kPrimPreset); }
                }
            }
            else
            {
                // 추가
                m_SelectedItems.Add(kItem);
                m_AnchorItem = kItem;
                m_CurItemUI  = kItem;
                m_ParkSpaceUI.m_iCurPresetIdx = kItem.m_Idx;
                m_iCurScrollItemIdx = iOrderIdx;
                SDPresetInfo kAddPreset = CDataMgr.Inst.m_SavePresetData.GetPresetInfo(kItem.m_Idx);
                if (kAddPreset != null) { FromData(kAddPreset); UpdateFaceWidthDisplay(kAddPreset); }
            }
            RefreshSelectionVisualsAndTargets();
        }
        else
        {
            // 단순 클릭: 단일선택 — 기존 OnSelected_PresetItem 에 위임 (앵커도 갱신됨)
            OnSelected_PresetItem(kItem, iOrderIdx);
        }
    }

    // Shift 범위 선택: content sibling index 기준 anchorItem~currentItem inclusive
    void SelectRange(CItemText anchorItem, CItemText currentItem)
    {
        // 기존 선택 해제
        for (int i = 0; i < m_SelectedItems.Count; i++)
            if (m_SelectedItems[i] != null) m_SelectedItems[i].SetSelectd(false);
        m_SelectedItems.Clear();

        Transform content = m_PresetScrollView.content;
        int anchorSib  = anchorItem.transform.GetSiblingIndex();
        int currentSib = currentItem.transform.GetSiblingIndex();
        int minSib = Mathf.Min(anchorSib, currentSib);
        int maxSib = Mathf.Max(anchorSib, currentSib);

        for (int i = minSib; i <= maxSib; i++)
        {
            Transform tr = content.GetChild(i);
            if (tr == null) continue;
            CItemText kItemText = tr.GetComponent<CItemText>();
            if (kItemText != null)
                m_SelectedItems.Add(kItemText);
        }
    }

    // 선택 시각화 + MoveUI 타깃 갱신
    void RefreshSelectionVisualsAndTargets()
    {
        // 스크롤 아이템 색상 갱신
        Transform content = m_PresetScrollView.content;
        for (int i = 0; i < content.childCount; i++)
        {
            CItemText kText = content.GetChild(i).GetComponent<CItemText>();
            if (kText == null) continue;
            kText.SetSelectd(m_SelectedItems.Contains(kText));
        }

        // 선택 프리셋 인덱스 목록
        var selectedIdxs = new List<int>(m_SelectedItems.Count);
        var selectedObjs  = new List<CPMakerParkSpaceUI.SPresetObjUI>(m_SelectedItems.Count);
        var selectedInfos = new List<SDPresetInfo>(m_SelectedItems.Count);

        for (int i = 0; i < m_SelectedItems.Count; i++)
        {
            int idx = m_SelectedItems[i].m_Idx;
            selectedIdxs.Add(idx);
            var obj  = m_ParkSpaceUI.GetPresetObj(idx);
            var info = CDataMgr.Inst.m_SavePresetData.GetPresetInfo(idx);
            if (obj  != null) selectedObjs.Add(obj);
            if (info != null) selectedInfos.Add(info);
        }

        int primaryIdx = m_CurItemUI != null ? m_CurItemUI.m_Idx : -1;
        m_ParkSpaceUI.HighlightPresets(selectedIdxs, primaryIdx);

        var primaryObj  = primaryIdx >= 0 ? m_ParkSpaceUI.GetPresetObj(primaryIdx) : null;
        var primaryInfo = primaryIdx >= 0 ? CDataMgr.Inst.m_SavePresetData.GetPresetInfo(primaryIdx) : null;
        m_PresetMoveUI.SetTargets(selectedObjs, selectedInfos, primaryObj, primaryInfo);
    }


    public void RemoveAll_ScrollItems()
    {
        for (int i = m_PresetScrollView.content.childCount - 1; i >= 0; i--)
        {
            Transform tr = m_PresetScrollView.content.GetChild(i);
            if (tr != null)
            {
                Destroy(tr.gameObject);
            }
        }
    }

    public void RemoveScrollItem2(int iOrderIdx)
    {
        Transform tr = m_PresetScrollView.content.GetChild(iOrderIdx);
        if( tr != null)
            Destroy(tr.gameObject);
    }
    
    // 스크롤 아이템 삭제
    public void RemoveScrollItem(int iPresetId)
    {
        for (int i = m_PresetScrollView.content.childCount - 1; i >= 0; i--)
        {
            Transform tr = m_PresetScrollView.content.GetChild(i);
            if (tr != null)
            {
                var kText = tr.GetComponent<CItemText>();
                if (kText != null)
                {
                    if (kText.m_Idx == iPresetId)
                    {
                        Destroy(tr.gameObject);
                        break;
                    }
                }
            }
        }
    }

    void SetOffsetPositionToUI(Vector3 vPos)
    {
        m_editOffsetX.text = string.Format("{0:0.000}", vPos.x);
        m_editOffsetY.text = string.Format("{0:0.000}", vPos.y);
        m_editOffsetZ.text = string.Format("{0:0.000}", vPos.z);

        int iPresetIdx = m_CurItemUI == null ? -1 : m_CurItemUI.m_Idx;
        SDPresetInfo kPresetInfo = CDataMgr.Inst.m_SavePresetData.GetPresetInfo(iPresetIdx);
        if (kPresetInfo != null)
        {
            kPresetInfo.offsetPos.Set(vPos);
            kPresetInfo.faceRot = float.Parse(m_editFaceRotate.text);
            ToData(kPresetInfo);
        }
    }

    [Space]
    public LayerMask m_SelectableLayer; // 선택할 오브젝트가 속한 레이어

    public void CheckPickingPositionByMouse()
    {
        if (Input.GetKey(KeyCode.LeftControl))
        {
            if (Input.GetMouseButtonDown(0)) // 마우스 왼쪽 버튼 클릭
            {
                // UI 요소 위에 있을 때는 바닥 레이캐스트 피킹 건너뜀
                if (EventSystem.current != null && EventSystem.current.IsPointerOverGameObject())
                    return;

                Vector3 vMouse = Input.mousePosition;
                Ray ray = Camera.main.ScreenPointToRay(vMouse);
                RaycastHit hit;

                if (Physics.Raycast(ray, out hit, Mathf.Infinity, m_SelectableLayer))
                {
                    if (hit.collider.tag == "Floor")// || hit.collider.tag == "Road")
                    {
                        m_HitOffsetPoint = hit.point;

                        m_OffsetPosObj.transform.position = hit.point;
                        SetOffsetPositionToUI(hit.point);

                        m_PresetMoveUI.MovePreset(hit.point);
                        if (m_CurItemUI != null)
                        {
                            m_ParkSpaceUI.RemoveAll_SelectOveray();
                            m_ParkSpaceUI.HighlightPreset(m_CurItemUI.m_Idx);
                        }

                        
                        //OnClicked_Make();

                        //메인카메라의 마우스 컨트롤의 타겟 정보 변경
                        m_CamMouseControl.m_TargetObject = m_OffsetPosObj.transform;
                    }
                }
            }
        }
    }

    public void OnBeginDrag(PointerEventData data){ }
    public void OnDrag(PointerEventData data){ }
    public void OnEndDrag(PointerEventData data){ }


    // ─────────────────────────────────────────────────────────────────────
    // 주차면 번호 할당
    // ─────────────────────────────────────────────────────────────────────

    /// <summary>
    /// 카메라 기준으로 각 프리셋에 주차면 번호를 저장된 순서대로 할당한다.
    ///
    /// 할당 우선순위
    ///   1. 카메라 인덱스(camIdx) 오름차순
    ///   2. 프리셋 인덱스(idx) 오름차순
    ///   3. 프리셋 내 주차면 순서 (1번부터 연속 번호)
    ///
    /// 반환값: 카메라/프리셋 순으로 정렬된 SParkingSpaceAssignment 목록.
    ///         각 항목에는 해당 프리셋의 시작/끝 주차면 번호 및 전체 번호 리스트가 담겨 있다.
    /// </summary>
    public List<SPSpaceAssignment> SetParkingSpaceNumbers()
    {
        var presets     = CDataMgr.Inst.m_SavePresetData.m_Datas.datas;
        var assignments = CSavePresetData.CalculateParkingSpaceAssignments(presets);

        if (assignments.Count == 0)
        {
            Debug.LogWarning("[SetParkingSpaceNumbers] 프리셋 데이터가 없습니다.");
            return assignments;
        }

        // 결과 로그 출력
        Debug.Log("[SetParkingSpaceNumbers] ===== 주차면 번호 할당 결과 =====");
        int prevCam = -1;
        foreach (var assign in assignments)
        {
            if (assign.camIdx != prevCam)
            {
                Debug.Log(string.Format("  [카메라 {0}]", assign.camIdx));
                prevCam = assign.camIdx;
            }
            Debug.Log(string.Format(
                "    프리셋[{0}] : 주차면 {1} ~ {2}  (총 {3}개)",
                assign.presetIdx,
                assign.startFaceNum,
                assign.EndFaceNum,
                assign.faceCount));
        }
        Debug.Log(string.Format("[SetParkingSpaceNumbers] 전체 주차면 수 = {0}",
            assignments.Count > 0
                ? assignments[assignments.Count - 1].EndFaceNum
                : 0));

        return assignments;
    }



    
}
