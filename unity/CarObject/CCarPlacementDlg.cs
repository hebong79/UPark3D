//using SCarPosData = CSaveCarPosData.SObjectPosDatas;

using System;
using System.Collections.Generic;
using System.IO;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.UI;
using SCarPos = SObjectPos;

/// <summary>
/// 차량 배치를 위한 대화상자
/// </summary>
public class CCarPlacementDlg : CDialogUI, IBeginDragHandler, IDragHandler, IEndDragHandler
{
    public enum EKeyType
    {
        None = 0,
        Move = 1,   // 이동
        Rot = 2,    // 회전    
    }


    public CCarObjListUI m_CarObjListUI = null; // 차량 오브젝트 리스트 UI
    public Text m_txtFileName;                  // 파일명 텍스트

    [Header("차량 TYPE")]
    [SerializeField] Dropdown m_cboCarType;         // 차량 타입 드롭다운
    [SerializeField] GameObject m_PrefabItemText;   // 차량 오브젝트 리스트 아이템 프리팹

    [Header("버튼들")]
    [SerializeField] Button m_btnSave;      // 저장
    [SerializeField] Button m_btnLoad;      // 불러오기
    [SerializeField] Button m_btnClear;     // 초기화
    [SerializeField] Button m_btnExit;      // 종료

    [Header("차량위치 리스트 ")]
    [SerializeField] Dropdown m_cboCarPrefabs;      // 차량 프리팹 드롭다운
    [SerializeField] ScrollRect m_CarScrollView;    // 차량 인스턴스 오브젝트 리스트 스크롤뷰
    //[SerializeField] Button m_btnReload;

    [Header("차량오브젝트 정보")]
    [SerializeField] InputField m_inCarObjId;       // 오브젝트 차량 인덱스
    [SerializeField] InputField m_inPresetId;       // 프리셋 id
    [SerializeField] InputField m_inFaceIdx;        // 면 인덱스
    [SerializeField] InputField m_inRotateY;        // Y축 회전값
    [SerializeField] Button m_btnAddRotateY;        // Y축 회전값 버튼( 90도씩 회전 )
    [SerializeField] Toggle m_toFrontFace;          // 정면
    [SerializeField] Toggle m_toBackFace;           // 후면
    [SerializeField] Button m_btnRepair;            // 수정


    [Header("차량배치")]
    [SerializeField] Button m_btnDeleteCar;         // 선택된 차 obj 삭제
    [SerializeField] Button m_btnPlacementStart;    // 배치 시작

    [Header("자동배치")]
    [SerializeField] Button m_btnAutoCreate;            // 자동 배치
    [SerializeField] InputField m_InCreateCarNum;       // 자동으로 생성할 차량수
    [SerializeField] InputField m_InCreateSpaceWidth;   // 자동으로 생성할 차량 간격
    [SerializeField] Toggle m_toHeightDiposit;          // 세로배치 여부

    [Space]
    [SerializeField] Toggle m_toggleMove;        // 이동 플래그 Toggle
    [SerializeField] Toggle m_toggleRotate;      // 회전 플래그 Toggle
    [SerializeField] Toggle m_togglePresetGroup; // 프리셋 그룹 플래그 Toggle
    [SerializeField] Text m_txtCarObjId;         // 배치된 사량오브젝트 id
    [SerializeField] InputField m_inRotateTerm;  // 회전폭

    [Space]
    //public InputField m_editFileName;
    public float m_MoveSpeed = 1;
    public const float DEFAULT_SPEED = 15;

    [Header("선택 하이라이트")]
    [SerializeField] CCarSelectHighlight.EHighlightMode m_HighlightMode = CCarSelectHighlight.EHighlightMode.Outline;
    [SerializeField] Color m_SolidSelectColor = new Color(0f, 0.85f, 1f, 1f);
    [SerializeField] Color m_OutlineColor     = Color.yellow;
    [SerializeField] [Range(1.01f, 1.15f)] float m_OutlineScale = 1.05f;
    // 모드 전환 드롭다운 (Inspector에서 연결, 없어도 동작)
    [SerializeField] Dropdown m_cboHighlightMode;

    [HideInInspector]
    public Camera m_CurCamera = null;          // 현재 사용중인 카메라
    GameObject m_SelectdCarObject = null;      // 선택된 차량 오브젝트 인스턴스
    CCarSelectHighlight m_CurHighlight = null; // 현재 선택된 차량의 하이라이트 컴포넌트
    CItemText m_CurItemUI = null;
    int m_iCurPrefabId = 1;                 // 선택된 프리팹 id

    // ── 다중 선택 ────────────────────────────────────────────────────
    List<CObjCar>   m_MultiSelectedCars  = new List<CObjCar>();
    List<CItemText> m_MultiSelectedItems = new List<CItemText>();
    public int MultiSelectedCount => m_MultiSelectedCars.Count;
    // Shift+클릭 범위 선택의 기준이 되는 앵커(마지막 단일/Ctrl 클릭한 항목의 sibling index)
    int m_AnchorIdx = -1;

    EKeyType m_KeyType = EKeyType.None;              //없음= 0, 이동 = 1, 회전 = 2
    
    //bool m_bPickingState = false;    // 차량 배치할수 있는 상태 여부

    Vector3 m_vOffsetAutoCarPos = new Vector3(0, 0, 0); // 차량 자동 배치시 첫번째 차 위치
    string m_DefaultFileName = "fileName";

    void Start()
    {
        m_cboCarType.onValueChanged.AddListener(OnValueChanged_CarType);
        m_cboCarPrefabs.onValueChanged.AddListener(OnValueChanged_CarPrefabs);

        //m_btnReload.onClick.AddListener(OnClicked_Reload);
        
        m_btnSave.onClick.AddListener(OnClicked_Save);
        m_btnLoad.onClick.AddListener(OnClicked_Load);
        m_btnClear.onClick.AddListener(OnClicked_Clear);
        m_btnExit.onClick.AddListener(OnClicked_Exit);

        m_btnPlacementStart.onClick.AddListener(OnClicked_PlacementStart);
        m_btnDeleteCar.onClick.AddListener(OnClicked_DeleteCar);
        m_btnAutoCreate.onClick.AddListener(() => {   OnClicked_AutoCreate(); 
            //int iNum = m_InCreateCarNum.text=="" ? 0 : int.Parse(m_InCreateCarNum.text);
            //int presetId = m_inPresetId.text == "" ? 1 : int.Parse(m_inPresetId.text);
            //m_CarObjListUI.CreateRandomCarsInLine(presetId, iNum, m_vOffsetAutoCarPos, m_CurCamera);
        });

        m_toggleMove.onValueChanged.AddListener(OnValueChanged_Move);
        m_toggleRotate.onValueChanged.AddListener(OnValueChanged_Rotate);

        m_btnRepair.onClick.AddListener(OnClicked_Repair);
        m_btnAddRotateY.onClick.AddListener(OnClicked_AddRotate);

        if (m_cboHighlightMode != null)
        {
            m_cboHighlightMode.ClearOptions();
            m_cboHighlightMode.AddOptions(new System.Collections.Generic.List<string> { "외곽선", "단색" });
            m_cboHighlightMode.value = (int)m_HighlightMode;
            m_cboHighlightMode.onValueChanged.AddListener(OnValueChanged_HighlightMode);
        }

        Invoke("Callback_DealyStart", 0.1f);
    }

    public void Initialize()
    {
        if(m_CurCamera == null)
            m_CurCamera = Camera.main;

        InitDropdown_CarType();
        InitDropdown_CarPrefabs();

        

        m_CarObjListUI.Initialize(m_CurCamera);
        //m_CarObjListUI.Reset_CreateCarObjectList();

        //m_CurCarPrefab = m_CarObjListUI.GetCarPrefab(0);
        m_KeyType = EKeyType.Move;
    }

    void Callback_DealyStart()
    {
        if (m_CurCamera == null)
            m_CurCamera = Camera.main;

        InitDropdown_CarType();
        InitDropdown_CarPrefabs();

        //InitScrollView_CarObject();
        m_CarObjListUI.Initialize(m_CurCamera);
        //m_CurCarPrefab = m_CarObjListUI.GetCarPrefab(0);
        m_KeyType = EKeyType.Move;
    }

    void OnDestroy()
    {
        RemoveAll_ScrollItems();
        m_CarObjListUI?.RemoveAll();
    }


    void OnValueChanged_CarType(int nPos)
    {
        //m_CurCarPrefab = m_CarObjListUI.GetCarPrefab(nPos);
        //m_inCarPrefabID.text = nPos.ToString();
    }

    void OnValueChanged_CarPrefabs(int nPos)
    {
        GameObject goPrefab = m_CarObjListUI.GetCarPrefab(nPos);
        m_CarObjListUI.SetCurPrefab(nPos);

        if (goPrefab == null || m_CarObjListUI.m_ScoCarData == null)
            return;

        // m_PrefabList 인덱스 순서와 SCarData.m_Idx 값은 일치하지 않을 수 있다.
        // (인스펙터에서 m_PrefabList 를 직접 셋팅하고, ScriptableObject 의 m_Idx 에
        //  누락/중복이 존재해 nPos+1 == m_Idx 가정이 깨짐.)
        // → 프리팹 이름으로 SCarData 를 찾는다.
        string prefabName = goPrefab.name;
        CScoCarData.SCarData kCarData =
            m_CarObjListUI.m_ScoCarData.m_Datas.Find(x => x.m_PrefabName == prefabName);

        if (kCarData == null)
        {
            Debug.LogWarning($"[CCarPlacementDlg] SCarData not found for prefab '{prefabName}'. CarType dropdown not updated.");
            return;
        }

        m_iCurPrefabId = kCarData.m_Idx;

        // 드롭다운은 ECarType.Small(1) ~ Max-1 을 0-based 로 보여주므로
        // None(0) 등 유효하지 않은 타입은 무시한다.
        int typeIdx = (int)kCarData.m_Type - 1;
        if (typeIdx >= 0 && typeIdx < m_cboCarType.options.Count)
            m_cboCarType.value = typeIdx;
    }


    void Update()
    {
        if(CDataMgr.Inst.IsCarPlacementState())  //(m_bPickingState)
        {
            CheckPickingPositionByMouse();
            Update_CarMoveRotate();
            Update_CarRotateByScroll();
        }
    }

    /// <summary>
    /// 마우스 휠 스크롤로 선택 차량(또는 같은 프리셋 그룹) 을 회전.
    /// - 회전 토글(`m_toggleRotate`) ON 일 때만 동작
    /// - 회전 폭은 `m_inRotateTerm` (비어있거나 파싱 실패 시 기본 5도)
    /// - Shift/Ctrl/Alt 가 눌리면 카메라 컨트롤에 양보
    /// - UI 위에서는 스크롤뷰 등과 충돌 방지 위해 무시
    /// </summary>
    void Update_CarRotateByScroll()
    {
        if (m_KeyType != EKeyType.Rot) return;
        if (m_MultiSelectedCars.Count == 0) return;

        if (Input.GetKey(KeyCode.LeftControl) || Input.GetKey(KeyCode.LeftAlt) || Input.GetKey(KeyCode.LeftShift))
            return;

        if (EventSystem.current != null && EventSystem.current.IsPointerOverGameObject())
            return;

        float scroll = Input.mouseScrollDelta.y;
        if (Mathf.Approximately(scroll, 0f)) return;

        float step = ParseRotateTermDeg();
        if (step <= 0f) return;

        float delta = Mathf.Sign(scroll) * step;

        bool bPresetGroup = m_togglePresetGroup != null && m_togglePresetGroup.isOn;
        CObjCar anchorCar;
        List<CObjCar> targets;
        if (bPresetGroup && m_SelectdCarObject != null)
        {
            anchorCar = m_SelectdCarObject.GetComponent<CObjCar>();
            targets   = GetSamePresetCars();
        }
        else
        {
            anchorCar = m_MultiSelectedCars[0];
            targets   = m_MultiSelectedCars;
        }

        if (anchorCar == null || targets == null || targets.Count == 0) return;

        foreach (CObjCar car in targets)
        {
            float angle = car.transform.localEulerAngles.y + delta;
            angle %= 360f;
            if (angle < 0f) angle += 360f;

            car.transform.localRotation = Quaternion.Euler(0, angle, 0);
            SCarPos pos = CDataMgr.Inst.m_SaveCarPosData.GetObjectPosDataById(car.m_NameId);
            if (pos != null) pos.rotY = angle;
        }

        m_inRotateY.text = string.Format("{0:N2}", anchorCar.transform.localEulerAngles.y);
    }

    /// <summary>m_inRotateTerm 파싱. 비어있거나 잘못된 값이면 기본 5도 반환.</summary>
    float ParseRotateTermDeg()
    {
        const float defaultStep = 5f;
        if (m_inRotateTerm == null || string.IsNullOrEmpty(m_inRotateTerm.text))
            return defaultStep;

        return float.TryParse(m_inRotateTerm.text, out float v) ? Mathf.Abs(v) : defaultStep;
    }

    void Update_CarMoveRotate()
    {
        if (m_SelectdCarObject == null) return;
        if (Input.GetKey(KeyCode.LeftControl) || Input.GetKey(KeyCode.LeftAlt) || Input.GetKey(KeyCode.LeftShift))
            return;

        float xDelta = Input.GetAxis("Horizontal");
        float zDelta = Input.GetAxis("Vertical");

        xDelta *= (m_MoveSpeed * DEFAULT_SPEED * 0.5f * Time.deltaTime);
        zDelta *= (m_MoveSpeed * DEFAULT_SPEED * 0.5f * Time.deltaTime);

        bool bPresetGroup = m_togglePresetGroup != null && m_togglePresetGroup.isOn;

        if (m_KeyType == EKeyType.Move)
        {
            // 화면 기준 이동: 카메라의 right/forward를 XZ 평면에 투영하여 이동 방향 결정
            Camera cam = m_CurCamera != null ? m_CurCamera : Camera.main;
            Vector3 camRight   = cam.transform.right;   camRight.y   = 0; camRight.Normalize();
            Vector3 camForward = cam.transform.forward; camForward.y = 0; camForward.Normalize();
            Vector3 v = camRight * xDelta + camForward * zDelta;

            if (bPresetGroup)
            {
                foreach (CObjCar car in GetSamePresetCars())
                {
                    car.transform.Translate(v, Space.World);
                    SCarPos kCarPos = CDataMgr.Inst.m_SaveCarPosData.GetObjectPosDataById(car.m_NameId);
                    if (kCarPos != null)
                        kCarPos.pos.Set(car.transform.position);
                }
            }
            else
            {
                foreach (CObjCar car in m_MultiSelectedCars)
                {
                    car.transform.Translate(v, Space.World);
                    SCarPos kCarPos = CDataMgr.Inst.m_SaveCarPosData.GetObjectPosDataById(car.m_NameId);
                    if (kCarPos != null)
                        kCarPos.pos.Set(car.transform.position);
                }
            }
        }
        else if (m_KeyType == EKeyType.Rot)
        {
            if (bPresetGroup)
            {
                foreach (CObjCar car in GetSamePresetCars())
                {
                    float fAngle = car.transform.localEulerAngles.y + (xDelta * 10);
                    car.transform.localRotation = Quaternion.Euler(0, fAngle, 0);
                    SCarPos kCarPos = CDataMgr.Inst.m_SaveCarPosData.GetObjectPosDataById(car.m_NameId);
                    if (kCarPos != null)
                        kCarPos.rotY = fAngle;
                }
            }
            else
            {
                foreach (CObjCar car in m_MultiSelectedCars)
                {
                    float fAngle = car.transform.localEulerAngles.y + (xDelta * 10);
                    car.transform.localRotation = Quaternion.Euler(0, fAngle, 0);
                    SCarPos kCarPos = CDataMgr.Inst.m_SaveCarPosData.GetObjectPosDataById(car.m_NameId);
                    if (kCarPos != null)
                        kCarPos.rotY = fAngle;
                }
            }
        }

        if (Input.GetKeyDown(KeyCode.T))
        {
            if (bPresetGroup)
            {
                foreach (CObjCar car in GetSamePresetCars())
                {
                    float fAngle = car.transform.localEulerAngles.y + 15;
                    car.transform.localRotation = Quaternion.Euler(0, fAngle, 0);
                    SCarPos kCarPos = CDataMgr.Inst.m_SaveCarPosData.GetObjectPosDataById(car.m_NameId);
                    if (kCarPos != null)
                        kCarPos.rotY = fAngle;
                }
            }
            else
            {
                foreach (CObjCar car in m_MultiSelectedCars)
                {
                    float fAngle = car.transform.localEulerAngles.y + 15;
                    car.transform.localRotation = Quaternion.Euler(0, fAngle, 0);
                }
            }
        }
    }

    /// <summary>
    /// 선택된 차량과 같은 프리셋 ID를 가진 모든 CObjCar를 반환한다.
    /// </summary>
    List<CObjCar> GetSamePresetCars()
    {
        CObjCar selected = m_SelectdCarObject.GetComponent<CObjCar>();
        if (selected == null) return new List<CObjCar>();

        int presetId = selected.m_iPresetId;
        return m_CarObjListUI.m_ObjCarList.FindAll(car => car.m_iPresetId == presetId);
    }

    SCarPos GetCarPosData()
    {
        CObjCar kObjCar = m_SelectdCarObject.GetComponent<CObjCar>();
        //int idx = m_CarObjListUI.m_ObjCarList.FindIndex(x => x.m_NameId == kObjCar.m_NameId);

        SCarPos kCarPos = CDataMgr.Inst.m_SaveCarPosData.GetObjectPosDataById(kObjCar.m_NameId);
        return kCarPos;
    }

    void InitDropdown_CarType()
    {
        m_cboCarType.ClearOptions();

        int carTypeCount = (int)ECarType.Max;
        for( int i = 0; i < carTypeCount; i++)
        {
            string sTypeName = CSaveCarPosData.GetCarTypeName(i+1);
            m_cboCarType.options.Add(new Dropdown.OptionData(sTypeName));
        }
        m_cboCarType.value = 0;

        //var kDatas = m_CarObjListUI.m_ScoCarData.m_Datas;

        //List<string> strDatas = new List<string>();
        //for (int i = 0; i < kDatas.Count; i++)
        //{

        //    CScoCarData.SCarData data = kDatas[i];
        //    strDatas.Add(data.m_PrefabName);
        //}
        //m_cboCarType.AddOptions(strDatas);
    }

    protected void InitDropdown_CarPrefabs()
    {
        var kPrefabList= m_CarObjListUI.m_PrefabList;
        List<string> strDatas = new List<string>();
       
        for (int i = 0; i < kPrefabList.Count; i++)
        {
            string sName = kPrefabList[i].name;
            strDatas.Add(sName);
        }
        m_cboCarPrefabs.AddOptions(strDatas);

        m_cboCarPrefabs.value = 0;
        m_CarObjListUI.SetCurPrefab(0);

        m_cboCarType.value = 1;
    }

    void InitScrollView_CarObject()
    {
        Reset_CreateScrollViewCarItem();
        m_KeyType = EKeyType.Move;
    }

   


    void Reset_CreateScrollViewCarItem()
    {
        RemoveAll_ScrollItems();

        var kDatas = m_CarObjListUI.m_ObjCarList;
        for (int i = 0; i < kDatas.Count; i++)
        {
            CObjCar kObj = kDatas[i];

            Create_ScvCarItemUI(kObj.m_NameId, i);
        }
    }

    CItemText Create_ScvCarItemUI(string sCarObjName, int idx)
    {
        GameObject go = Instantiate(m_PrefabItemText, m_CarScrollView.content);
        go.transform.localPosition = Vector3.zero;

        CItemText kItem = go.GetComponent<CItemText>();
        kItem.Initialize(sCarObjName, idx);
        Button btn = kItem.GetComponent<Button>();
        int index = idx;

        btn.onClick.AddListener(() => {
            OnSelected_CarObjItem(kItem, index);
        });
        return kItem;
    }

    void Reset_CreateScrollViewCarItem2()
    {
        RemoveAll_ScrollItems();

        var kDatas = m_CarObjListUI.m_ScoCarData.m_Datas;
        List<string> strDatas = new List<string>();
        for (int i = 0; i < kDatas.Count; i++)
        {
            CScoCarData.SCarData data = kDatas[i];
            Create_ScvCarItemUI2(data, i);
        }
    }

    void Create_ScvCarItemUI2(CScoCarData.SCarData kCarData, int idx)
    {
        GameObject go = Instantiate(m_PrefabItemText, m_CarScrollView.content);
        go.transform.localPosition = Vector3.zero;

        CItemText kItem = go.GetComponent<CItemText>();
        string sName = kCarData.m_PrefabName; // .m_Name;
        kItem.Initialize(sName, kCarData.m_Idx);
        Button btn = kItem.GetComponent<Button>();
        int index = idx;

        btn.onClick.AddListener(() => {
            OnSelected_CarObjItem(kItem, index);
        });
    }


    // 로딩후 처음 한번만 강제로 인덱스를 사용한다.
    // bForceSingleSelect=true 이면 Ctrl/Shift 키 상태를 무시하고 무조건 단일 선택으로 처리한다.
    // (차량 신규 추가 시 Ctrl 키가 눌린 상태에서도 누적 선택되지 않도록 하기 위함)
    void OnSelected_CarObjItem(CItemText kItem, int idx, bool bUseForceIndex = false, bool bForceSingleSelect = false)
    {
        SCarPos kCarPos = null;
        if (bUseForceIndex)
            kCarPos = CDataMgr.Inst.m_SaveCarPosData.GetObjectPosData(idx);
        else
            kCarPos = CDataMgr.Inst.m_SaveCarPosData.GetObjectPosDataById(kItem.m_TxtName.text);

        if (kCarPos == null)
            return;

        CObjCar kObjCar = m_CarObjListUI.m_ObjCarList.Find(x => x.m_NameId == kCarPos.id);

        // 현재 스크롤뷰에서의 실제 표시 위치(삭제 등으로 인덱스가 어긋날 수 있어 sibling 사용)
        int siblingIdx = kItem.transform.GetSiblingIndex();

        bool bShift = !bForceSingleSelect && (Input.GetKey(KeyCode.LeftShift)   || Input.GetKey(KeyCode.RightShift));
        bool bCtrl  = !bForceSingleSelect && (Input.GetKey(KeyCode.LeftControl) || Input.GetKey(KeyCode.RightControl));

        if (bShift && m_AnchorIdx >= 0)
        {
            // Shift+클릭: 앵커 ~ 현재 클릭 위치 사이를 모두 선택
            SelectRange(m_AnchorIdx, siblingIdx);
            if (kObjCar != null)
            {
                FromSaveData(kCarPos);
                CDataMgr.Inst.pmakerScene.SetSelectTargetObject(m_SelectdCarObject);
            }
        }
        else if (bCtrl && kObjCar != null)
        {
            // Ctrl+클릭: 개별 항목 토글 (앵커 갱신)
            ToggleCarInSelection(kObjCar, kItem);
            m_AnchorIdx = siblingIdx;
            if (m_SelectdCarObject == kObjCar.gameObject)
            {
                FromSaveData(kCarPos);
                CDataMgr.Inst.pmakerScene.SetSelectTargetObject(m_SelectdCarObject);
            }
        }
        else
        {
            // 단일 클릭(또는 앵커가 없는 Shift+클릭): 새로 선택, 앵커 설정
            ClearMultiSelection();
            m_AnchorIdx = siblingIdx;
            if (kObjCar != null)
            {
                AddToSelection(kObjCar, kItem);
                CDataMgr.Inst.pmakerScene.SetSelectTargetObject(m_SelectdCarObject);
                FromSaveData(kCarPos);
            }
            else
            {
                m_CurItemUI = kItem;
                kItem.SetSelectd(true);
            }
        }
    }

    /// <summary>
    /// 스크롤뷰 sibling 인덱스 기준으로 anchorIdx와 clickedIdx 사이의 모든 항목을 선택한다.
    /// 클릭한 항목이 마지막에 추가되어 primary(m_SelectdCarObject)가 되도록 한다.
    /// </summary>
    void SelectRange(int anchorIdx, int clickedIdx)
    {
        int childCount = m_CarScrollView.content.childCount;
        if (childCount == 0) return;

        int min = Mathf.Clamp(Mathf.Min(anchorIdx, clickedIdx), 0, childCount - 1);
        int max = Mathf.Clamp(Mathf.Max(anchorIdx, clickedIdx), 0, childCount - 1);

        ClearMultiSelection();

        if (clickedIdx >= anchorIdx)
        {
            for (int i = min; i <= max; i++) AddRangeItemAt(i);
        }
        else
        {
            for (int i = max; i >= min; i--) AddRangeItemAt(i);
        }

        // Shift+클릭은 앵커를 유지한다 (다음 Shift+클릭의 기준점)
        m_AnchorIdx = anchorIdx;
    }

    void AddRangeItemAt(int siblingIdx)
    {
        Transform tr = m_CarScrollView.content.GetChild(siblingIdx);
        if (tr == null) return;
        CItemText kItem = tr.GetComponent<CItemText>();
        if (kItem == null || kItem.m_TxtName == null) return;

        SCarPos kCarPos = CDataMgr.Inst.m_SaveCarPosData.GetObjectPosDataById(kItem.m_TxtName.text);
        if (kCarPos == null) return;

        CObjCar kObjCar = m_CarObjListUI.m_ObjCarList.Find(x => x.m_NameId == kCarPos.id);
        if (kObjCar != null) AddToSelection(kObjCar, kItem);
    }

    private void FromSaveData(SCarPos kCarPos)
    {
        m_inCarObjId.text = kCarPos.id;
        m_inFaceIdx.text = kCarPos.slotId.ToString();
        m_inPresetId.text = kCarPos.presetId.ToString();
        m_inRotateY.text = string.Format("{0:N2}", kCarPos.rotY);
        m_toFrontFace.isOn = kCarPos.isFront;
        m_toBackFace.isOn = !kCarPos.isFront;
        m_txtCarObjId.text = kCarPos.id;
        m_cboCarType.value = kCarPos.type - 1;
        m_cboCarPrefabs.value = kCarPos.prefabId - 1;
    }

    public void AddCarObject(Vector3 vPos)
    {
        if( m_CarObjListUI.m_CurCarPrefab == null)
        {
            Debug.LogWarning("Car Prefab is null !!!");
            return;
        }

        int iCount = m_CarObjListUI.m_ObjCarList.Count;
        int presetId = (m_inPresetId.text == "-1" || m_inPresetId.text == "") ? 1 : int.Parse(m_inPresetId.text);
        int startSlotId = (m_inFaceIdx.text == "-1" || m_inFaceIdx.text == "") ? 1 : int.Parse(m_inFaceIdx.text);

        //string sTime = DateTime.Now.ToString(("HH.mm.ss"));

        SCarPos kCarPos = CDataMgr.Inst.AddCarPosData(vPos, 0, m_iCurPrefabId, iCount);

        kCarPos.presetId = presetId;
        kCarPos.slotId = startSlotId+1;

        if (m_CurCamera == null)
            m_CurCamera = Camera.main;

        m_CarObjListUI.CreateCarObject(vPos, kCarPos, m_CurCamera);

        m_txtCarObjId.text = kCarPos.id;

        CItemText kItemText = Create_ScvCarItemUI(kCarPos.id, iCount);
        // Ctrl 키를 누른 상태로 차량을 추가하더라도 누적 선택되지 않도록
        // 단일 선택을 강제한다. (마지막 추가된 아이템만 선택됨)
        OnSelected_CarObjItem(kItemText, iCount, false, true);

        // 새로 추가된 아이템이 스크롤뷰 화면에 보이도록 끝으로 스크롤
        ScrollToLastItem();
    }

    /// <summary>
    /// 스크롤뷰를 마지막 아이템 위치로 이동시킨다.
    /// 수직/수평 방향 모두 지원하며, 호출 직전에 추가된 아이템의
    /// Layout 이 반영되도록 ForceUpdateCanvases 를 먼저 수행한다.
    /// </summary>
    void ScrollToLastItem()
    {
        if (m_CarScrollView == null) return;

        Canvas.ForceUpdateCanvases();
        if (m_CarScrollView.vertical)
            m_CarScrollView.verticalNormalizedPosition = 0f;     // 수직: 0 = 최하단
        if (m_CarScrollView.horizontal)
            m_CarScrollView.horizontalNormalizedPosition = 1f;   // 수평: 1 = 최우측
    }

    private float m_fRotateY = 0;
    void OnClicked_AddRotate()
    {
        if (m_MultiSelectedCars.Count == 0)
        {
            m_fRotateY = 0;
            m_inRotateY.text = string.Format("{0:N2}", m_fRotateY);
            return;
        }

        bool bPresetGroup = m_togglePresetGroup != null && m_togglePresetGroup.isOn;

        // 프리셋 그룹 토글 ON: 선택된 차량과 같은 프리셋 ID를 가진 차량들에 회전 적용
        // 토글 OFF: 기존대로 다중 선택된 차량들에 회전 적용
        CObjCar anchorCar;
        List<CObjCar> targets;
        if (bPresetGroup && m_SelectdCarObject != null)
        {
            anchorCar = m_SelectdCarObject.GetComponent<CObjCar>();
            targets   = GetSamePresetCars();
        }
        else
        {
            anchorCar = m_MultiSelectedCars[0];
            targets   = m_MultiSelectedCars;
        }

        if (anchorCar == null || targets == null || targets.Count == 0)
            return;

        m_fRotateY = anchorCar.transform.localEulerAngles.y;

        m_fRotateY += 90;
        if (m_fRotateY >= 360)
            m_fRotateY -= 360;

        float rotY = m_fRotateY;
        foreach (CObjCar car in targets)
        {
            car.transform.localRotation = Quaternion.Euler(0, rotY, 0);
            SCarPos pos = CDataMgr.Inst.m_SaveCarPosData.GetObjectPosDataById(car.m_NameId);
            if (pos != null)
            {
                pos.rotY = rotY;
            }
        }

        m_inRotateY.text = string.Format("{0:N2}", m_fRotateY);
    }

    void OnClicked_Repair()
    {
        if (m_CurItemUI == null)
            return;

        string id = m_CurItemUI.m_TxtName.text;
        SCarPos kCarPos = CDataMgr.Inst.m_SaveCarPosData.GetObjectPosDataById(id);
        if (kCarPos == null)
            return;

        ToSaveData(kCarPos);

        // 기본 선택 차량: 모든 필드 적용
        CObjCar primary = m_CarObjListUI.m_ObjCarList.Find(x => x.m_NameId == kCarPos.id);
        if (primary != null)
        {
            primary.transform.localRotation = Quaternion.Euler(0, kCarPos.rotY, 0);
            primary.m_iPresetId   = kCarPos.presetId;
            primary.m_VehicleType = CSaveCarPosData.GetCarTypeName(kCarPos.type);
            primary.m_IsFront     = kCarPos.isFront;
            primary.m_iFaceSlot   = kCarPos.slotId;
        }

        // 다중 선택 나머지 차량: Rot Y 와 front, back 만 적용
        float rotY = kCarPos.rotY;
        bool isFront = m_toFrontFace.isOn;

        foreach (CObjCar car in m_MultiSelectedCars)
        {
            if (car == primary) continue;
            car.transform.localRotation = Quaternion.Euler(0, rotY, 0);
            SCarPos pos = CDataMgr.Inst.m_SaveCarPosData.GetObjectPosDataById(car.m_NameId);
            if (pos != null)
            {
                pos.rotY = rotY;
                pos.isFront = isFront;
            }
        }
    }

    private void ToSaveData(SCarPos kCarPos)
    {
        kCarPos.rotY = float.Parse(m_inRotateY.text);
        kCarPos.slotId = int.Parse(m_inFaceIdx.text);
        kCarPos.presetId = int.Parse(m_inPresetId.text);
        kCarPos.isFront = m_toFrontFace.isOn;
        kCarPos.type = m_cboCarType.value + 1;
        kCarPos.prefabId = m_cboCarPrefabs.value + 1;
    }



    void OnClicked_Exit()
    {
        CloseUI();
    }

    public override void CloseUI()
    {
        base.CloseUI();

        CDataMgr.Inst.SetNormalState();
    }


    void OnClicked_DeleteCar()
    {
        if (m_MultiSelectedCars.Count == 1 && m_SelectdCarObject != null)
        {
            var kObjCar = m_SelectdCarObject.GetComponent<CObjCar>();
            Debug.Assert(kObjCar != null, "SelectObject is null !!!");
            if (kObjCar != null)
            {
                ClearMultiSelection();

                Delete_ScrollItem(kObjCar.m_NameId);
                CDataMgr.Inst.m_SaveCarPosData.RemoveById(kObjCar.m_NameId);
                m_CarObjListUI.RemoveCarObject(kObjCar);
            }
            return;
        }

        for( int i = 0; i < m_MultiSelectedCars.Count; i++)
        {
            CObjCar car = m_MultiSelectedCars[i];
            if (car != null)
            {
                Delete_ScrollItem(car.m_NameId);
                CDataMgr.Inst.m_SaveCarPosData.RemoveById(car.m_NameId);
                m_CarObjListUI.RemoveCarObject(car);
            }
        }
        ClearMultiSelection();
    }

    void OnValueChanged_Move(bool isOn)
    {
        m_KeyType = EKeyType.Move;
    }
    void OnValueChanged_Rotate(bool isOn)
    {
        m_KeyType = EKeyType.Rot;
    }

    void OnValueChanged_HighlightMode(int idx)
    {
        m_HighlightMode = (CCarSelectHighlight.EHighlightMode)idx;
        // 현재 선택된 차량에 즉시 모드 반영
        m_CurHighlight?.SetMode(m_HighlightMode);
    }

    // 차량 선택: 이전 선택 해제 → 새 차량 단일 선택
    void ApplyCarSelection(CObjCar kObjCar)
    {
        ClearMultiSelection();
        if (kObjCar != null)
            AddToSelection(kObjCar, null);
    }

    void ClearMultiSelection()
    {
        foreach (CObjCar car in m_MultiSelectedCars)
        {
            CCarSelectHighlight hl = GetHighlight(car);
            if (hl != null) hl.SetSelected(false);
        }
        foreach (CItemText item in m_MultiSelectedItems)
        {
            if (item != null) item.SetSelectd(false);
        }
        m_MultiSelectedCars.Clear();
        m_MultiSelectedItems.Clear();
        m_SelectdCarObject = null;
        m_CurHighlight     = null;
        m_CurItemUI        = null;
        m_AnchorIdx        = -1;
    }

    void AddToSelection(CObjCar kObjCar, CItemText kItem)
    {
        if (kObjCar == null || m_MultiSelectedCars.Contains(kObjCar)) return;

        m_MultiSelectedCars.Add(kObjCar);
        m_MultiSelectedItems.Add(kItem);
        m_SelectdCarObject = kObjCar.gameObject;

        CCarSelectHighlight hl = GetHighlight(kObjCar);
        if (hl != null)
        {
            hl.m_Mode             = m_HighlightMode;
            hl.m_SolidSelectColor = m_SolidSelectColor;
            hl.m_OutlineColor     = m_OutlineColor;
            hl.m_OutlineScale     = m_OutlineScale;
            hl.SetSelected(true);
            m_CurHighlight = hl;
        }

        if (kItem != null)
        {
            m_CurItemUI = kItem;
            kItem.SetSelectd(true);
        }
    }

    void RemoveFromSelection(CObjCar kObjCar)
    {
        int idx = m_MultiSelectedCars.IndexOf(kObjCar);
        if (idx < 0) return;

        CCarSelectHighlight hl = GetHighlight(kObjCar);
        if (hl != null) hl.SetSelected(false);

        m_MultiSelectedCars.RemoveAt(idx);
        if (idx < m_MultiSelectedItems.Count)
        {
            if (m_MultiSelectedItems[idx] != null)
                m_MultiSelectedItems[idx].SetSelectd(false);
            m_MultiSelectedItems.RemoveAt(idx);
        }

        int count = m_MultiSelectedCars.Count;
        if (count > 0)
        {
            CObjCar last = m_MultiSelectedCars[count - 1];
            m_SelectdCarObject = last.gameObject;
            m_CurHighlight     = GetHighlight(last);
            m_CurItemUI        = (m_MultiSelectedItems.Count > 0)
                               ? m_MultiSelectedItems[m_MultiSelectedItems.Count - 1]
                               : null;
        }
        else
        {
            m_SelectdCarObject = null;
            m_CurHighlight     = null;
            m_CurItemUI        = null;
        }
    }

    void ToggleCarInSelection(CObjCar kObjCar, CItemText kItem)
    {
        if (m_MultiSelectedCars.Contains(kObjCar))
            RemoveFromSelection(kObjCar);
        else
            AddToSelection(kObjCar, kItem);
    }

    CCarSelectHighlight GetHighlight(CObjCar car)
    {
        if (car == null) return null;
        return car.m_SelectHighlight != null
             ? car.m_SelectHighlight
             : car.GetComponent<CCarSelectHighlight>();
    }

    CItemText FindScrollItemByNameId(string nameId)
    {
        for (int i = 0; i < m_CarScrollView.content.childCount; i++)
        {
            Transform tr = m_CarScrollView.content.GetChild(i);
            if (tr == null) continue;
            CItemText item = tr.GetComponent<CItemText>();
            if (item != null && item.m_TxtName.text == nameId)
                return item;
        }
        return null;
    }

    void OnClicked_Save()
    {
        string pathName = StandaloneFileSave_Json();
        if (pathName == "") return;
        Debug.Log(pathName);
        //string sFileName = m_editFileName.text;
        string sFileName = Path.GetFileName(pathName);
        m_DefaultFileName = sFileName;

        CDataMgr.Inst.SaveCarPosFile(CDataMgr.DSAVE_PATH_CARPOS, pathName);
    }
    void OnClicked_Load()
    {
        string pathName = StandaloneFileOpen_Json();
        if (pathName == "") return;
        Debug.Log(pathName);

        if (m_CurCamera == null)
            m_CurCamera = Camera.main;

        string sFileName = Path.GetFileName(pathName);
        m_DefaultFileName = sFileName;
        if( m_txtFileName != null)
            m_txtFileName.text = sFileName;
        //string sFileName = m_editFileName.text;
        //if (CDataMgr.Inst.LoadCarPosFile(CDataMgr.DSAVE_PATH_CARPOS, sFileName))
        if (CDataMgr.Inst.LoadCarPosFileFromDataMgr(pathName))
        {
            ClearMultiSelection();
            Reset_CreateScrollViewCarItem();
            m_CarObjListUI.Reset_CreateCarObjectList(m_CurCamera, false);


            // 차량 스크롤뷰 추가
            RemoveAll_ScrollItems();
            var kDatas = CDataMgr.Inst.m_SaveCarPosData.m_Data.datas;
            for (int i = 0; i < kDatas.Count; i++)
            {
                SCarPos kCarPos = kDatas[i];
                Create_ScvCarItemUI(kCarPos.id, i);
            }
            if( kDatas.Count > 0)
                OnSelected_CarObjItem(m_CarScrollView.content.GetChild(0).GetComponent<CItemText>(), 0, true);
        }
    }

    void OnClicked_Clear()
    {
        ClearMultiSelection();
        CDataMgr.Inst.m_SaveCarPosData.Clear();
        m_CarObjListUI.RemoveAll();
        RemoveAll_ScrollItems();
        m_fRotateY = 0;
    }

    void OnClicked_PlacementStart()
    {
        if (!CDataMgr.Inst.IsCarPlacementState())
            CDataMgr.Inst.SetCarPlacementState();
        else
            CDataMgr.Inst.SetNormalState(); 
    }

    void OnClicked_AutoCreate()
    {
        int iNum = m_InCreateCarNum.text == "" ? 0 : int.Parse(m_InCreateCarNum.text);
        int presetId = (m_inPresetId.text == "-1" || m_inPresetId.text == "") ? 1 : int.Parse(m_inPresetId.text);
        int startSlotId = (m_inFaceIdx.text == "-1" || m_inFaceIdx.text == "") ? 1 : int.Parse(m_inFaceIdx.text);
        float spacing = (m_InCreateSpaceWidth.text == "") ? 2.5f : float.Parse(m_InCreateSpaceWidth.text);
        bool bVertical = m_toHeightDiposit != null && m_toHeightDiposit.isOn;

        // 기준차량(선택된 차량)의 위치·right 벡터·회전각. 없으면 기본값
        // 선택 차량이 있으면 그 차량 위치를 기준점으로 옆에 배치, 없으면 마지막 클릭 위치 사용.
        Vector3 basePos  = m_vOffsetAutoCarPos;
        Vector3 rightDir = Vector3.right;
        float   refRotY  = 180f;
        if (m_SelectdCarObject != null)
        {
            basePos = m_SelectdCarObject.transform.position;

            Vector3 dir = m_SelectdCarObject.transform.right;
            dir.y = 0;
            if (dir.sqrMagnitude > 0.001f)
                rightDir = dir.normalized;

            refRotY = m_SelectdCarObject.transform.eulerAngles.y;
        }

        var kDatas = CDataMgr.Inst.m_SaveCarPosData.m_Data.datas;
        int curCount = kDatas.Count;
        List<GameObject> kObjectList = m_CarObjListUI.CreateRandomCarsInLine(presetId, iNum, basePos, m_CurCamera, spacing, bVertical, rightDir, refRotY);
        for( int i = 0; i < kObjectList.Count; i++)
        {
            SCarPos kCarPos = kDatas[curCount];
            kCarPos.presetId = presetId;
            kCarPos.slotId = startSlotId + i + 1;
            GameObject go = kObjectList[i];
            CObjCar kObjCar = go.GetComponent<CObjCar>();
            if (kObjCar != null)
            {
                Create_ScvCarItemUI(kCarPos.id, curCount);
            }
            curCount++;
        }

    }

    public void Enter_CarPlacement()
    {
        Text kText = m_btnPlacementStart.GetComponentInChildren<Text>();
        kText.text = "배치 중...";
        kText.color = Color.red;
    }

    public void Exit_CarPlacement()
    {
        Text kText = m_btnPlacementStart.GetComponentInChildren<Text>();
        kText.text = "배치 시작";
        kText.color = Color.black;
    }


    public void RemoveAll_ScrollItems()
    {
        for (int i = m_CarScrollView.content.childCount - 1; i >= 0; i--)
        {
            Transform tr = m_CarScrollView.content.GetChild(i);
            if (tr != null)
            {
                Destroy(tr.gameObject);
            }
        }
    }

    // 스크롤뷰 아이템 삭제
    public void Delete_ScrollItem(string sCarObjId)
    {
        for (int i = m_CarScrollView.content.childCount - 1; i >= 0; i--)
        {
            Transform tr = m_CarScrollView.content.GetChild(i);
            if (tr != null)
            {
                CItemText kItem = tr.GetComponent<CItemText>();
                if (kItem != null && kItem.m_TxtName.text == sCarObjId)
                {
                    Destroy(tr.gameObject);
                    break;
                }
            }
        }
    }


    public LayerMask m_SelectableLayer; // 선택할 오브젝트가 속한 레이어

    public void CheckPickingPositionByMouse()
    {
        if (EventSystem.current != null && EventSystem.current.IsPointerOverGameObject())
            return;

        if (Input.GetKey(KeyCode.LeftControl))
        {
            if (Input.GetMouseButtonDown(0)) // 마우스 왼쪽 버튼 클릭
            {
                Vector3 vMouse = Input.mousePosition;
                Ray ray = Camera.main.ScreenPointToRay(vMouse);
                RaycastHit hit;

                if (Physics.Raycast(ray, out hit, Mathf.Infinity, m_SelectableLayer))
                {
                    if (hit.collider.tag == "Floor")// || hit.collider.tag == "Road")
                    {
                        AddCarObject(hit.point);
                        m_vOffsetAutoCarPos = hit.point;
                    }
                }
            }
        }
        else
        {
            if (Input.GetMouseButtonDown(0)) // 마우스 왼쪽 버튼 클릭
            {
                Vector3 vMouse = Input.mousePosition;
                Ray ray = Camera.main.ScreenPointToRay(vMouse);
                RaycastHit hit;

                int kLayerMask = 1 << LayerMask.NameToLayer("Car");
                //int layerMask = LayerMask.GetMask("Layer1");

                if (Physics.Raycast(ray, out hit, Mathf.Infinity, kLayerMask))
                {
                    if (hit.collider.tag == "Car")
                    {
                        CObjCar kObj = hit.collider.GetComponentInParent<CObjCar>()
                                    ?? hit.collider.GetComponent<CObjCar>();
                        if (kObj != null)
                        {
                            if (Input.GetKey(KeyCode.LeftShift))
                            {
                                CItemText scrollItem = FindScrollItemByNameId(kObj.m_NameId);
                                ToggleCarInSelection(kObj, scrollItem);
                                if (m_SelectdCarObject == kObj.gameObject)
                                {
                                    m_txtCarObjId.text = kObj.m_NameId;
                                    SCarPos kCarPos = CDataMgr.Inst.m_SaveCarPosData.GetObjectPosDataById(kObj.m_NameId);
                                    if (kCarPos != null) FromSaveData(kCarPos);
                                    CDataMgr.Inst.pmakerScene.SetSelectTargetObject(m_SelectdCarObject);
                                }
                            }
                            else
                            {
                                ApplyCarSelection(kObj);
                                CDataMgr.Inst.pmakerScene.SetSelectTargetObject(m_SelectdCarObject);
                                m_txtCarObjId.text = kObj.m_NameId;
                                SelectCarItemByNameId(kObj.m_NameId);
                            }
                        }
                        Debug.Log("Selected Object(CarPlace) : " + hit.collider.gameObject.name);
                    }
                }
            }
        }
    }

    public void SelectCarItemByNameId(string sCarObjId)
    {
        for (int i = 0; i < m_CarScrollView.content.childCount; i++)
        {
            Transform tr = m_CarScrollView.content.GetChild(i);
            if (tr != null)
            {
                CItemText kItem = tr.GetComponent<CItemText>();
                if (kItem != null && kItem.m_TxtName.text == sCarObjId)
                {
                    OnSelected_CarObjItem(kItem, kItem.m_Idx);
                    break;
                }
            }
        }
    }



    public string StandaloneFileOpen_Json()
    {
        string baseDir = CDataMgr.DSAVE_PATH_CARPOS;

        return CMyUtil.StandaloneFileOpen(baseDir, "Json Files", "json");
    }

    public string StandaloneFileSave_Json()
    {
        string baseDir = CDataMgr.DSAVE_PATH_CARPOS;
        string sDefaultFileName = m_DefaultFileName; // m_editFileName.text;

        return CMyUtil.StandaloneFileSave(baseDir, sDefaultFileName, "Json Files", "json");
    }

    ////파일 오픈 다이얼로그
    //public string StandaloneFileOpen(string sFilterName = "Json Files", params string[] exts)
    //{
    //    // Open file with filter
    //    var extensions = new[] {
    //        new ExtensionFilter(sFilterName, exts ),
    //        new ExtensionFilter("All Files", "*" ),
    //    };

    //    var paths = StandaloneFileBrowser.OpenFilePanel("Open File", "", extensions, false);
    //    if (paths == null || paths.Length == 0) return "";

    //    return paths[0];
    //}


    public void OnBeginDrag(PointerEventData data){ }
    public void OnDrag(PointerEventData data){ }
    public void OnEndDrag(PointerEventData data){ }
}
