using UnityEngine;


// Wall의 두께는 처음에 선정된 크기에서 고정되어야 한다. (즉, 벽의 두께는 크기에 따라 달라지면 안된다.)
// this의 scale이 변경되면 Wall의 scale은 역으로 변경되어야 한다. (즉, this의 scale이 2배가 되면 Wall의 scale은 0.5배가 되어야 한다.)
public class CResizeFloor : MonoBehaviour
{
    // ─────────────────────────────────────────────
    // Inspector 연결
    // ─────────────────────────────────────────────
    public GameObject m_FloorBase;

    public GameObject m_LeftWall;
    public GameObject m_RightWall;
    public GameObject m_FrontWall;
    public GameObject m_BackWall;

    // ─────────────────────────────────────────────
    // 고정 두께 (Inspector에서 직접 지정)
    // 기본값: Left/Right = (0.05, 1, 1), Front/Back = (1, 1, 0.05)
    // ─────────────────────────────────────────────
    [Header("Wall 고정 두께 (변경되지 않아야 할 실제 스케일)")]
    [SerializeField] private Vector3 m_FixedLeftScale   = new Vector3(0.05f, 1f, 1f);
    [SerializeField] private Vector3 m_FixedRightScale  = new Vector3(0.05f, 1f, 1f);
    [SerializeField] private Vector3 m_FixedFrontScale  = new Vector3(1f,    1f, 0.05f);
    [SerializeField] private Vector3 m_FixedBackScale   = new Vector3(1f,    1f, 0.05f);

    private Vector3 m_PrevFloorScale = Vector3.one;  // 변경 감지용

    // ─────────────────────────────────────────────
    // Unity 생명주기
    // ─────────────────────────────────────────────
    void Start()
    {
        m_PrevFloorScale = transform.localScale;
        ApplyWallScaleCorrection();
    }

    void Update()
    {
        // 런타임 scale 변경 감지 → 즉시 보정
        if (transform.localScale != m_PrevFloorScale)
        {
            ApplyWallScaleCorrection();
            m_PrevFloorScale = transform.localScale;
        }
    }

#if UNITY_EDITOR
    // Inspector 에서 값이 바뀔 때마다 에디터에서 즉시 호출
    void OnValidate()
    {
        ApplyWallScaleCorrection();
    }
#endif

    // ─────────────────────────────────────────────
    // 핵심: wall.localScale = 고정두께 / floor.localScale (성분별)
    //
    //  floor  = (80, 1, 10)  일 때
    //  LeftWall fixedScale = (0.05, 1, 1)
    //  → corrected = (0.05/80, 1/1, 1/10) = (0.000625, 1, 0.1)
    //  → 월드 기준 실제 두께 = corrected * floor = (0.05, 1, 1) ✔
    // ─────────────────────────────────────────────
    private void ApplyWallScaleCorrection()
    {
        Vector3 floorScale = transform.localScale;

        ApplyCorrection(m_LeftWall,  m_FixedLeftScale,  floorScale, true);
        ApplyCorrection(m_RightWall, m_FixedRightScale, floorScale, true);
        ApplyCorrection(m_FrontWall, m_FixedFrontScale, floorScale, false);
        ApplyCorrection(m_BackWall,  m_FixedBackScale,  floorScale, false);
    }

    /// <summary>
    /// 단일 Wall 보정
    /// corrected = fixedScale / floorScale  (성분별)
    /// floorScale 성분이 0 이면 0-나눗셈 방어: fixedScale 성분 그대로
    /// isLR: Left/Right 벽은 Z축 스케일 고정, Front/Back 벽은 X축 스케일 고정
    /// </summary>
    private void ApplyCorrection(GameObject wall, Vector3 fixedScale, Vector3 floorScale, bool isLR )
    {
        if (wall == null) return;
        float x = floorScale.x != 0f ? fixedScale.x / floorScale.x : fixedScale.x;
        float y = 1; // floorScale.y != 0f ? fixedScale.y / floorScale.y : fixedScale.y;
        float z = floorScale.z != 0f ? fixedScale.z / floorScale.z : fixedScale.z;

        if (isLR)
            z = 1;
        else
            x = 1;

        wall.transform.localScale = new Vector3(x, y, z);
    }

    // ─────────────────────────────────────────────
    // 공개 메서드: 고정 두께를 현재 Wall localScale로 재기록
    // (Wall 두께를 수동으로 바꾼 뒤 새 기준을 잡을 때 호출)
    // ─────────────────────────────────────────────
    public void ResetFixedScaleFromCurrentWalls()
    {
        Vector3 floorScale = transform.localScale;

        if (m_LeftWall  != null) m_FixedLeftScale  = Vector3.Scale(m_LeftWall.transform.localScale,  floorScale);
        if (m_RightWall != null) m_FixedRightScale = Vector3.Scale(m_RightWall.transform.localScale, floorScale);
        if (m_FrontWall != null) m_FixedFrontScale = Vector3.Scale(m_FrontWall.transform.localScale, floorScale);
        if (m_BackWall  != null) m_FixedBackScale  = Vector3.Scale(m_BackWall.transform.localScale,  floorScale);
    }

    // ─────────────────────────────────────────────
    // 테스트용 접근자 (내부 검증)
    // ─────────────────────────────────────────────
#if UNITY_EDITOR
    public Vector3 GetFixedLeftScale()  => m_FixedLeftScale;
    public Vector3 GetFixedRightScale() => m_FixedRightScale;
    public Vector3 GetFixedFrontScale() => m_FixedFrontScale;
    public Vector3 GetFixedBackScale()  => m_FixedBackScale;

    /// <summary>
    /// 테스트 전용: 고정 두께를 코드에서 직접 설정
    /// </summary>
    public void SetFixedScales(Vector3 left, Vector3 right, Vector3 front, Vector3 back)
    {
        m_FixedLeftScale  = left;
        m_FixedRightScale = right;
        m_FixedFrontScale = front;
        m_FixedBackScale  = back;
    }
#endif
}
