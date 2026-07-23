using System;
using System.Collections.Generic;
using UnityEngine;





/*
 *  바닥 주차면 +  Line Qube 관리 
 * 
 * 
 * 
 */
public class CPMakerParkSpaceUI : MonoBehaviour
{
    //
    // 프리셋 관리
    //
    [Serializable]
    public class SPresetObjUI
    {
        public int m_PresetIdx = 0;                                             // 프리셋 id = CFaceRect.m_Id
        public List<CFaceRect> m_listFaceRect = new List<CFaceRect>();          // 주차면 리스트
        public List<CLineQubeBox> m_listQubeBox = new List<CLineQubeBox>();     // 라인 육면체 리스트
        public GameObject m_goFaceRectParent = null;                            // "preset_line" 부모 GO (face rect + PresetQube 포함)


        public void ShowQubeLineList(bool bShow)
        {
            for (int i = 0; i < m_listQubeBox.Count; i++)
            {
                m_listQubeBox[i].gameObject.SetActive(bShow);
            }
        }

        // 프리셋 파괴하기 — 부모 GO 삭제로 face rect·PresetQube·QubeBox 일괄 제거
        public void RemoveAll_FaceRectList()
        {
            m_listQubeBox.Clear();
            if (m_goFaceRectParent != null)
            {
                Destroy(m_goFaceRectParent);
                m_goFaceRectParent = null;
            }
            else
            {
                for (int i = 0; i < m_listFaceRect.Count; i++)
                    if (m_listFaceRect[i] != null)
                        Destroy(m_listFaceRect[i].gameObject);
            }
            m_listFaceRect.Clear();
            DestroySelectOverlay();
        }

        // 3D 큐브 파괴하기
        public void RemoveAll_QubeBoxList()
        {
            for (int i = 0; i < m_listQubeBox.Count; i++)
            {
                m_listQubeBox[i].RemoveAll_DrawRectList();
            }
            m_listQubeBox.Clear();
        }

        // ── 선택 오버레이 ──────────────────────────────────────────
        public GameObject m_goSelectOverlay = null;

        public void ShowSelectOverlay(bool bShow)
        {
            if (m_goSelectOverlay != null)
                m_goSelectOverlay.SetActive(bShow);
        }

        public void DestroySelectOverlay()
        {
            if (m_goSelectOverlay != null)
            {
                Destroy(m_goSelectOverlay);
                m_goSelectOverlay = null;
            }
        }

        public void RemoveAll()
        {
            RemoveAll_FaceRectList();
            RemoveAll_QubeBoxList();
            DestroySelectOverlay();
        }


    }

    public Camera m_Camera;
    public Transform m_FaceRectParent;
    public List<SPresetObjUI> m_PresetObjList = new List<SPresetObjUI>();           // 프리셋 리스트    

    [Space]
    public Color m_LineColor = Color.white;                             // 면의 라인 색상
    public Color m_SelectColor = new Color32(0, 217, 51, 40);          // 선택 영역 하이라이트 색상 (a=40)
    public float m_QubeHeight = 2.5f;           // 직육면체 높이( 기본값은 2.5 )
    public float m_QubeLineWidth = 0.05f;       // 직육면체 라인의 두께
    public float m_FacePosY = 0.05f;            // 주차면의 y값

    [HideInInspector] public int  m_iCurPresetIdx      = 0;     // 현재 선택된 프리셋 인덱스
    [HideInInspector] public bool m_bHighlightBarVisible = true; // 하이라이트 표시 여부

    public float m_AddSpace = 0;              // 주차면 간격 ( 기본값은 2.5 ) - 기본 width값이 안맞을때 추가로 사용



    void Start()
    {
        if(m_Camera == null)
            m_Camera = Camera.main;

    }

    private void OnDestroy()
    {
        RemoveAll();
    }


    public void Initialize(bool isQubeShow)
    {
        MakePresetList(isQubeShow);
        //CameraMoveToPreset();
    }

    
    public SPresetObjUI GetPresetObj(int idx)
    {
        return m_PresetObjList.Find( x => x.m_PresetIdx == idx );
    }

    public SPresetObjUI GetPresetObj()
    {
        int idx = m_iCurPresetIdx;
        return m_PresetObjList.Find(x => x.m_PresetIdx == idx);
    }

    // 주차면 정보 로딩하고 주차면 그리기
    // nPlaceIndex : 설치 주자장 인덱스
    // fileName : path가 없는 파일 이름
    // fSpace : 주차면 간격 ( 기본값은 2.5 ) - 기본 width값이 안맞을때 추가로 사용
    // return : FaceRect들의 부모 gameObject
    public Transform MakeFaceRect( SDPresetInfo kPreset, Transform kFaceParent = null )
    {
        if (kFaceParent == null)
            kFaceParent = m_FaceRectParent;

        SPresetObjUI kPresetObjUI = GetPresetObj(kPreset.idx);
        if (kPresetObjUI == null)
        {
            kPresetObjUI = new SPresetObjUI();
            kPresetObjUI.m_PresetIdx = kPreset.idx;
            m_PresetObjList.Add(kPresetObjUI);
        }
        else
        {
            kPresetObjUI.RemoveAll_FaceRectList();
        }

        int dirType = kPreset.dirType;

        GameObject goParent = new GameObject("preset_line");
        goParent.transform.parent = kFaceParent;
        kPresetObjUI.m_goFaceRectParent = goParent;

        //var data = kSave.m_FGData.datas[i];
        float xSize = kPreset.xSize;
        float zSize = kPreset.zSize;

        

        for (int j = 0; j < kPreset.faceCount; j++)
        {
            Vector3 v = kPreset.offsetPos.Get();

            CFaceRect kRect = CFaceRect.CreateFaceRect(goParent.transform, xSize, zSize, m_FacePosY, CFaceRect.DParkFaceLineWidth);
            kRect.m_faceRot = kPreset.faceRot;
            kRect.transform.localRotation = Quaternion.Euler(0, kRect.m_faceRot, 0);

            float width = xSize;
            float height = zSize;

            // Default 케이스는 월드 X(또는 Z)축 단순 가산이므로, 사선 회전 시 각도로 인한 폭 증가를 보정한다.
            // cos(faceRot) 이 0에 가까운 각도(±90°)는 보정 불가 → 원본 xSize/zSize 유지.
            if ((EFaceDirType)kPreset.dirType == EFaceDirType.Default)
            {
                float cosRot = Mathf.Cos(kRect.m_faceRot * Mathf.Deg2Rad);
                if (Mathf.Abs(cosRot) > 0.001f)
                {
                    width  = CFaceRect.CalculateRotatedWidth(xSize, zSize, kRect.m_faceRot);
                    height = CFaceRect.CalculateRotatedWidth(zSize, xSize, kRect.m_faceRot);
                }
            }

            if ( kPreset.useBaseWidth)
                MoveByFaceDirType(kRect.transform, (EFaceDirType)kPreset.dirType, width * j, kPreset.useBaseWidth, ref v);
            else
                MoveByFaceDirType(kRect.transform, (EFaceDirType)kPreset.dirType, height * j, kPreset.useBaseWidth, ref v);

            kRect.m_idx = j + 1;
            kRect.SetPosition(v);
            kRect.SetLineColor(m_LineColor);

            kPresetObjUI.m_listFaceRect.Add(kRect);
        }

        goParent.transform.localRotation = Quaternion.Euler(0, kPreset.groupRot, 0);

        return goParent.transform;
    }

    public void MoveByFaceDirType(Transform target, EFaceDirType eDirType, float fMoveWidth, bool useBaseWidth, ref Vector3 vOri)
    {
        if (eDirType == EFaceDirType.Dir){

            Vector3 dir;
            if (useBaseWidth)
                dir = (target.eulerAngles.y > 180.0f) ? -target.right * fMoveWidth : target.right * fMoveWidth;
            else
                dir = (target.eulerAngles.y > 180.0f) ? -target.forward * fMoveWidth : target.forward * fMoveWidth;

            vOri += dir;
        }
        else {
            if (useBaseWidth) {
                if (target.eulerAngles.y > 180.0)
                    vOri.x -= fMoveWidth;
                else
                    vOri.x += fMoveWidth;
            }
            else
            {
                if (target.eulerAngles.y > 180.0)
                    vOri.z -= fMoveWidth;
                else
                    vOri.z += fMoveWidth;
            }
        }
    }

    public void MoveByFaceDirType2(Transform target, EFaceDirType eDirType, float fMoveWidth, bool useBaseWidth, ref Vector3 vOri)
    {
        switch (eDirType)
        {
            //case EFaceDirType.XPlus:
            //    vOri.x += fMoveWidth;
            //    break;
            //case EFaceDirType.XMinus:
            //    vOri.x -= fMoveWidth;
            //    break;
            //case EFaceDirType.ZPlus:
            //    vOri.z += fMoveWidth;
            //    break;
            //case EFaceDirType.ZMinus:
            //    vOri.z -= fMoveWidth;
            //    break;
            case EFaceDirType.Dir:
                {
                    Vector3 dir;
                    if (useBaseWidth)
                        dir = (target.eulerAngles.y > 180.0f) ? -target.right * fMoveWidth : target.right * fMoveWidth;
                    else
                        dir = (target.eulerAngles.y > 180.0f) ? -target.forward * fMoveWidth : target.forward * fMoveWidth;

                    vOri += dir;
                }
                break;
        }
    }


    public void MakeLineQubeBox(SDPresetInfo kPreset, Transform kParent)
    {
        var listColor = new List<Color>() { Color.green, Color.yellow, Color.magenta, Color.cyan };
        int maxColor = listColor.Count;

        // 프리셋 기준으로 만들기 위해 설정
        SPresetObjUI kPresetObjUI = GetPresetObj(kPreset.idx);
        if (kPresetObjUI == null) return;

        kPresetObjUI.RemoveAll_QubeBoxList();

        GameObject goParent = new GameObject("PresetQube");
        goParent.transform.parent = kParent;

        for (int i = 0; i < kPresetObjUI.m_listFaceRect.Count; i++)
        {
            CFaceRect kRect = kPresetObjUI.m_listFaceRect[i];
            Color color = listColor[i % maxColor];

            CLineQubeBox kQubeBox = CLineQubeBox.MakeQubeBoxLine(kRect.transform, kRect.m_Points, color, m_QubeHeight, m_QubeLineWidth); 
            kQubeBox.transform.position = kRect.transform.position;
            // 주의: 사각박스가 부모이므로 회전값은 0이 되어야 한다.
            kQubeBox.transform.localRotation = Quaternion.identity; // kRect.transform.localRotation;


            kPresetObjUI.m_listQubeBox.Add(kQubeBox);    
        }
    }

    /// <summary>
    /// 프리셋 리스트 생성
    /// </summary>
    /// <param name="bShowQubeBox"></param>
    public void MakePresetList(bool bShowQubeBox)
    {
        this.RemoveAll();

        SDPresetDatas kDatas = CDataMgr.Inst.m_SavePresetData.m_Datas;
        for (int i = 0; i < kDatas.datas.Count; i++)
        {
            var kPreset = kDatas.datas[i];

            Transform kFaceParent = this.MakeFaceRect(kPreset);
            this.MakeLineQubeBox(kPreset, kFaceParent);
            this.ShowQubeLineList(bShowQubeBox);
        }
    }

    public void MakePreset(SDPresetInfo kPreset, bool bShowQubeBox)
    {
        Transform kFaceParent = this.MakeFaceRect(kPreset);
        this.MakeLineQubeBox(kPreset, kFaceParent);
        this.ShowQubeLineList(bShowQubeBox);
    }


    //public void CameraMoveToPreset()
    //{
    //    SDPresetInfo kPreset = CDataMgr.Inst.m_SavePresetData.GetPresetInfo(1);
    //    if(kPreset != null)
    //    {
    //        Vector3 vPos = kPreset.camPos.Get();
    //        Vector3 vRot = kPreset.camRot.Get();

    //        m_Camera.transform.position = vPos;
    //        m_Camera.transform.localRotation = Quaternion.Euler(vRot);

    //        // 수평시야각 = fov * 화면비율( fov * 16.0f / 9 ) 인데
    //        // camera의 fov는 수직시야각 기준으로 계산이 되므로 화면 비율을 반대로 곱한다.
    //        if( kPreset.fov > 0.000000001f)
    //            m_Camera.fieldOfView = kPreset.fov * 9.0f / 16; 
    //    }
    //}


    /// <summary>
    /// 라인 육면체 Show / hide
    /// </summary>
    /// <param name="bShow"></param>
    public void ShowQubeLineList(bool bShow)
    {
        for( int i = 0; i < m_PresetObjList.Count; i++) {
            m_PresetObjList[i].ShowQubeLineList(bShow);
        }
    }

    public void RemoveAll_PresetObjList()
    {
        for( int i = 0; i < m_PresetObjList.Count; i++) {
            m_PresetObjList[i].RemoveAll();
        }
        m_PresetObjList.Clear();
    }

    //// 프리셋 파괴하기
    //public void RemoveAll_FaceRectList()
    //{
    //    for (int i = 0; i < m_listFaceRect.Count; i++)
    //    {
    //        Destroy(m_listFaceRect[i].gameObject);
    //    }
    //    m_listFaceRect.Clear();
    //}

    //// 3D 큐브 파괴하기
    //public void RemoveAll_QubeBoxList()
    //{
    //    for (int i = 0; i < m_listQubeBox.Count; i++)
    //    {
    //        m_listQubeBox[i].RemoveAll_DrawRectList();
    //    }
    //    m_listQubeBox.Clear();
    //}

    // 남아있는 노드명 들도 같이 삭제한다.
    public void RemoveAll_NodeClear()
    {
        // 0번 제외 ( 0번은 부모 노드임 )
        Transform [] trs = m_FaceRectParent.GetComponentsInChildren<Transform>(); 
        for( int i = trs.Length-1; i > 0; i--)
        {
            if( trs[i] != null )
                Destroy(trs[i].gameObject);
        }
    }

    // ── 선택 영역 하이라이트 ─────────────────────────────────────────────────

    /// <summary>
    /// 여러 프리셋을 동시에 하이라이트한다.
    /// presetIdxs 에 포함된 프리셋은 녹색 라인+오버레이, 나머지는 기본 색상.
    /// m_iCurPresetIdx 는 primaryIdx 로 설정된다.
    /// </summary>
    public void HighlightPresets(List<int> presetIdxs, int primaryIdx)
    {
        m_iCurPresetIdx = primaryIdx;

        for (int i = 0; i < m_PresetObjList.Count; i++)
        {
            SPresetObjUI kPresetObj = m_PresetObjList[i];
            bool isSelected = presetIdxs != null && presetIdxs.Contains(kPresetObj.m_PresetIdx);

            bool showHighlight = isSelected && m_bHighlightBarVisible;
            Color lineColor = showHighlight ? m_SelectColor : m_LineColor;
            for (int j = 0; j < kPresetObj.m_listFaceRect.Count; j++)
                kPresetObj.m_listFaceRect[j].SetLineColor(lineColor);

            if (showHighlight)
            {
                if (kPresetObj.m_goSelectOverlay == null)
                    kPresetObj.m_goSelectOverlay = CreateSelectOverlay(kPresetObj);
                kPresetObj.ShowSelectOverlay(true);
            }
            else
            {
                kPresetObj.ShowSelectOverlay(false);
            }
        }
    }

    /// <summary>
    /// 지정된 프리셋을 선택 하이라이트(녹색 오버레이 + 녹색 라인)로 표시하고,
    /// 나머지 프리셋은 기본 라인 색상으로 복원한다.
    /// presetIdx == -1 이면 모든 선택을 해제한다.
    /// </summary>
    public void HighlightPreset(int presetIdx)
    {
        m_iCurPresetIdx = presetIdx;

        for (int i = 0; i < m_PresetObjList.Count; i++)
        {
            SPresetObjUI kPresetObj = m_PresetObjList[i];
            bool isSelected = (kPresetObj.m_PresetIdx == presetIdx);

            // 면 라인 색상 변경 (하이라이트 숨김 중이면 선택도 기본 색상으로)
            bool showHighlight = isSelected && m_bHighlightBarVisible;
            Color lineColor = showHighlight ? m_SelectColor : m_LineColor;
            for (int j = 0; j < kPresetObj.m_listFaceRect.Count; j++)
                kPresetObj.m_listFaceRect[j].SetLineColor(lineColor);

            // 선택 오버레이 표시/숨김
            if (showHighlight)
            {
                if (kPresetObj.m_goSelectOverlay == null)
                    kPresetObj.m_goSelectOverlay = CreateSelectOverlay(kPresetObj);
                kPresetObj.ShowSelectOverlay(true);
            }
            else
            {
                kPresetObj.ShowSelectOverlay(false);
            }
        }
    }

    public void RemoveAll_SelectOveray()
    {
        for (int i = 0; i < m_PresetObjList.Count; i++)
        {
            SPresetObjUI kPresetObj = m_PresetObjList[i];
            kPresetObj.DestroySelectOverlay();
        }
    }

    /// <summary>
    /// 현재 선택된 프리셋의 하이라이트(오버레이 + 라인 색상)를 표시하거나 숨긴다.
    /// m_toHideSelectBar 토글의 onValueChanged 에서 ShowHighlightBar(!isOn) 으로 호출한다.
    /// </summary>
    public void ShowHighlightBar(bool bShow)
    {
        m_bHighlightBarVisible = bShow;
        HighlightPreset(m_iCurPresetIdx);
    }

    /// <summary>
    /// 선택된 프리셋의 각 주차면(CFaceRect) 내부만 채우는 메시 오버레이를 생성한다.
    /// 모든 face rect의 쿼드를 하나의 메시로 합쳐 렌더링한다.
    /// m_Points 순서: A(0) B(1) C(2) D(3) — 위에서 볼 때 CCW
    /// </summary>
    GameObject CreateSelectOverlay(SPresetObjUI presetObj)
    {
        if (presetObj.m_listFaceRect.Count == 0) return null;

        int    faceCount = presetObj.m_listFaceRect.Count;
        float  yPos      = m_FacePosY - 0.02f;   // 라인 아래에 배치(Z-fighting 방지)
        var    verts     = new Vector3[faceCount * 4];
        var    tris      = new int[faceCount * 6]; // 면당 삼각형 2개

        for (int i = 0; i < faceCount; i++)
        {
            CFaceRect rect = presetObj.m_listFaceRect[i];
            Transform tr   = rect.transform;

            // 4개 꼭짓점을 월드 공간으로 변환 후 Y를 오버레이 높이로 고정
            for (int j = 0; j < 4; j++)
            {
                Vector3 w = tr.TransformPoint(rect.m_Points[j]);
                w.y = yPos;
                verts[i * 4 + j] = w;
            }

            // 쿼드 → 삼각형 2개 (CCW: A-B-C, A-C-D)
            int vi = i * 4;
            int ti = i * 6;
            tris[ti]     = vi;     tris[ti + 1] = vi + 1; tris[ti + 2] = vi + 2;
            tris[ti + 3] = vi;     tris[ti + 4] = vi + 2; tris[ti + 5] = vi + 3;
        }

        var mesh = new Mesh();
        mesh.vertices  = verts;
        mesh.triangles = tris;
        mesh.RecalculateNormals();
        mesh.RecalculateBounds();

        GameObject go = new GameObject("SelectOverlay");
        go.transform.parent     = m_FaceRectParent;
        go.transform.position   = Vector3.zero;
        go.transform.rotation   = Quaternion.identity;
        go.transform.localScale = Vector3.one;

        go.AddComponent<MeshFilter>().mesh = mesh;

        go.AddComponent<MeshRenderer>().material = CreateOverlayMaterial();

        return go;
    }

    /// <summary>
    /// m_SelectColor 의 RGB + 고정 알파(40/255)로 반투명 머티리얼을 생성한다.
    /// URP 환경에서는 Universal Render Pipeline/Unlit 셰이더를 투명 모드로 설정한다.
    /// Inspector 직렬화값에 남아 있는 알파는 무시하고 코드에서 40/255 를 강제 적용한다.
    /// </summary>
    Material CreateOverlayMaterial()
    {
        const float OVERLAY_ALPHA = 40f / 255f;
        Color c = new Color(m_SelectColor.r, m_SelectColor.g, m_SelectColor.b, OVERLAY_ALPHA);

        // URP 프로젝트: Universal Render Pipeline/Unlit 셰이더로 투명 머티리얼 생성
        Shader urpUnlit = Shader.Find("Universal Render Pipeline/Unlit");
        if (urpUnlit != null)
        {
            Material mat = new Material(urpUnlit);

            // Surface Type = Transparent (1), Blend = Alpha (0)
            mat.SetFloat("_Surface",  1f);
            mat.SetFloat("_Blend",    0f);
            mat.SetFloat("_SrcBlend", (float)UnityEngine.Rendering.BlendMode.SrcAlpha);
            mat.SetFloat("_DstBlend", (float)UnityEngine.Rendering.BlendMode.OneMinusSrcAlpha);
            mat.SetFloat("_ZWrite",   0f);
            mat.EnableKeyword("_SURFACE_TYPE_TRANSPARENT");
            mat.renderQueue = (int)UnityEngine.Rendering.RenderQueue.Transparent;

            // URP Unlit 은 _BaseColor 를 사용
            mat.SetColor("_BaseColor", c);
            mat.color = c;   // _Color 도 함께 설정 (fallback)
            return mat;
        }

        // Built-in 파이프라인 fallback: Sprites/Default
        Shader spriteShader = Shader.Find("Sprites/Default");
        if (spriteShader != null)
        {
            Material mat = new Material(spriteShader);
            mat.SetInt("_SrcBlend", (int)UnityEngine.Rendering.BlendMode.SrcAlpha);
            mat.SetInt("_DstBlend", (int)UnityEngine.Rendering.BlendMode.OneMinusSrcAlpha);
            mat.SetInt("_ZWrite",   0);
            mat.renderQueue = (int)UnityEngine.Rendering.RenderQueue.Transparent;
            mat.color = c;
            return mat;
        }

        // 최후 fallback: Standard 셰이더 투명 모드
        Material fallback = new Material(Shader.Find("Standard"));
        fallback.SetFloat("_Mode",     3f);  // Transparent
        fallback.SetInt("_SrcBlend",   (int)UnityEngine.Rendering.BlendMode.SrcAlpha);
        fallback.SetInt("_DstBlend",   (int)UnityEngine.Rendering.BlendMode.OneMinusSrcAlpha);
        fallback.SetInt("_ZWrite",     0);
        fallback.renderQueue = (int)UnityEngine.Rendering.RenderQueue.Transparent;
        fallback.color = c;
        return fallback;
    }

    /// <summary>
    /// 주차면 그룹 전체를 지정된 월드 위치로 이동한다.
    /// "preset_line" 부모 GO 의 position 을 설정하므로
    /// 모든 자식 CFaceRect · CLineQubeBox 가 함께 이동된다.
    /// SelectOverlay 는 별도 GO 이므로 이동 후 HighlightPreset() 을 다시 호출해야 갱신된다.
    /// </summary>
    public void MovePreset(SPresetObjUI presetObj, Vector3 vNewPos)
    {
        if (presetObj == null || presetObj.m_goFaceRectParent == null) return;
        presetObj.m_goFaceRectParent.transform.position = vNewPos;
    }

    public void MovePreset(int presetIdx, Vector3 vNewPos)
    {
        MovePreset(GetPresetObj(presetIdx), vNewPos);
    }

    public void RemovePresetObj(int iPresetId)
    {
        SPresetObjUI kObj = m_PresetObjList.Find(x => x.m_PresetIdx == iPresetId);
        if (kObj != null)
        {
            kObj.RemoveAll();
            m_PresetObjList.Remove(kObj);
        }
    }

    public void RemoveAll()
    {
        RemoveAll_PresetObjList();
        RemoveAll_NodeClear();
    }
}
