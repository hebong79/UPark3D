using UnityEngine;
using UnityEngine.UI;
using UnityEngine.EventSystems;

public class CPCamDistDlg : CDialogUI, IBeginDragHandler, IDragHandler, IEndDragHandler
{
    public CPCamObjListUI m_CamObjListUI = null;  // 카메라 오브젝트 리스트 UI

    [HideInInspector] public CPCamControlDlg m_CamControlDlg = null; // 배타 피킹 조정용 (CPCamControlDlg가 Start에서 주입)

    [Header("UI 요소")]
    public Button m_btnCreateTargetPoint; // 타겟점 생성
    public Text m_txtDistance;            // 카메라와 타겟점 사이의 거리 표시 텍스트
    public Text m_txtHeight;              // 카메라 바닥까지 거리 표시 텍스트
    //public Text m_txtCamCarAnlge;         // 타겟라인 0° 기준점까지 수평거리 표시 텍스트
    public Text m_txtAngle;               // 타겟포인트 수직각·수평각 표시
    [Space]
    public Button m_btnExit;              // 다이얼로그 닫기 버튼
    public Button m_btnSetTargetLine;     // 타겟 라인 설정 토글 버튼
    public Text m_txtCamTargetStartPoint; // 타겟라인 시작점의 카메라 기준 각도 텍스트
    public Text m_txtCamTargetEndPoint;   // 타겟라인 끝점의 카메라 기준 각도 텍스트
    //public Button m_btnShowCamPos;        // 폴대 보여주기/숨기기 버튼

    public LayerMask m_SelectableLayer;   // 바닥 레이캐스트용 레이어

    // ── 내부 상태 ────────────────────────────────────────────────────────────
    private CObjCamera m_CurCamera = null;
    private float m_fCamHeight = 0f;

    // 타겟 라인 상태 머신
    private enum ETargetLineState { None, WaitStart, WaitEnd, Done }
    private ETargetLineState m_eTargetLineState = ETargetLineState.None;
    private bool m_bTargetLineActive = false;

    // 씬 오브젝트
    private GameObject   m_goStartSphere      = null;
    private GameObject   m_goEndSphere        = null;
    private GameObject   m_goRefSphere        = null;   // 직교점(0° 기준) 빨간 구
    private LineRenderer m_TargetLineRenderer  = null;

    // 라인 월드 좌표 (XZ 각도 계산에 사용)
    private Vector3 m_vLineStart;
    private Vector3 m_vLineEnd;

    private const float LINE_EXTEND = 500f;   // 무한선 시각화 연장 길이(m)
    private const float SPHERE_SIZE = 0.3f;   // 기준점 구 크기

    public Vector3 m_vVerticalPosWithCam; // 카메라와 수직인 지점의 월드 좌표 (0° 기준점)

    // ─────────────────────────────────────────────────────────────────────────

    public void SetCameraHeight(float fHeight)
    {
        m_fCamHeight = fHeight;
        PrintDistanceToCamera(m_fCamHeight);
    }

    void Start()
    {
        m_btnCreateTargetPoint.onClick.AddListener(OnClicked_CreateTargetPoint);
        //m_btnShowCamPos.onClick.AddListener(OnClicked_ShowPole);
        m_btnSetTargetLine.onClick.AddListener(OnClicked_SetTargetLine);

        m_btnExit.onClick.AddListener(() => { CloseUI(); });  
    }

    void Update()
    {
        // 타겟포인트 활성 중: 거리·각도 실시간 갱신
        if (m_CamObjListUI != null
            && m_CamObjListUI.m_TargetPosObj != null
            && m_CamObjListUI.m_TargetPosObj.activeSelf)
        {
            PrintDistanceToCamera(m_fCamHeight);
        }

        if (!m_bTargetLineActive) return;
        if (m_eTargetLineState == ETargetLineState.None) return;

        // Done 상태: 카메라 이동·회전 시 각도 실시간 갱신
        if (m_eTargetLineState == ETargetLineState.Done)
        {
            UpdateAngleDisplay();
            return;
        }

        // UI 위 클릭은 무시 / 통일 제스처: Ctrl + 좌클릭
        if (EventSystem.current != null && EventSystem.current.IsPointerOverGameObject()) return;
        if (!Input.GetKey(KeyCode.LeftControl)) return;
        if (!Input.GetMouseButtonDown(0)) return;
        if (Camera.main == null) return;

        Ray ray = Camera.main.ScreenPointToRay(Input.mousePosition);
        if (!Physics.Raycast(ray, out RaycastHit hit, Mathf.Infinity, m_SelectableLayer)) return;
        if (!hit.collider.CompareTag("Floor")) return;

        if (m_eTargetLineState == ETargetLineState.WaitStart)
        {
            PlaceStartPoint(hit.point);
            m_eTargetLineState = ETargetLineState.WaitEnd;
            SetTargetLineButtonUI(true, "끝점 클릭");
        }
        else if (m_eTargetLineState == ETargetLineState.WaitEnd)
        {
            PlaceEndPoint(hit.point);
            m_eTargetLineState = ETargetLineState.Done;
            SetTargetLineButtonUI(true, "라인 완성");
            UpdateAngleDisplay();
        }
    }

    // ── 타겟 라인 토글 ────────────────────────────────────────────────────────

    void OnClicked_SetTargetLine()
    {
        m_bTargetLineActive = !m_bTargetLineActive;

        // true→false / false→true 모두 기존 라인 완전 삭제
        ClearTargetLine();
        // 라인을 (다시) 그리거나 끄는 동안에는 타겟점도 끈다.
        // (타겟점은 완성된 라인이 화면에 있어야 찍을 수 있으므로)
        ForceStopTargetPoint();

        if (m_bTargetLineActive)
        {
            // 카메라 피킹만 배타로 끈다. (타겟라인 ↔ 타겟점은 동시 사용)
            m_CamControlDlg?.ForceStopCameraPicking();

            m_eTargetLineState = ETargetLineState.WaitStart;
            SetTargetLineButtonUI(true, "시작점 클릭");
        }
        else
        {
            m_eTargetLineState = ETargetLineState.None;
            SetTargetLineButtonUI(false, "타겟라인 설정");
        }
    }

    // ── 점 배치 ───────────────────────────────────────────────────────────────

    void PlaceStartPoint(Vector3 worldPos)
    {
        m_vLineStart = worldPos;

        if (m_goStartSphere == null)
            m_goStartSphere = CreatePointSphere(worldPos, Color.green);
        else
            m_goStartSphere.transform.position = worldPos;

        if (m_txtCamTargetStartPoint != null)
            m_txtCamTargetStartPoint.text = "-- (끝점 대기)";
    }

    void PlaceEndPoint(Vector3 worldPos)
    {
        m_vLineEnd = worldPos;

        if (m_goEndSphere == null)
            m_goEndSphere = CreatePointSphere(worldPos, Color.green);
        else
            m_goEndSphere.transform.position = worldPos;

        if (m_TargetLineRenderer == null)
            m_TargetLineRenderer = CreateLineRenderer();

        RefreshLineVisual();

        // 좌표는 라인이 확정된 이후 1회만 갱신 (정적 정보)
        UpdateCoordinatesDisplay();
    }

    /// <summary>시작점·끝점 월드 좌표를 m_txtCamTargetStartPoint 에 표시한다.</summary>
    void UpdateCoordinatesDisplay()
    {
        if (m_txtCamTargetStartPoint == null) return;
        Vector3 s = m_vLineStart;
        Vector3 e = m_vLineEnd;
        m_txtCamTargetStartPoint.text = $"({s.x:F2},{s.y:F2},{s.z:F2} ~ ({e.x:F2},{e.y:F2},{e.z:F2})";
        //m_txtCamTargetEndPoint.text = $"({e.x:F2},{e.y:F2},{e.z:F2})";
    }

    // ── 라인 시각화 (무한선 연장) ─────────────────────────────────────────────

    void RefreshLineVisual()
    {
        if (m_TargetLineRenderer == null) return;

        Vector3 dir = m_vLineEnd - m_vLineStart;
        if (dir.sqrMagnitude < 0.0001f) return;
        dir.Normalize();

        // 바닥에서 살짝 띄워 Z-fighting 방지
        float y  = Mathf.Max(m_vLineStart.y, m_vLineEnd.y) + 0.05f;
        Vector3 p0 = m_vLineStart - dir * LINE_EXTEND; p0.y = y;
        Vector3 p1 = m_vLineEnd   + dir * LINE_EXTEND; p1.y = y;

        m_TargetLineRenderer.SetPosition(0, p0);
        m_TargetLineRenderer.SetPosition(1, p1);
    }

    // ── 각도 계산 & 표시 ──────────────────────────────────────────────────────

    /// <summary>
    /// 카메라 → 직교점(수선의 발) 방향을 0° 기준으로 시작점·끝점의 수평각을 표시한다.
    ///
    /// · 0°  = 카메라에서 수선의 발까지의 방향 (라인과 수직)
    /// · (-)  = 직교점 기준 왼쪽
    /// · (+)  = 직교점 기준 오른쪽
    ///
    /// 직교점이 두 끝점보다 왼쪽에 있으면 → 두 점 모두 (+) 출력
    /// </summary>
    void UpdateAngleDisplay()
    {
        if (m_CurCamera == null) return;

        // 모든 계산을 XZ 평면에서 수행
        Vector3 camPos    = new Vector3(m_CurCamera.transform.position.x, 0f, m_CurCamera.transform.position.z);
        Vector3 lineStart = new Vector3(m_vLineStart.x, 0f, m_vLineStart.z);
        Vector3 lineEnd   = new Vector3(m_vLineEnd.x,   0f, m_vLineEnd.z);
        Vector3 lineDir   = (lineEnd - lineStart).normalized;

        if (lineDir.sqrMagnitude < 0.001f) return;

        // 수선의 발: 카메라 XZ → 직교점 (0° 기준이자 빨간 구 위치)
        float   t     = Vector3.Dot(camPos - lineStart, lineDir);
        Vector3 refPt = lineStart + t * lineDir;

        float   refY        = Mathf.Max(m_vLineStart.y, m_vLineEnd.y) + 0.05f;
        Vector3 refWorldPos = new Vector3(refPt.x, refY, refPt.z);
        if (m_goRefSphere == null)
            m_goRefSphere = CreatePointSphere(refWorldPos, Color.blue);
        else
            m_goRefSphere.transform.position = refWorldPos;

        m_vVerticalPosWithCam = refWorldPos;
        // 0° 기준 방향: 카메라 → 직교점
        Vector3 refDir = refPt - camPos;
        if (refDir.sqrMagnitude < 0.001f)
        {
            // 카메라가 라인 바로 위(직교점 = 카메라 위치) → 계산 불가
            if (m_txtCamTargetEndPoint != null) m_txtCamTargetEndPoint.text = "N/A";
            return;
        }
        refDir.Normalize();

        // 각도 계산: cross(refDir, toPoint).y 부호로 좌/우 판별
        //   cross(...).y > 0  →  toPoint가 refDir 오른쪽  →  (+)
        //   cross(...).y < 0  →  toPoint가 refDir 왼쪽    →  (-)
        Vector3 toStart = lineStart - camPos;
        Vector3 toEnd   = lineEnd   - camPos;
        if (toStart.sqrMagnitude < 0.001f || toEnd.sqrMagnitude < 0.001f) return;

        float angleStart = Vector3.SignedAngle(refDir, toStart.normalized, Vector3.up);
        float angleEnd   = Vector3.SignedAngle(refDir, toEnd.normalized,   Vector3.up);

        if (m_txtCamTargetEndPoint != null)
            m_txtCamTargetEndPoint.text =
                $"시작:{FormatAngle(angleStart)} / 끝:{FormatAngle(angleEnd)}";
    }

    static string FormatAngle(float angle)
        => angle >= 0f ? $"+{angle:F1}°" : $"{angle:F1}°";

    // ── 초기화 ────────────────────────────────────────────────────────────────

    void ClearTargetLine()
    {
        if (m_goStartSphere      != null) { Destroy(m_goStartSphere);                  m_goStartSphere      = null; }
        if (m_goEndSphere        != null) { Destroy(m_goEndSphere);                    m_goEndSphere        = null; }
        if (m_goRefSphere        != null) { Destroy(m_goRefSphere);                    m_goRefSphere        = null; }
        if (m_TargetLineRenderer != null) { Destroy(m_TargetLineRenderer.gameObject);  m_TargetLineRenderer = null; }

        if (m_txtCamTargetStartPoint != null) m_txtCamTargetStartPoint.text = "--";
        if (m_txtCamTargetEndPoint   != null) m_txtCamTargetEndPoint.text   = "--";
        //if (m_txtCamCarAnlge         != null) m_txtCamCarAnlge.text         = "--";

    }

    // ── 씬 오브젝트 생성 ──────────────────────────────────────────────────────

    GameObject CreatePointSphere(Vector3 worldPos, Color kColor)
    {
        GameObject go = GameObject.CreatePrimitive(PrimitiveType.Sphere);
        go.name = "TargetLineSphere";
        go.transform.position   = worldPos;
        go.transform.localScale = Vector3.one * SPHERE_SIZE;

        Renderer rend = go.GetComponent<Renderer>();
        if (rend != null)
        {
            Shader shader = Shader.Find("Universal Render Pipeline/Lit");   //Shader.Find("Standard") ??
            rend.material = new Material(shader) { color = kColor };
        }

        // 구 콜라이더가 Floor 레이캐스트를 막지 않도록 제거
        Collider col = go.GetComponent<Collider>();
        if (col != null) Destroy(col);

        return go;
    }

    LineRenderer CreateLineRenderer()
    {
        GameObject   go = new GameObject("TargetLineRenderer");
        LineRenderer lr = go.AddComponent<LineRenderer>();
        lr.useWorldSpace = true;
        lr.positionCount = 2;
        lr.startWidth    = 0.05f;
        lr.endWidth      = 0.05f;
        lr.material      = new Material(Shader.Find("Sprites/Default"));
        lr.startColor    = Color.green;
        lr.endColor      = Color.green;
        return lr;
    }

    // ── UI 헬퍼 ───────────────────────────────────────────────────────────────

    void SetTargetLineButtonUI(bool bActive, string label)
    {
        if (m_btnSetTargetLine == null) return;
        Text kText = m_btnSetTargetLine.GetComponentInChildren<Text>();
        if (kText == null) return;
        kText.text  = label;
        kText.color = bActive ? Color.red : Color.black;
    }

    void SetCreateTargetPointButtonUI(bool bActive)
    {
        if (m_btnCreateTargetPoint == null) return;
        Text kText = m_btnCreateTargetPoint.GetComponentInChildren<Text>();
        if (kText == null) return;
        kText.text  = bActive ? "설치중" : "타겟점 설치";
        kText.color = bActive ? Color.red : Color.black;
    }

    /// <summary>타겟라인이 화면에 출력(시작점·끝점 완성, Done)된 상태인지 여부.</summary>
    bool IsTargetLineDisplayed()
    {
        return m_bTargetLineActive && m_eTargetLineState == ETargetLineState.Done;
    }

    // ── 배타 피킹 모드: 외부에서 강제 해제 ──────────────────────────────────────

    /// <summary>외부(카메라 피킹/타겟라인)에서 타겟점 설치 모드를 강제로 끈다. 이미 꺼져 있으면 무시.</summary>
    public void ForceStopTargetPoint()
    {
        if (m_CamObjListUI == null || m_CamObjListUI.m_TargetPosObj == null) return;
        if (!m_CamObjListUI.m_TargetPosObj.activeSelf) return;
        m_CamObjListUI.m_TargetPosObj.SetActive(false);
        SetCreateTargetPointButtonUI(false);
    }

    /// <summary>외부(카메라 피킹/타겟점)에서 타겟라인 설정 모드를 강제로 끈다. 이미 꺼져 있으면 무시.</summary>
    public void ForceStopTargetLine()
    {
        if (!m_bTargetLineActive) return;
        m_bTargetLineActive = false;
        m_eTargetLineState  = ETargetLineState.None;
        ClearTargetLine();
        SetTargetLineButtonUI(false, "타겟라인 설정");
    }

    // ── 기존 기능 (변경 없음) ─────────────────────────────────────────────────

    public void SetCurCarmera(CObjCamera cam)
    {
        m_CurCamera = cam;
        if (m_CurCamera == null)
        {
            m_txtDistance.text    = "--";
            m_txtHeight.text      = "--";
            //m_txtCamCarAnlge.text = "--";
            if (m_txtAngle != null) m_txtAngle.text = "--";
            //SetPoleButtonUI(false);
        }
    }

    public void OnClicked_CreateTargetPoint()
    {
        if (m_CamObjListUI.IsCreateTargetPoint())
        {
            m_CamObjListUI.m_TargetPosObj?.SetActive(false);
            SetCreateTargetPointButtonUI(false);
        }
        else
        {
            // 타겟점은 타겟라인이 화면에 출력(시작점·끝점 완성)된 상태에서만 찍을 수 있다.
            // (타겟점의 수평각이 타겟라인 기준점(m_vVerticalPosWithCam)에 의존하기 때문)
            if (!IsTargetLineDisplayed())
            {
                Debug.LogWarning("[CPCamDistDlg] 타겟라인을 먼저 그린 후 타겟점을 설치할 수 있습니다.");
                return;
            }

            // 카메라 피킹만 배타로 끈다. (타겟라인은 동시 사용)
            m_CamControlDlg?.ForceStopCameraPicking();

            m_CamObjListUI.m_TargetPosObj?.SetActive(true);
            SetCreateTargetPointButtonUI(true);

            PrintDistanceToCamera(m_fCamHeight);
        }
    }

    public void PrintDistanceToCamera(float fCamHeight)
    {
        float fDist = m_CamObjListUI.CalcualteDistanceToCameraBottom();
        if (m_txtDistance != null)
            m_txtDistance.text = $"{fDist:F2} m";

        float fHeight = fCamHeight <= 0 ? GetCameraHeightFromFloor() : fCamHeight;
        if (m_txtHeight != null)
            m_txtHeight.text = $"{fHeight:F2} m";

        PrintAngleToTarget();
    }

    /// <summary>
    /// 카메라 → 타겟포인트의 수직각(내림각)과 수평각을 계산하여 m_txtAngle에 표시한다.
    ///
    /// · 수직각 : atan2(높이차, XZ거리) — 양수(↓) = 내려다봄, 음수(↑) = 올려다봄
    /// · 수평각 : 카메라 → 수직점 방향 기준 타겟 방향의 signed angle
    ///            + = 오른쪽,  - = 왼쪽
    /// </summary>
    void PrintAngleToTarget()
    {
        if (m_txtAngle == null) return;

        if (m_CurCamera == null
            || m_CamObjListUI == null
            || m_CamObjListUI.m_TargetPosObj == null)
        {
            m_txtAngle.text = "--";
            return;
        }

        Vector3 camPos = m_CurCamera.transform.position;
        Vector3 tgtPos = m_CamObjListUI.m_TargetPosObj.transform.position;

        // 수평 벡터 (카메라 → 타겟, Y 제거)
        Vector3 toTargetXZ = new Vector3(tgtPos.x - camPos.x, 0f, tgtPos.z - camPos.z);
        float   hDist      = toTargetXZ.magnitude;

        // ── 수직각 ────────────────────────────────────────────────────────────
        float deltaY    = camPos.y - tgtPos.y;          // 양수 = 카메라가 위
        float vertAngle = hDist > 0.001f
            ? Mathf.Atan2(deltaY, hDist) * Mathf.Rad2Deg
            : (deltaY >= 0f ? 90f : -90f);

        // ── 수평각 ────────────────────────────────────────────────────────────
        float horzAngle = 0f;
        if (hDist > 0.001f)
        {
            Vector3 vBaseDir = m_vVerticalPosWithCam - camPos; // 카메라 → 수직점 방향 (0° 기준)
            vBaseDir.y = 0f; // XZ 평면 수평각 비교 — Y 제거하지 않으면 카메라 높이만큼 각도 오프셋 발생
            if (vBaseDir.sqrMagnitude > 0.00001f)
                horzAngle = Vector3.SignedAngle(vBaseDir.normalized, toTargetXZ.normalized, Vector3.up);
        }

        string vertStr = vertAngle >= 0f
            ? $"↓{vertAngle:F1}°"
            : $"↑{Mathf.Abs(vertAngle):F1}°";
        string horzStr = horzAngle >= 0f ? $"+{horzAngle:F1}°" : $"{horzAngle:F1}°";

        m_txtAngle.text = $"수직:{vertStr}  수평:{horzStr}";
    }

    /// <summary>
    /// 카메라 위치에서 바닥까지 아래 방향 Raycast로 수직 거리를 반환한다.
    /// Floor 레이어에 닿지 않으면 카메라 Y 좌표를 대신 반환한다.
    /// </summary>
    float GetCameraHeightFromFloor()
    {
        if (m_CurCamera == null) return 0f;

        Vector3 camPos  = m_CurCamera.transform.position;
        Ray     downRay = new Ray(camPos, Vector3.down);

        if (Physics.Raycast(downRay, out RaycastHit hit, Mathf.Infinity, m_SelectableLayer))
            return hit.distance;

        return camPos.y;
    }

    ///// <summary>
    ///// 폴대 보여주기/숨기기 토글.
    ///// </summary>
    //void OnClicked_ShowPole()
    //{
    //    if (m_CurCamera == null) return;

    //    if (m_CurCamera.m_goPole == null)
    //    {
    //        GameObject polePrefab = m_CamObjListUI != null ? m_CamObjListUI.m_PolePrefab : null;
    //        if (polePrefab == null)
    //        {
    //            Debug.LogWarning("[CPCamDistDlg] CPCamObjListUI.m_PolePrefab이 설정되지 않았습니다.");
    //            return;
    //        }
    //        m_CurCamera.CreatePole(polePrefab);
    //        SetPoleButtonUI(true);
    //    }
    //    else
    //    {
    //        bool bNextVisible = !m_CurCamera.m_goPole.activeSelf;
    //        if (bNextVisible)
    //            m_CurCamera.UpdatePolePosition();
    //        m_CurCamera.ShowPole(bNextVisible);
    //        SetPoleButtonUI(bNextVisible);
    //    }
    //}

    //void SetPoleButtonUI(bool bVisible)
    //{
    //    if (m_btnShowCamPos == null) return;
    //    Text kText = m_btnShowCamPos.GetComponentInChildren<Text>();
    //    if (kText == null) return;
    //    kText.text  = bVisible ? "폴대 숨기기" : "폴대 보여주기";
    //    kText.color = bVisible ? Color.red : Color.black;
    //}

    /// <summary>
    /// 카메라 위치에서 targetPos 까지의 XZ 수평 거리를 반환한다.
    /// </summary>
    public float CalcDistanceToPoint(Vector3 targetPos)
    {
        if (m_CurCamera == null) return 0f;

        Vector3 diff = targetPos - m_CurCamera.transform.position;
        diff.y = 0f;
        return diff.magnitude;
    }

    public void OnBeginDrag(PointerEventData data) { }
    public void OnDrag(PointerEventData data) { }
    public void OnEndDrag(PointerEventData data) { }
}
