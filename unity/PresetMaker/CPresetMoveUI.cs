using System;
using System.Collections.Generic;
using System.Diagnostics;
using UnityEngine;
using UnityEngine.UI;
using static CCarPlacementDlg;
using static CPMakerParkSpaceUI;

public class CPresetMoveUI : MonoBehaviour
{
    public enum EKeyType
    {
        None = 0,
        Move = 1,   // 이동
        Rot = 2,    // 회전
    }
    //[SerializeField] Button m_btnFoword;
    //[SerializeField] Button m_btnBackword;
    //[SerializeField] Button m_btnLeft;
    //[SerializeField] Button m_btnRight;

    [SerializeField] Toggle m_toggleMove;       // 이동 플래그 Toggle
    [SerializeField] Toggle m_toggleRotate;     // 회전 플래그 Toggle

    SPresetObjUI m_PresetObjUI;
    SDPresetInfo m_kCurPresetInfo = null;

    // 다중 타깃 목록 (단일 Initialize 경로도 여기에 동기화됨)
    private List<SPresetObjUI> m_TargetObjs  = new List<SPresetObjUI>();
    private List<SDPresetInfo> m_TargetInfos = new List<SDPresetInfo>();


    [Space]
    public float m_MoveSpeed = 1;
    public const float DEFAULT_SPEED = 70;

    public Action<Vector3> onAction_Move = null;
    public Action<Vector3, SDPresetInfo> onAction_Rotate = null;

    EKeyType m_KeyType = EKeyType.Move;              //없음= 0, 이동 = 1, 회전 = 2

    bool m_useUpdate = false;
    public EKeyType KeyType => m_KeyType;
    public bool IsToggleMoveOn => m_toggleMove != null && m_toggleMove.isOn;
    public bool IsToggleRotateOn => m_toggleRotate != null && m_toggleRotate.isOn;

    public void SetPresetObjUI(SPresetObjUI kUI)
    {
        m_PresetObjUI = kUI;
    }

    // Start is called once before the first execution of Update after the MonoBehaviour is created
    void Start()
    {
        m_toggleMove.onValueChanged.AddListener(OnValueChanged_Move);
        m_toggleRotate.onValueChanged.AddListener(OnValueChanged_Rotate);
        //m_btnFoword.onClick.AddListener(OnClicked_Up);
        //m_btnBackword.onClick.AddListener(OnClicked_Down);
        //m_btnLeft.onClick.AddListener(OnClicked_Lef);
        //m_btnRight.onClick.AddListener(OnClicked_Right);
        m_KeyType = EKeyType.Move;
    }

    public void Initialize(SPresetObjUI target, SDPresetInfo kPresetInfo)
    {
        m_PresetObjUI    = target;
        m_kCurPresetInfo = kPresetInfo;

        // 단일 타깃도 다중 경로와 동기화
        m_TargetObjs.Clear();
        m_TargetInfos.Clear();
        if (target != null)    m_TargetObjs.Add(target);
        if (kPresetInfo != null) m_TargetInfos.Add(kPresetInfo);

        if (m_KeyType == EKeyType.None)
            m_KeyType = EKeyType.Move;

        //m_KeyType = eKeyType;
        //if (eKeyType == EKeyType.Rot)
        //{
        //    m_toggleMove.isOn = false;
        //    m_toggleRotate.isOn = true;
        //}
        //else
        //{
        //    m_toggleMove.isOn = true;
        //    m_toggleRotate.isOn = false;
        //}
    }

    /// <summary>
    /// 다중 타깃을 설정한다. primary 가 콜백 기준값으로 사용된다.
    /// </summary>
    public void SetTargets(List<SPresetObjUI> objs, List<SDPresetInfo> infos,
                           SPresetObjUI primaryObj, SDPresetInfo primaryInfo)
    {
        m_TargetObjs.Clear();
        m_TargetInfos.Clear();
        if (objs  != null) m_TargetObjs.AddRange(objs);
        if (infos != null) m_TargetInfos.AddRange(infos);

        m_PresetObjUI    = primaryObj;
        m_kCurPresetInfo = primaryInfo;

        if (m_KeyType == EKeyType.None)
            m_KeyType = EKeyType.Move;
    }

    public void EnterMoveState()
    {
        m_useUpdate = true;
    }
    public void ExitMoveState()
    {
        m_useUpdate = false;
    }

    private void Update()
    {
        HandleSpeedShortcut();
        if(m_useUpdate)
            Update_PresetMoveRotate();
    }

    void HandleSpeedShortcut()
    {
        if (!Input.GetKey(KeyCode.LeftControl)) return;

        if (Input.GetKeyDown(KeyCode.M))
        {
            m_MoveSpeed = Mathf.Min(m_MoveSpeed * 2f, 16f);
            UnityEngine.Debug.Log($"[PresetMove] 이동 속도: {m_MoveSpeed}x");
        }
        else if (Input.GetKeyDown(KeyCode.N))
        {
            m_MoveSpeed = Mathf.Max(m_MoveSpeed * 0.5f, 0.25f);
            UnityEngine.Debug.Log($"[PresetMove] 이동 속도: {m_MoveSpeed}x");
        }
    }


    void OnValueChanged_Move(bool isOn)
    {
        if (isOn) m_KeyType = EKeyType.Move;
    }
    void OnValueChanged_Rotate(bool isOn)
    {
        if (isOn) m_KeyType = EKeyType.Rot;
    }

    void Update_PresetMoveRotate()
    {
        if (Input.GetKey(KeyCode.LeftControl) || Input.GetKey(KeyCode.LeftAlt) || Input.GetKey(KeyCode.LeftShift))
            return;

        float xDelta = Input.GetAxis("Horizontal");
        float zDelta = Input.GetAxis("Vertical");

        //이게 없으면 콜백함수가 키 입력이 없어도 계속 호출됨
        if (Math.Abs(xDelta) <= 0.0000001f && Math.Abs(zDelta) <= 0.0000001f)  
            return;

        float fSpeed = m_MoveSpeed * DEFAULT_SPEED * Time.deltaTime;
        if( m_KeyType == EKeyType.Rot )
            fSpeed *= 3f; // 

        xDelta *= fSpeed;
        zDelta *= fSpeed;

        if (m_KeyType == EKeyType.Move)
        {
            MovePreset(xDelta, zDelta);
        }
        else if (m_KeyType == EKeyType.Rot)
        {
            RotatePreset(xDelta, zDelta);
            //float fAngle = m_SelectdCarObject.transform.localEulerAngles.y;
            //fAngle += (xDelta * 10);
            //m_SelectdCarObject.transform.localRotation = Quaternion.Euler(0, fAngle, 0);
        }
    }

    public void MovePreset(float xDelta, float zDelta)
    {
        if (m_TargetObjs.Count == 0) return;

        Vector3 vDir = new Vector3(xDelta, 0, zDelta);
        for (int t = 0; t < m_TargetObjs.Count; t++)
        {
            SPresetObjUI kObj = m_TargetObjs[t];
            if (kObj == null || kObj.m_listFaceRect.Count == 0) continue;
            for (int i = 0; i < kObj.m_listFaceRect.Count; i++)
                kObj.m_listFaceRect[i].transform.localPosition += vDir * Time.deltaTime * DEFAULT_SPEED;
        }

        // 콜백은 primary 기준 1회
        if (m_PresetObjUI != null && m_PresetObjUI.m_listFaceRect.Count > 0)
            onAction_Move?.Invoke(m_PresetObjUI.m_listFaceRect[0].transform.position);
    }

    public void RotatePreset(float xDelta, float zDelta=0)
    {
        if (m_TargetObjs.Count == 0) return;

        Vector3 vDir = new Vector3(0, xDelta, 0);
        for (int t = 0; t < m_TargetObjs.Count; t++)
        {
            SPresetObjUI kObj = m_TargetObjs[t];
            // info null 체크는 개별 타깃별로 수행 (설계서 B 지침)
            SDPresetInfo kInfo = t < m_TargetInfos.Count ? m_TargetInfos[t] : null;
            if (kObj == null || kObj.m_listFaceRect.Count == 0 || kInfo == null) continue;
            for (int i = 0; i < kObj.m_listFaceRect.Count; i++)
            {
                CFaceRect kFace = kObj.m_listFaceRect[i];
                Vector3 vRot = kFace.transform.localEulerAngles;
                vRot += vDir * Time.deltaTime * DEFAULT_SPEED * m_MoveSpeed;
                kFace.transform.localRotation = Quaternion.Euler(vRot);
            }
        }

        // 콜백은 primary 기준 1회
        if (m_PresetObjUI != null && m_kCurPresetInfo != null && m_PresetObjUI.m_listFaceRect.Count > 0)
            onAction_Rotate?.Invoke(m_PresetObjUI.m_listFaceRect[0].transform.eulerAngles, m_kCurPresetInfo);
    }


    public void MovePreset(Vector3 vTargetPos)
    {
        if (m_PresetObjUI == null) return;
        if (m_PresetObjUI.m_listFaceRect.Count == 0) return;

        CFaceRect kOriRect = m_PresetObjUI.m_listFaceRect[0];
        float xDelta = vTargetPos.x - kOriRect.transform.position.x;
        float zDelta = vTargetPos.z - kOriRect.transform.position.z;

        for (int i = 0; i < m_PresetObjUI.m_listFaceRect.Count; i++)
        {
            CFaceRect kFace = m_PresetObjUI.m_listFaceRect[i];
            Vector3 vDir = new Vector3(xDelta, 0, zDelta);
            kFace.transform.localPosition += vDir;// * Time.deltaTime * DEFAULT_SPEED;
        }

        CFaceRect kRect = m_PresetObjUI.m_listFaceRect[0];
        onAction_Move?.Invoke(kRect.transform.position);
    }


    //void OnClicked_Up()
    //{
    //    if(m_PresetObjUI == null)  return;
    //    if(m_PresetObjUI.m_listFaceRect.Count == 0) return;

    //    for( int i = 0; i < m_PresetObjUI.m_listFaceRect.Count; i++)
    //    {
    //        CFaceRect kFace = m_PresetObjUI.m_listFaceRect[i];
    //        kFace.transform.localPosition += kFace.transform.forward * Time.deltaTime * DEFAULT_SPEED;
    //    }

    //    CFaceRect kRect = m_PresetObjUI.m_listFaceRect[0];
    //    onAction_Move?.Invoke(kRect.transform.position);
    //}

    //void OnClicked_Down()
    //{
    //    if (m_PresetObjUI == null) return;
    //    if (m_PresetObjUI.m_listFaceRect.Count == 0) return;

    //    for (int i = 0; i < m_PresetObjUI.m_listFaceRect.Count; i++)
    //    {
    //        CFaceRect kFace = m_PresetObjUI.m_listFaceRect[i];
    //        kFace.transform.localPosition += (-kFace.transform.forward) * Time.deltaTime * DEFAULT_SPEED;
    //    }

    //    CFaceRect kRect = m_PresetObjUI.m_listFaceRect[0];
    //    onAction_Move?.Invoke(kRect.transform.position);
    //}

    //void OnClicked_Lef()
    //{
    //    if (m_PresetObjUI == null) return;
    //    if (m_PresetObjUI.m_listFaceRect.Count == 0) return;

    //    for (int i = 0; i < m_PresetObjUI.m_listFaceRect.Count; i++)
    //    {
    //        CFaceRect kFace = m_PresetObjUI.m_listFaceRect[i];
    //        kFace.transform.localPosition += (-kFace.transform.right) * Time.deltaTime * DEFAULT_SPEED;
    //    }

    //    CFaceRect kRect = m_PresetObjUI.m_listFaceRect[0];
    //    onAction_Move?.Invoke(kRect.transform.position);
    //}

    //void OnClicked_Right()
    //{
    //    if (m_PresetObjUI == null) return;
    //    if (m_PresetObjUI.m_listFaceRect.Count == 0) return;

    //    for (int i = 0; i < m_PresetObjUI.m_listFaceRect.Count; i++)
    //    {
    //        CFaceRect kFace = m_PresetObjUI.m_listFaceRect[i];
    //        kFace.transform.localPosition += (kFace.transform.right) * Time.deltaTime * DEFAULT_SPEED;
    //    }

    //    CFaceRect kRect = m_PresetObjUI.m_listFaceRect[0];
    //    onAction_Move?.Invoke(kRect.transform.position);
    //}

}