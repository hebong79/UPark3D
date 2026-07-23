using System.Collections.Generic;
using UnityEngine;

/// <summary>
/// 차량 선택 시 하이라이트 효과를 담당하는 컴포넌트.
/// EHighlightMode.SolidColor : 차체 색상을 선택 색으로 단색 변경
/// EHighlightMode.Outline    : 차체 외곽선(스케일업 + 전면 컬링) 표시
/// CObjCar.Start() 에서 자동으로 추가된다.
/// </summary>
public class CCarSelectHighlight : MonoBehaviour
{
    public enum EHighlightMode
    {
        SolidColor = 0,  // 단색 오버레이
        Outline    = 1,  // 외곽선
    }

    [Header("하이라이트 설정")]
    public EHighlightMode m_Mode            = EHighlightMode.Outline;
    public Color          m_SolidSelectColor = new Color(0f, 0.85f, 1f, 1f);  // 단색 선택 색
    public Color          m_OutlineColor     = Color.yellow;                    // 외곽선 색
    [Range(1.01f, 1.15f)]
    public float          m_OutlineScale     = 1.05f;                           // 외곽선 두께

    // ── 내부 상태 ──────────────────────────────────────────────
    bool   m_IsSelected   = false;
    Color  m_OriginalColor;
    CVehicleColorController m_ColorCtrl;
    readonly List<GameObject> m_OutlineObjs = new List<GameObject>();
    Material m_OutlineMat;

    void Awake()
    {
        m_ColorCtrl    = GetComponent<CVehicleColorController>();
        m_OriginalColor = m_ColorCtrl != null ? m_ColorCtrl.GetCurrentColor() : Color.white;
    }

    // ── Public API ─────────────────────────────────────────────

    /// <summary>선택 상태를 설정한다.</summary>
    public void SetSelected(bool selected)
    {
        if (m_IsSelected == selected) return;
        m_IsSelected = selected;

        // 원본 색 스냅샷 (선택 시점의 현재 색을 기억)
        if (selected && m_ColorCtrl != null)
            m_OriginalColor = m_ColorCtrl.GetCurrentColor();

        Refresh();
    }

    /// <summary>하이라이트 모드를 전환한다. 이미 선택 중이면 즉시 재적용된다.</summary>
    public void SetMode(EHighlightMode mode)
    {
        if (m_Mode == mode) return;
        bool wasSelected = m_IsSelected;
        if (wasSelected) RemoveHighlight();
        m_Mode = mode;
        if (wasSelected) ApplyHighlight();
    }

    public bool IsSelected => m_IsSelected;

    // ── 내부 구현 ──────────────────────────────────────────────

    void Refresh()
    {
        if (m_IsSelected) ApplyHighlight();
        else              RemoveHighlight();
    }

    void ApplyHighlight()
    {
        if (m_Mode == EHighlightMode.SolidColor) ApplySolidColor();
        else                                      ApplyOutline();
    }

    // ── 단색 모드 ──────────────────────────────────────────────

    void ApplySolidColor()
    {
        if (m_ColorCtrl == null) return;
        m_ColorCtrl.SetColor(m_SolidSelectColor);
    }

    // ── 외곽선 모드 ────────────────────────────────────────────

    void ApplyOutline()
    {
        DestroyOutlineObjs();
        EnsureOutlineMaterial();

        foreach (MeshFilter mf in GetComponentsInChildren<MeshFilter>(true))
        {
            if (mf.sharedMesh == null) continue;
            MeshRenderer mr = mf.GetComponent<MeshRenderer>();
            if (mr == null) continue;

            // 원본 메시와 같은 위치·크기에서 약간 더 크게 스케일한 오브젝트 생성
            GameObject outlineGO = new GameObject("_SelectOutline");
            outlineGO.hideFlags = HideFlags.HideAndDontSave;
            outlineGO.transform.SetParent(mf.transform, false);
            outlineGO.transform.localPosition = Vector3.zero;
            outlineGO.transform.localRotation = Quaternion.identity;
            outlineGO.transform.localScale    = Vector3.one * m_OutlineScale;

            MeshFilter omf = outlineGO.AddComponent<MeshFilter>();
            omf.sharedMesh = mf.sharedMesh;

            // sharedMaterial 개수에 맞춰 외곽선 머티리얼 배열 구성
            MeshRenderer omr = outlineGO.AddComponent<MeshRenderer>();
            Material[] mats  = new Material[mr.sharedMaterials.Length];
            for (int i = 0; i < mats.Length; i++) mats[i] = m_OutlineMat;
            omr.sharedMaterials = mats;

            m_OutlineObjs.Add(outlineGO);
        }
    }

    /// <summary>
    /// 외곽선용 머티리얼 생성.
    /// CullMode.Front : 앞면을 컬링 → 뒷면만 렌더 → 스케일업과 결합해 외곽선 효과.
    /// URP → Built-in 순으로 셰이더 탐색.
    /// </summary>
    void EnsureOutlineMaterial()
    {
        if (m_OutlineMat != null) return;

        Shader sh = Shader.Find("Universal Render Pipeline/Lit")
                 ?? Shader.Find("Standard");

        m_OutlineMat = new Material(sh)
        {
            color = m_OutlineColor,
            enableInstancing = true
        };
        m_OutlineMat.SetInt("_Cull", (int)UnityEngine.Rendering.CullMode.Front);
    }

    // ── 하이라이트 제거 ────────────────────────────────────────

    void RemoveHighlight()
    {
        // 단색 복원
        if (m_ColorCtrl != null)
            m_ColorCtrl.SetColor(m_OriginalColor);

        // 외곽선 오브젝트 제거
        DestroyOutlineObjs();
    }

    void DestroyOutlineObjs()
    {
        foreach (var go in m_OutlineObjs)
            if (go != null) Destroy(go);
        m_OutlineObjs.Clear();
    }

    void OnDestroy()
    {
        DestroyOutlineObjs();
        if (m_OutlineMat != null) Destroy(m_OutlineMat);
    }
}
