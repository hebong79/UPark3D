using System.Collections.Generic;
using UnityEngine;

/// <summary>
/// PTZ 공간 이미지 메이커용 차량 배치/제거 유틸리티.
///
/// 배치 조건 (모두 만족해야 스폰):
///   (1) 현재 카메라 뷰 안 — ViewportPointToRay 기반 랜덤 샘플링으로 보장
///   (2) 주차장 맵 콜라이더 위 — Physics.Raycast 하방 검증
///   (3) 기존 차량과 겹치지 않음 — XZ 최소 거리 검사
///   (4) 신규 차량 번호판이 카메라에서 보임 — 번호판 면적 다중 지점(코너4+중심) 샘플링 레이캐스트
///   (5) 기존 차량 번호판을 가리지 않음 — 동일 다중 지점 샘플링 시선 재검증
///   (6) 화면 2D 박스 겹침 방지 — 월드 AABB 8코너 투영 후 Rect 겹침 비율 임계 검사
/// </summary>
public static class CPtzSpaceVehiclePlacer
{
    private const float kMapCheckRayHeight = 50f;

    // IsPlateVisible 내 RaycastNonAlloc 재사용 버퍼 (호출이 단일 스레드이므로 static 공유 안전)
    private static readonly RaycastHit[] kRayHitBuffer = new RaycastHit[32];

    /// <summary>
    /// 현재 카메라 뷰 안이면서 주차장 맵 위에 차량을 랜덤 배치한다.
    /// 차량 겹침과 번호판 가림을 모두 방지한다.
    /// </summary>
    /// <param name="cam">PTZ 카메라 오브젝트</param>
    /// <param name="carListUI">차량 목록 UI</param>
    /// <param name="count">배치 대수</param>
    /// <param name="groundY">지면 높이 (기본값 0)</param>
    /// <param name="insetRatio">뷰포트 가장자리 제외 비율(0~1)</param>
    /// <param name="maxRetryPerCar">차량 1대당 유효 위치 탐색 최대 시도 횟수</param>
    /// <param name="minVehicleSpacingM">차량 간 최소 XZ 거리(m). 물리적 겹침 방지.</param>
    /// <param name="boxMin">맵 사각박스 최소 XZ. 지정 시 "박스 안" 후보를 유효 지면으로 인정(콜라이더 불필요).</param>
    /// <param name="boxMax">맵 사각박스 최대 XZ.</param>
    /// <param name="maxScreenOverlapRatio">신규·기존 차량 화면 박스 겹침 허용 최대 비율(교차/작은박스). 초과 시 배치 거부.</param>
    public static List<CObjCar> PlaceVehicles(
        CObjCamera    cam,
        CCarObjListUI carListUI,
        int           count,
        float         groundY               = 0f,
        float         insetRatio            = 0.05f,
        int           maxRetryPerCar        = 40,
        float         minVehicleSpacingM    = 4f,
        Vector3       boxMin                = default,
        Vector3       boxMax                = default,
        float         maxScreenOverlapRatio = 0.05f)
    {
        var placed        = new List<CObjCar>();
        var placedCenters = new List<Vector3>();
        var placedRects   = new List<Rect>();

        // 진단 카운터: 차량 0대일 때 원인을 콘솔로 즉시 파악하기 위함(요구사항: 근본원인 추적)
        int failRay = 0, failGround = 0, failOverlap = 0, failSpawn = 0, failPlate = 0, failOccl = 0, failScreenOverlap = 0, failZoom = 0;

        if (cam == null || carListUI == null || count <= 0) return placed;
        if (carListUI.m_PrefabList == null || carListUI.m_PrefabList.Count == 0)
        {
            Debug.LogWarning("[PlaceVehicles] 프리팹 리스트가 비어 있습니다 → 차량 0대.");
            return placed;
        }

        Camera kCamera = cam.getCamera;
        if (kCamera == null)
        {
            Debug.LogWarning("[PlaceVehicles] cam.getCamera 가 null 입니다 → 차량 0대.");
            return placed;
        }

        // 맵 박스가 유효한 면적을 가지면 "박스 안"을 유효 지면으로 사용(콜라이더 없어도 배치).
        bool hasBox = Mathf.Abs(boxMax.x - boxMin.x) > 1e-3f && Mathf.Abs(boxMax.z - boxMin.z) > 1e-3f;

        // 카메라 XZ 월드 위치 (각 CObjCamera 고유 위치 — m_PCamera 공유 여부와 무관)
        Vector3 camWorldPosXZ = new Vector3(cam.transform.position.x, 0f, cam.transform.position.z);

        // ① 거리 상한: zoom = DMAX_ZOOM 이 되는 카메라↔번호판 임계 거리.
        //   동일 공식(CLicensePlateFocusCapture.CalcVerticalFOV)에서 역산하므로 포커스 로직과 일관.
        //   카메라 aspect는 현재 카메라 컴포넌트에서 얻는다(없으면 1.0 fallback).
        float camAspect = (kCamera != null && kCamera.aspect > 0f) ? kCamera.aspect : 1f;
        float kMaxPlaceDist = CalcPlateZoomThresholdDist(cam.DMAX_ZOOM, camAspect);
        Debug.Log($"[PlaceVehicles] 카메라↔번호판 거리 상한: {kMaxPlaceDist:F1}m (zoom={cam.DMAX_ZOOM}에 해당)");   

        float margin      = Mathf.Clamp01(insetRatio);
        float minSpacingSq = minVehicleSpacingM * minVehicleSpacingM;

        // kMaxPlaceDist = kMaxPlaceDist * 0.9f; // 임계 거리에 딱 붙는 배치는 불안정하므로 약간 여유를 둔다(실험적으로 0.9가 적당).

        for (int i = 0; i < count; i++)
        {
            CObjCar car   = null;
            bool    found = false;

            for (int retry = 0; retry < maxRetryPerCar; retry++)
            {
                // ② 거리 균등 샘플링: v(상하 뷰포트)를 거리 역비례로 보정하여 원근 편향 제거.
                //   u(좌우)는 균등 유지. v는 원거리(큰 v)~근거리(작은 v) 사이에서 거리가 균등해지도록 샘플.
                float u = UnityEngine.Random.Range(margin, 1f - margin);
                float v = SampleDistanceUniformV(kCamera, groundY, margin, kMaxPlaceDist);
                Ray ray = kCamera.ViewportPointToRay(new Vector3(u, v, 0f));

                if (ray.direction.y >= -1e-4f) { failRay++; continue; }   // 하향 레이 아님
                float t = (groundY - ray.origin.y) / ray.direction.y;
                if (t <= 0f) { failRay++; continue; }

                Vector3 candidate = ray.GetPoint(t);
                candidate.y = groundY;

                // ① 거리 상한: candidate가 임계 거리(zoom=DMAX_ZOOM에 해당) 초과 시 reject.
                //   번호판 위치는 지면점과 거의 일치하므로 지면 candidate 거리로 근사한다.
                float candidateDist = Vector3.Distance(kCamera.transform.position, candidate);
                if (candidateDist > kMaxPlaceDist) { failZoom++; continue; }

                // ② 지면 유효성: "맵 박스 안"이거나(콜라이더 불필요) 또는 주차장 콜라이더 위.
                //    [근본원인 대응] 런타임 주차장 콜라이더가 없거나 레이어가 달라도 박스 안이면 배치 허용.
                bool inBox      = hasBox && IsWithinMapBox(candidate, boxMin, boxMax);
                bool onCollider = IsOnParkingLot(candidate, groundY);
                if (!inBox && !onCollider) { failGround++; continue; }

                // ③ 기존 차량과 XZ 겹침 검사
                if (IsOverlapping(candidate, placedCenters, minSpacingSq)) { failOverlap++; continue; }

                // ④ 차량 회전각 계산: 차량 위치 → 현재 카메라 위치 방향
                Vector3 toCamera = camWorldPosXZ - new Vector3(candidate.x, 0f, candidate.z);
                float rotY;
                if (toCamera.sqrMagnitude < 0.001f)
                {
                    // 극히 드문 경우(차량이 카메라 바로 아래): 카메라 forward 역방향으로 폴백
                    Vector3 fwd = cam.m_PCamera != null
                        ? cam.m_PCamera.transform.forward
                        : Vector3.forward;
                    fwd.y = 0f;
                    if (fwd.sqrMagnitude < 0.001f) fwd = Vector3.forward;
                    else fwd.Normalize();
                    rotY = Mathf.Atan2(-fwd.x, -fwd.z) * Mathf.Rad2Deg;
                }
                else
                {
                    toCamera.Normalize();
                    rotY = Mathf.Atan2(toCamera.x, toCamera.z) * Mathf.Rad2Deg;
                }

                // ⑤ 차량 스폰 후 번호판 가시성 검사
                car = SpawnCar(carListUI, candidate, rotY, kCamera);
                if (car == null) { failSpawn++; continue; }

                // ⑤-1 신규 차량 번호판이 보이는가
                if (!IsPlateVisible(kCamera, car, null))
                {
                    carListUI.RemoveCarObject(car);
                    car = null;
                    failPlate++;
                    continue;
                }

                // ⑤-2 기존 차량 번호판을 신규 차량이 가리는가
                bool occludes = false;
                foreach (var prev in placed)
                {
                    if (!IsPlateVisible(kCamera, prev, car))
                    {
                        occludes = true;
                        break;
                    }
                }
                if (occludes)
                {
                    carListUI.RemoveCarObject(car);
                    car = null;
                    failOccl++;
                    continue;
                }

                // ⑤-3 화면 2D 박스 겹침 검사
                if (!TryGetScreenRect(kCamera, car, out Rect newRect))
                {
                    carListUI.RemoveCarObject(car);
                    car = null;
                    failScreenOverlap++;
                    continue;
                }
                bool screenOverlap = false;
                for (int ri = 0; ri < placedRects.Count; ri++)
                {
                    if (ScreenRectOverlapRatio(newRect, placedRects[ri]) > maxScreenOverlapRatio)
                    {
                        screenOverlap = true;
                        break;
                    }
                }
                if (screenOverlap)
                {
                    carListUI.RemoveCarObject(car);
                    car = null;
                    failScreenOverlap++;
                    continue;
                }

                // ⑥ 실제 zoom 포화 검사: manifest(CVlaPSImgMakerGameUI.Enum_CaptureAtPTZ)와 동일한
                //   CalcTargetPTZ → cam.ZoomFromFov(수직 FOV) 경로로 zoom을 산출하여 DMAX_ZOOM 이상이면
                //   포화 번호판(겉보기 폭 작음 = 비스듬·좁은 프리팹)이므로 reject·재샘플.
                //   ① kMaxPlaceDist 프리필터(공칭 폭 기준)가 통과시킨 케이스를 최종 차단하는 권위 게이트.
                if (CLicensePlateFocusCapture.CalcTargetPTZ(
                        cam, car, useFrontPlate: true,
                        out _, out _, out float fFovActual))
                {
                    // manifest 산출식: plateZoom = cam.ZoomFromFov(vFov) — 수직→수평→탄젠트역산
                    float rawPlateZoom = fFovActual > 0f
                        ? cam.ZoomFromFov(fFovActual)
                        : cam.DMAX_ZOOM;
                    if (rawPlateZoom >= cam.DMAX_ZOOM)
                    {
                        carListUI.RemoveCarObject(car);
                        car = null;
                        failZoom++;
                        continue;
                    }
                }

                found = true;
                break;
            }

            if (!found || car == null) continue;

            placed.Add(car);
            placedCenters.Add(car.transform.position);
            TryGetScreenRect(kCamera, car, out Rect placedRect);
            placedRects.Add(placedRect);
        }

        // 진단 요약: 차량이 적게/0대 배치되면 어떤 단계가 막혔는지 한 줄로 보고.
        if (placed.Count < count)
        {
            Debug.Log(
                $"[PlaceVehicles] 요청 {count}대 / 배치 {placed.Count}대 | hasBox={hasBox} groundY={groundY} maxDist={kMaxPlaceDist:F1}m | " +
                $"실패: 하향레이 {failRay}, zoom포화(거리초과·겉보기폭작음) {failZoom}, 박스밖·콜라이더없음 {failGround}, 겹침 {failOverlap}, " +
                $"스폰실패 {failSpawn}, 번호판가림 {failPlate}, 기존가림 {failOccl}, 화면겹침 {failScreenOverlap}");
        }

        return placed;
    }

    /// <summary>배치된 차량을 오브젝트 풀로 반환하고 리스트를 비운다.</summary>
    public static void RemoveVehicles(List<CObjCar> vehicles, CCarObjListUI carListUI)
    {
        if (vehicles == null || carListUI == null) return;
        for (int i = vehicles.Count - 1; i >= 0; i--)
            carListUI.RemoveCarObject(vehicles[i]);
        vehicles.Clear();
    }

    // ── 공개 유틸리티 ──────────────────────────────────────────

    /// <summary>
    /// vehicle의 모든 Renderer bounds를 합친 월드 AABB 8코너를 화면 좌표로 투영해 2D Rect를 반환한다.
    /// Renderer가 없거나 카메라 뒤(z &lt;= 0) 코너가 있으면 false(보수적 처리).
    /// </summary>
    public static bool TryGetScreenRect(Camera camera, CObjCar vehicle, out Rect rect)
    {
        rect = default;
        if (camera == null || vehicle == null) return false;

        Renderer[] renderers = vehicle.GetComponentsInChildren<Renderer>();
        if (renderers.Length == 0) return false;

        Bounds bounds = renderers[0].bounds;
        for (int i = 1; i < renderers.Length; i++)
            bounds.Encapsulate(renderers[i].bounds);

        // AABB 8코너
        Vector3 min = bounds.min;
        Vector3 max = bounds.max;
        Vector3[] corners = new Vector3[8]
        {
            new Vector3(min.x, min.y, min.z),
            new Vector3(max.x, min.y, min.z),
            new Vector3(min.x, max.y, min.z),
            new Vector3(max.x, max.y, min.z),
            new Vector3(min.x, min.y, max.z),
            new Vector3(max.x, min.y, max.z),
            new Vector3(min.x, max.y, max.z),
            new Vector3(max.x, max.y, max.z),
        };

        float xMin = float.MaxValue, xMax = float.MinValue;
        float yMin = float.MaxValue, yMax = float.MinValue;

        foreach (var corner in corners)
        {
            Vector3 sp = camera.WorldToScreenPoint(corner);
            if (sp.z <= 0f) return false;   // 카메라 뒤 코너 → 보수적으로 거부
            if (sp.x < xMin) xMin = sp.x;
            if (sp.x > xMax) xMax = sp.x;
            if (sp.y < yMin) yMin = sp.y;
            if (sp.y > yMax) yMax = sp.y;
        }

        rect = Rect.MinMaxRect(xMin, yMin, xMax, yMax);
        return true;
    }

    /// <summary>
    /// 두 Rect의 교차 면적 / 두 박스 중 작은 박스 면적을 반환한다. 겹침 없으면 0.
    /// </summary>
    public static float ScreenRectOverlapRatio(Rect a, Rect b)
    {
        float ix = Mathf.Max(0f, Mathf.Min(a.xMax, b.xMax) - Mathf.Max(a.xMin, b.xMin));
        float iy = Mathf.Max(0f, Mathf.Min(a.yMax, b.yMax) - Mathf.Max(a.yMin, b.yMin));
        float intersection = ix * iy;
        if (intersection <= 0f) return 0f;

        float areaA = a.width * a.height;
        float areaB = b.width * b.height;
        float smaller = Mathf.Min(areaA, areaB);
        if (smaller <= 0f) return 0f;

        return intersection / smaller;
    }

    /// <summary>
    /// 카메라에서 vehicle의 전면 번호판으로의 시선이 열려 있는지 확인한다.
    /// 번호판 Renderer.bounds에서 5개 표본점(코너4+중심)을 추출해 다중 레이캐스트.
    /// 하나라도 가리면 false(부분 가림도 가림으로 판정).
    /// ignoreCar를 지정하면 해당 차량의 콜라이더는 가림 판정에서 제외한다.
    /// (신규 차량이 기존 차량 번호판을 가리는지 검사할 때 신규 차량 자신은 제외)
    /// </summary>
    public static bool IsPlateVisible(Camera camera, CObjCar vehicle, CObjCar ignoreCar = null)
    {
        if (vehicle == null || vehicle.m_FrontPlateNo == null) return true;

        Vector3   camPos  = camera.transform.position;
        Vector3[] samples = BuildPlateSamplePoints(vehicle.m_FrontPlateNo.transform, camPos);

        foreach (var samplePos in samples)
        {
            float dist = Vector3.Distance(camPos, samplePos);
            if (dist < 0.01f) continue;

            Vector3 dir      = (samplePos - camPos) / dist;
            int     hitCount = Physics.RaycastNonAlloc(
                camPos,
                dir,
                kRayHitBuffer,
                dist,
                Physics.DefaultRaycastLayers,
                QueryTriggerInteraction.Ignore);

            for (int h = 0; h < hitCount; h++)
            {
                RaycastHit hit = kRayHitBuffer[h];

                // 자기 자신 차량(vehicle) 콜라이더 → 무시
                CObjCar hitCar = hit.transform.GetComponentInParent<CObjCar>();
                if (hitCar == vehicle) continue;

                // 검사 제외 대상 차량(ignoreCar) → 무시
                if (ignoreCar != null && hitCar == ignoreCar) continue;

                // 지면 충돌 (hit.point.y ≈ 0) → 무시
                if (hit.point.y < 0.15f) continue;

                // 위 조건에 해당하지 않는 충돌 → 해당 표본점이 가려짐
                return false;
            }
        }
        return true;
    }

    /// <summary>
    /// 번호판 Transform의 자식 포함 Renderer.bounds에서
    /// 카메라에 가까운 면의 코너 4개 + 중심 1개, 총 5점을 반환한다.
    /// Renderer가 없으면 transform.position 1점으로 폴백.
    /// </summary>
    private static Vector3[] BuildPlateSamplePoints(Transform plateTf, Vector3 camPos)
    {
        // 자식 포함 Renderer 중 첫 번째를 사용 (번호판 오브젝트 구조 무관하게 동작)
        Renderer rend = plateTf.GetComponentInChildren<Renderer>();
        if (rend == null)
            return new Vector3[] { plateTf.position };

        Bounds b = rend.bounds;

        // bounds 8코너 중 카메라에 가장 가까운 면(카메라 방향 기준)을 근사하기 위해
        // X·Y 각 축에서 카메라에 가까운 쪽을 선택해 코너 4점을 구성.
        float cx = (camPos.x > b.center.x) ? b.max.x : b.min.x;
        float cy0 = b.min.y;
        float cy1 = b.max.y;
        float cz0 = b.min.z;
        float cz1 = b.max.z;

        // 번호판은 수직으로 세워져 있으므로 XZ 양 끝 + Y 양 끝으로 4코너 구성
        // Z축이 번호판 폭, Y축이 번호판 높이에 해당하는 경우를 포함하기 위해
        // 카메라에 가까운 X면 위에서 (Z0,Y0), (Z1,Y0), (Z1,Y1), (Z0,Y1) 4코너 사용.
        return new Vector3[]
        {
            new Vector3(cx, cy0, cz0),   // 하단-좌
            new Vector3(cx, cy0, cz1),   // 하단-우
            new Vector3(cx, cy1, cz1),   // 상단-우
            new Vector3(cx, cy1, cz0),   // 상단-좌
            b.center,                    // 중심
        };
    }

    /// <summary>
    /// candidate 위치가 기존 배치 차량 중심(placedCenters)과 minSpacingSq(m²) 미만 거리면 true.
    /// </summary>
    public static bool IsOverlapping(Vector3 candidate, List<Vector3> placedCenters, float minSpacingSq)
    {
        float cx = candidate.x, cz = candidate.z;
        foreach (var p in placedCenters)
        {
            float dx = cx - p.x, dz = cz - p.z;
            if (dx * dx + dz * dz < minSpacingSq) return true;
        }
        return false;
    }

    /// <summary>worldPos가 카메라 뷰포트 [0,1]×[0,1] 안에 있는지 확인한다.</summary>
    public static bool IsInCameraView(Camera camera, Vector3 worldPos)
    {
        Vector3 vp = camera.WorldToViewportPoint(worldPos);
        return vp.z > 0f && vp.x >= 0f && vp.x <= 1f && vp.y >= 0f && vp.y <= 1f;
    }

    /// <summary>pos(XZ)가 맵 사각박스 [min,max] 안에 있는지 검사한다.</summary>
    public static bool IsWithinMapBox(Vector3 pos, Vector3 boxMin, Vector3 boxMax)
    {
        float minX = Mathf.Min(boxMin.x, boxMax.x), maxX = Mathf.Max(boxMin.x, boxMax.x);
        float minZ = Mathf.Min(boxMin.z, boxMax.z), maxZ = Mathf.Max(boxMin.z, boxMax.z);
        return pos.x >= minX && pos.x <= maxX && pos.z >= minZ && pos.z <= maxZ;
    }

    /// <summary>
    /// pos 수직 하방에 주차장 맵 콜라이더(바닥)가 있는지 Physics.Raycast로 확인한다.
    /// 트리거 콜라이더는 무시한다.
    /// </summary>
    public static bool IsOnParkingLot(Vector3 pos, float groundY = 0f)
    {
        Vector3 origin = new Vector3(pos.x, groundY + kMapCheckRayHeight, pos.z);
        return Physics.Raycast(
            origin,
            Vector3.down,
            kMapCheckRayHeight + 1f,
            Physics.DefaultRaycastLayers,
            QueryTriggerInteraction.Ignore);
    }

    /// <summary>
    /// 카메라 뷰 프러스텀의 원거리(far) 4꼭짓점을 지면(Y=groundY) 평면에 투영한 사각형을 계산한다.
    /// 반환 순서: 0=BL, 1=TL, 2=TR, 3=BR.
    /// </summary>
    public static bool TryCalcFrustumGroundQuad(Camera camera, out Vector3[] quad, float groundY = 0f)
    {
        quad = null;
        if (camera == null) return false;

        var localCorners = new Vector3[4];
        camera.CalculateFrustumCorners(
            new Rect(0f, 0f, 1f, 1f),
            camera.farClipPlane,
            Camera.MonoOrStereoscopicEye.Mono,
            localCorners);

        Transform t      = camera.transform;
        Vector3   camPos = t.position;
        var       result = new Vector3[4];

        for (int i = 0; i < 4; i++)
        {
            Vector3 worldPt = t.TransformPoint(localCorners[i]);
            Vector3 dir     = worldPt - camPos;
            if (dir.y >= -1e-4f) return false;
            float tVal = (groundY - camPos.y) / dir.y;
            if (tVal < 0f) return false;
            result[i]   = camPos + tVal * dir;
            result[i].y = groundY;
        }
        quad = result;
        return true;
    }

    /// <summary>quad 내부의 임의 점을 이중선형 보간으로 반환한다.</summary>
    public static Vector3 SamplePointInQuad(Vector3[] quad)
    {
        float   u      = UnityEngine.Random.value;
        float   v      = UnityEngine.Random.value;
        Vector3 bottom = Vector3.Lerp(quad[0], quad[3], u);
        Vector3 top    = Vector3.Lerp(quad[1], quad[2], u);
        return Vector3.Lerp(bottom, top, v);
    }

    /// <summary>quad의 각 꼭짓점을 중심 방향으로 ratio 비율만큼 축소한 새 quad를 반환한다.</summary>
    public static Vector3[] InsetQuad(Vector3[] quad, float ratio)
    {
        Vector3 center = (quad[0] + quad[1] + quad[2] + quad[3]) * 0.25f;
        var result = new Vector3[4];
        for (int i = 0; i < 4; i++)
            result[i] = Vector3.Lerp(quad[i], center, ratio);
        return result;
    }

    // ── 내부 유틸리티 ────────────────────────────────────────────

    /// <summary>
    /// zoom = dmaxZoom 이 되는 카메라↔번호판 임계 거리(m)를 반환한다.
    /// CLicensePlateFocusCapture.CalcVerticalFOV 의 역산 공식:
    ///   tanHalfHFov = plateWidth / (2 * dist * targetScreenRatio)
    ///   zoom = tan(DEFAULT_FOV/2) / tan(hFOV/2)  (탄젠트 광학 모델)
    /// 를 역산하면:
    ///   hFovAtMax = CObjCamera.ZoomToHFov(dmaxZoom)  = 2·atan(tan(29°)/dmaxZoom)
    ///   tanHalfHFov = tan(hFovAtMax / 2)
    ///   dist = plateWidth / (2 * tanHalfHFov * targetScreenRatio)
    /// </summary>
    private static float CalcPlateZoomThresholdDist(float dmaxZoom, float aspect)
    {
        // 탄젠트 광학 모델: horizontalFov at max zoom
        float hFovAtMax = CObjCamera.ZoomToHFov(dmaxZoom);
        float tanHalfHFov = Mathf.Tan(hFovAtMax * 0.5f * Mathf.Deg2Rad);
        // tanHalfHFov = plateWidth / (2 * dist * targetScreenRatio) 역산
        float dist = CLicensePlateFocusCapture.CKoreanPlateStandardWidth
                   / (2f * tanHalfHFov * CLicensePlateFocusCapture.CDefaultTargetScreenRatio);
        return dist;
    }

    /// <summary>
    /// 카메라↔지면 거리가 [dNear, dMax] 구간에서 균등 분포하도록 뷰포트 v 값을 샘플한다.
    /// 원리: 거리 d를 균등 샘플 → 해당 거리에 대응하는 카메라 수직 화각 위치(v)를 역산.
    ///
    /// 화각 수직 범위 [vFovMin_deg, vFovMax_deg] (카메라 tilt 기준 지면 교점 각도)에서,
    /// 거리 d = camHeight / sin(pitchAngle) 이므로 pitch 각도를 균등 샘플 후 v로 변환.
    /// 지면이 보이지 않거나 계산 불가 시 균등(margin,1-margin) 폴백.
    /// </summary>
    private static float SampleDistanceUniformV(Camera cam, float groundY, float margin, float dMax)
    {
        Vector3 camPos = cam.transform.position;
        float camHeight = camPos.y - groundY;
        if (camHeight <= 0f)
            return UnityEngine.Random.Range(margin, 1f - margin);

        // 뷰포트 상단(v=1-margin)·하단(v=margin) 레이의 지면 거리를 구해 샘플 구간 결정
        float dFar  = GroundDistFromViewportV(cam, 1f - margin, groundY);
        float dNear = GroundDistFromViewportV(cam, margin,      groundY);

        // 유효하지 않은 경우(하늘 방향 레이 등) 폴백
        if (dFar <= 0f || dNear <= 0f || dNear >= dFar)
            return UnityEngine.Random.Range(margin, 1f - margin);

        // dMax로 원거리 상한 제약 (① 거리 상한과 일관)
        float dSampleMax = Mathf.Min(dFar, dMax);
        if (dSampleMax <= dNear)
            return margin;  // 전체 구간이 상한 밖 → 근거리 끝으로 폴백

        // 거리 균등 샘플 → 해당 거리에 대응하는 뷰포트 v 역산
        float dSample = UnityEngine.Random.Range(dNear, dSampleMax);
        return ViewportVFromGroundDist(cam, dSample, groundY, margin);
    }

    /// <summary>뷰포트 v 위치의 레이가 지면(Y=groundY)과 교차하는 거리를 반환한다. 교차 없으면 -1.</summary>
    private static float GroundDistFromViewportV(Camera cam, float v, float groundY)
    {
        Ray r = cam.ViewportPointToRay(new Vector3(0.5f, v, 0f));
        if (r.direction.y >= -1e-4f) return -1f;
        float tVal = (groundY - r.origin.y) / r.direction.y;
        return tVal > 0f ? tVal : -1f;
    }

    /// <summary>
    /// 지면 거리 dSample 에 대응하는 뷰포트 v(상하 위치)를 이분탐색으로 역산한다.
    /// PTZ 카메라(높은 위치에서 하방)는 v 클수록 원거리(화면 상단 = 먼 지면)이므로
    /// 뷰포트 v 와 지면 거리는 단조 증가 관계(v 클수록 원거리).
    /// </summary>
    private static float ViewportVFromGroundDist(Camera cam, float dSample, float groundY, float margin)
    {
        float vLo = margin, vHi = 1f - margin;
        // 최대 16회 이분탐색으로 오차 < 0.001 수렴
        for (int iter = 0; iter < 16; iter++)
        {
            float vMid = (vLo + vHi) * 0.5f;
            float dMid = GroundDistFromViewportV(cam, vMid, groundY);
            if (dMid < 0f) { vHi = vMid; continue; }   // 하늘 방향 → v 낮춰서 지면 쪽으로
            // v 클수록 원거리(dMid 큼), v 작을수록 근거리(dMid 작음)
            if (dMid > dSample) vHi = vMid;  // dMid 크면(원거리) → v 낮춰서 근거리로
            else                vLo = vMid;
        }
        return Mathf.Clamp((vLo + vHi) * 0.5f, margin, 1f - margin);
    }

    private static CObjCar SpawnCar(CCarObjListUI carListUI, Vector3 pos, float rotY, Camera kCamera)
    {
        int        prefabIdx = UnityEngine.Random.Range(0, carListUI.m_PrefabList.Count);
        GameObject goPrefab  = carListUI.m_PrefabList[prefabIdx];
        if (goPrefab == null) return null;

        Quaternion rot = Quaternion.Euler(0f, rotY, 0f);
        GameObject go  = carListUI.m_UseObjectPooling && carListUI.m_ObjectPool != null
            ? carListUI.m_ObjectPool.Spawn(goPrefab, pos, rot, carListUI.transform)
            : UnityEngine.Object.Instantiate(goPrefab, pos, rot, carListUI.transform);

        if (go == null) return null;
        go.transform.localScale = goPrefab.transform.localScale;

        CObjCar car = go.GetComponent<CObjCar>();
        if (car == null) { UnityEngine.Object.Destroy(go); return null; }

        car.Initialize(carListUI.m_ObjCarList.Count, "", 0, kCamera, -1, true);
        carListUI.AddCarObject(car);
        return car;
    }
}
