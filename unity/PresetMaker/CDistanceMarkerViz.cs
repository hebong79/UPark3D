using UnityEngine;
using System.Collections.Generic;

// dirtype 이 수정되면서 정상동작하지 않음.- 수정필요 (2024-06-17) 


/// <summary>
/// CParkingGeometry 의 계산 결과를 기반으로 각 주차면 끝점을
/// 색깔 구(Sphere)로 3D 공간에 표시하는 시각화 헬퍼.
///
/// ┌────────────────────────────────────────────────────────────┐
/// │  마커 색상 정의                                             │
/// │  ● 파랑  (Blue)   : 시작점 (offsetPos, 1번 주차면 위치)    │
/// │  ● 청록  (Cyan)   : 1개 step 끝점 (2번 주차면 위치)        │
/// │  ● 초록  (Green)  : 마지막(N번) 주차면 위치                 │
/// │  ● 빨강  (Red)    : 마지막 주차면의 depth 끝점             │
/// │  ● 노랑  (Yellow) : 1번 주차면의 depth 끝점 (depthZOffset) │
/// └────────────────────────────────────────────────────────────┘
///
/// [CParkingGeometry 기반 좌표 계산]
///
///   width  = useBaseWidth ? xSize : zSize   (주차면 폭)
///   length = useBaseWidth ? zSize : xSize   (주차면 길이)
///   단, ZPlus/ZMinus 일 때 width↔length 교환 (MakeFaceRect 동일)
///
///   single = CParkingGeometry.GetSingleParkingDimensions(faceRot, width, length)
///   depthSize = single.boxHeight   (= width*sin + length*cos)
///
///   stepWidth 결정 (MakeFaceRect 와 동일한 규칙):
///     XPlus / XMinus / ZPlus / ZMinus :
///       CParkingGeometry.GetMultiParkingDimensions.stepW (= width / cos(faceRot))
///     Dir + useBaseWidth=true  : width   (cos 보정 없음)
///     Dir + useBaseWidth=false : length  (cos 보정 없음)
///
///   stepDir   = GetStepDir(dirType, faceRot, useBaseWidth)
///               XPlus→+X, XMinus→-X, ZPlus→+Z, ZMinus→-Z
///               Dir + useBaseWidth=true  : faceRot<=180°→+right, >180°→-right
///               Dir + useBaseWidth=false : faceRot<=180°→+forward, >180°→-forward
///   depthDir  = Quaternion.Euler(0, faceRot, 0) * Vector3.forward
///             = (-sinθ, 0, cosθ)
///
///     🔵 origin
///     🩵 origin + stepVec * 1                       (2번 주차면 = 1 step 뒤)
///     🟢 origin + stepVec * (N-1)                   (마지막 주차면)
///     🔴 origin + stepVec * (N-1) + depthVec        (마지막 주차면 depth 끝)
///     🟡 origin + depthVec                          (1번 주차면 depth 끝)
/// </summary>
public class CDistanceMarkerViz : MonoBehaviour
{
    [Header("마커 설정")]
    [Tooltip("구 마커의 반지름 (m)")]
    public float m_SphereRadius = 0.3f;

    [Tooltip("구 마커의 Y 높이 (바닥 위)")]
    public float m_MarkerY = 0.5f;

    // ── 마커 색상 ────────────────────────────────────────────
    public Color m_ColorOrigin    = Color.blue;    // 시작점
    public Color m_ColorStepEnd   = Color.cyan;    // 1 step 끝
    public Color m_ColorLastFace  = Color.green;   // 마지막 주차면
    public Color m_ColorLastDepth = Color.red;     // 마지막 주차면 depth 끝
    public Color m_ColorDepthZ    = Color.yellow;  // 1번 주차면 depth 끝

    // ── 내부 관리 ────────────────────────────────────────────
    private readonly List<GameObject> m_Markers = new List<GameObject>();

    // ─────────────────────────────────────────────
    // Public API
    // ─────────────────────────────────────────────

    /// <summary>
    /// 프리셋 정보를 받아 CParkingGeometry 기반으로 마커를 배치한다.
    /// </summary>
    public void ShowMarkers(SDPresetInfo preset, SingleParkingResult kSingle)
    {
        ClearMarkers();

        if (preset == null) return;

        float faceRot    = preset.faceRot;
        float faceRotRad = faceRot * Mathf.Deg2Rad;

        // ── width / length 결정 (MakeFaceRect 동일) ───────────
        float width, length;
       
        //if (!preset.useBaseWidth)
        {
            // ZPlus/ZMinus: xSize↔zSize 교환
            //width  = preset.useBaseWidth ? preset.zSize : preset.xSize;
            //length = preset.useBaseWidth ? preset.xSize : preset.zSize;
        }
        //else
        //{
            width  = preset.useBaseWidth ? preset.xSize : preset.zSize;
            length = preset.useBaseWidth ? preset.zSize : preset.xSize;
        //}

        // ── depth 크기: CParkingGeometry.GetSingleParkingDimensions ──
        SingleParkingResult single = CParkingGeometry.GetSingleParkingDimensions(faceRot, width, length);
        float depthSize = single.boxHeight;  // width*sinθ + length*cosθ

        // ── stepWidth 결정 (MakeFaceRect 와 동일한 규칙) ──────────
        // Dir 타입: MakeFaceRect 는 width(또는 length) * j 로 이동 (cos 보정 없음)
        // 그 외   : CalculateRotatedWidth = width / cos(faceRot) = CParkingGeometry.stepW
        float stepWidth;
        if (preset.dirType == (int)EFaceDirType.Dir)
        {
            stepWidth = preset.useBaseWidth ? width : length;
        }
        else
        {
            MultiParkingResult multi = CParkingGeometry.GetMultiParkingDimensions(preset.faceCount, faceRot, width, length);
            stepWidth = multi.stepW;   // width / cos(faceRot)
        }

        // ── step 방향 단위벡터 (EFaceDirType + MakeFaceRect 동일) ──
        Vector3 stepDir = GetStepDir((EFaceDirType)preset.dirType, faceRot, preset.useBaseWidth);

        // ── depth 방향: Quaternion.Euler(0, faceRot, 0) * forward ──
        Vector3 depthDir = new Vector3(-Mathf.Sin(faceRotRad), 0f, Mathf.Cos(faceRotRad));
        depthDir.Normalize();

        Vector3 origin   = preset.offsetPos.Get();
        Vector3 stepVec  = stepDir  * stepWidth;
        Vector3 depthVec = depthDir * depthSize;
        Vector3 boxWidthVec = stepDir * kSingle.boxWidth;  // 1개 주차면 전체 폭
        Vector3 boxHeightVec = depthDir * (-kSingle.boxHeight); // 주차면 높이 방향 벡터 (depthZ 위치 계산용)

        int n = preset.faceCount;

        // 🔵 시작점 (1번 주차면)
        PlaceMarker("Origin (1번 주차면)",           origin,                                m_ColorOrigin);

        // 🩵 2번 주차면 위치 (= 1 step 이동)
        PlaceMarker("StepEnd (2번 주차면 위치)",      origin + stepVec,                      m_ColorStepEnd);

        // 🟢 마지막(N번) 주차면 위치
        PlaceMarker($"LastFace ({n}번 주차면 위치)",  origin + stepVec * (n - 1),            m_ColorLastFace);

        // 🔴 마지막 주차면 depth 끝점
        PlaceMarker($"LastDepth ({n}번 주차면 끝)",   origin + stepVec * (n - 1) + boxWidthVec, m_ColorLastDepth);

        // 🟡 1번 주차면 depth 끝점
        //PlaceMarker("DepthZ (1번 주차면 depth 끝)",   origin + boxHeightVec,                m_ColorDepthZ);
    }

    /// <summary>모든 마커를 제거한다.</summary>
    public void ClearMarkers()
    {
        for (int i = m_Markers.Count - 1; i >= 0; i--)
        {
            if (m_Markers[i] != null)
                Destroy(m_Markers[i]);
        }
        m_Markers.Clear();
    }

    // ─────────────────────────────────────────────
    // 내부 헬퍼
    // ─────────────────────────────────────────────

    /// <summary>
    /// EFaceDirType 에 따른 step 방향 단위벡터 반환.
    /// Dir 타입은 MakeFaceRect 와 동일:
    ///   useBaseWidth=true  → faceRot &lt;= 180° : +right,  > 180° : -right
    ///   useBaseWidth=false → faceRot &lt;= 180° : +forward, > 180° : -forward
    /// </summary>
    public static Vector3 GetStepDir(EFaceDirType dirType, float faceRotDeg, bool useBaseWidth = true)
    {
        switch (dirType)
        {
            //case EFaceDirType.XPlus:  return Vector3.right;
            //case EFaceDirType.XMinus: return Vector3.left;
            //case EFaceDirType.ZPlus:  return Vector3.forward;
            //case EFaceDirType.ZMinus: return Vector3.back;
            case EFaceDirType.Default:
            case EFaceDirType.Dir:
            default:
            {
                float rad     = faceRotDeg * Mathf.Deg2Rad;
                // right  = Quaternion.Euler(0, faceRotDeg, 0) * Vector3.right  = ( cos θ, 0,  sin θ)
                // forward= Quaternion.Euler(0, faceRotDeg, 0) * Vector3.forward= (-sin θ, 0,  cos θ)
                Vector3 right   = new Vector3( Mathf.Cos(rad), 0f,  Mathf.Sin(rad));
                Vector3 forward = new Vector3(-Mathf.Sin(rad), 0f,  Mathf.Cos(rad));

                bool flipDir = faceRotDeg > 180f;
                if (useBaseWidth)
                    return flipDir ? -right   : right;
                else
                    return flipDir ? -forward : forward;
            }
        }
    }

    private void PlaceMarker(string label, Vector3 worldPos, Color color)
    {
        worldPos.y = m_MarkerY;

        GameObject go = GameObject.CreatePrimitive(PrimitiveType.Sphere);
        go.name = $"[Marker] {label}";
        go.transform.parent     = this.transform;
        go.transform.position   = worldPos;
        go.transform.localScale = Vector3.one * (m_SphereRadius * 2f);

        // Collider 제거 (피킹 방해 방지)
        Collider col = go.GetComponent<Collider>();
        if (col != null) Destroy(col);

        // URP Unlit → Legacy Unlit 순으로 셰이더 탐색
        Renderer rend = go.GetComponent<Renderer>();
        if (rend != null)
        {
            Shader sh = Shader.Find("Universal Render Pipeline/Unlit")
                     ?? Shader.Find("Unlit/Color")
                     ?? rend.sharedMaterial.shader;
            Material mat = new Material(sh);
            mat.color = color;
            rend.material = mat;
        }

        m_Markers.Add(go);
    }

    private void OnDestroy()
    {
        ClearMarkers();
    }
}
