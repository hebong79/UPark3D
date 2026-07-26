using System.Collections.Generic;
using UnityEngine;

/// <summary>
/// 맵 사각박스(min/max) 안에 PTZ 카메라를 랜덤 배치하고 "중심을 응시"하도록
/// Pan/Tilt 를 산출하는 static 유틸리티.
///
/// 응시 규칙 (Docs/20260523_141843_카메라랜덤배치_VLA_설계서.md §4):
///   - 일반: 카메라 → 중심점 방향 ray 가 박스 경계와 만나는 "반대편(far) 교점"을 타겟으로.
///           (중심을 지나 반대편 라인을 보므로 시선이 화면을 가로질러 정면 영역을 담는다.)
///   - 근접(|center - camXZ| < nearCenterRadius): 방향이 모호 → ±z 중 랜덤,
///           "확대박스(nearCenterBoxScale)"의 z 라인 점을 타겟. (= y만 들어 ±z 큰 박스 라인)
///
/// 차량 배치/번호판 가시성/줌 PTZ 계산은 기존 검증 자산을 재사용한다:
///   - CPtzSpaceVehiclePlacer (차량 배치 / 뷰포트·가림 판정)
///   - CLicensePlateFocusCapture.CalcPanTilt (방향→Pan/Tilt 변환)
/// </summary>
public static class CPtzRandomCameraPlacer
{
    /// <summary>맵 박스 안의 랜덤 XZ 위치(높이 y 지정)를 반환한다.</summary>
    public static Vector3 GetRandomXZInBox(Vector3 boxMin, Vector3 boxMax, float y)
    {
        // P2: 호출마다 재시드하지 않음 — 시드는 생성 세션 시작 시 1회 고정 (CVlaPSImgMakerGameUI)
        float x = Random.Range(Mathf.Min(boxMin.x, boxMax.x), Mathf.Max(boxMin.x, boxMax.x));
        float z = Random.Range(Mathf.Min(boxMin.z, boxMax.z), Mathf.Max(boxMin.z, boxMax.z));
        return new Vector3(x, y, z);
    }

    /// <summary>
    /// 카메라가 응시할 월드 타겟점(y = groundY)을 계산한다.
    /// 일반/근접 케이스를 분기한다(클래스 주석 참조).
    /// </summary>
    public static Vector3 CalcLookTarget(
        Vector3 camPos, Vector3 boxMin, Vector3 boxMax,
        float groundY = 0f, float nearCenterRadius = 3f, float nearCenterBoxScale = 2f)
    {
        // XZ 평면(Vector2.x = world x, Vector2.y = world z)
        Vector2 cam    = new Vector2(camPos.x, camPos.z);
        Vector2 min    = new Vector2(Mathf.Min(boxMin.x, boxMax.x), Mathf.Min(boxMin.z, boxMax.z));
        Vector2 max    = new Vector2(Mathf.Max(boxMin.x, boxMax.x), Mathf.Max(boxMin.z, boxMax.z));
        Vector2 center = (min + max) * 0.5f;
        Vector2 toC    = center - cam;

        // 근접: 방향 모호 → ±z 확대박스 라인의 한 점
        if (toC.magnitude < nearCenterRadius)
        {
            float sign      = (Random.value < 0.5f) ? 1f : -1f;
            float halfDepth = (Mathf.Abs(max.y - min.y) * 0.5f) * nearCenterBoxScale; // y == world z
            return new Vector3(center.x, groundY, center.y + sign * halfDepth);
        }

        // 일반: cam→center ray 가 박스 경계와 만나는 far 교점
        Vector2 hit = RayAabbFarExit(cam, toC.normalized, min, max);
        return new Vector3(hit.x, groundY, hit.y);
    }

    /// <summary>
    /// 카메라가 응시할 월드 타겟점(y = groundY)을 산출한다. (요구사항 1)
    ///   - 일반(중심까지 수평거리 ≥ nearCenterRadius): 맵 박스 "중심" 지면점을 응시한다.
    ///       → 화면 중심에 주차장 중심이 오고, 충분한 하향 tilt 가 확보된다.
    ///   - 근접(중심까지 수평거리 &lt; nearCenterRadius): 중심을 응시하면 거의 수직 하방이 되어
    ///       화면에 바닥 일부만 잡힌다. 대신 중심 방향(모호하면 +Z)으로 "맵 바닥 끝(far 모서리)"을
    ///       응시하여, 카메라 forward(=화면 중심선)가 맵 끝을 향하도록 tilt 를 낮춘다.
    ///       → 맵 바닥 끝이 화면 중심선에 위치한다.
    /// </summary>
    public static Vector3 CalcAimTarget(
        Vector3 camPos, Vector3 boxMin, Vector3 boxMax,
        float groundY = 0f, float nearCenterRadius = 3f)
    {
        Vector2 min    = new Vector2(Mathf.Min(boxMin.x, boxMax.x), Mathf.Min(boxMin.z, boxMax.z));
        Vector2 max    = new Vector2(Mathf.Max(boxMin.x, boxMax.x), Mathf.Max(boxMin.z, boxMax.z));
        Vector2 center = (min + max) * 0.5f;
        Vector2 cam    = new Vector2(camPos.x, camPos.z);
        Vector2 toC    = center - cam;
        float   dist   = toC.magnitude;

        // 일반: 중심 응시
        if (dist >= nearCenterRadius)
            return new Vector3(center.x, groundY, center.y);

        // 근접: 중심 방향(모호하면 +Z)으로 맵 바닥 끝(far 모서리) 응시 → 맵 끝이 화면 중심선
        Vector2 dir = (dist > 1e-4f) ? (toC / dist) : new Vector2(0f, 1f);
        Vector2 hit = RayAabbFarExit(cam, dir, min, max);
        return new Vector3(hit.x, groundY, hit.y);
    }

    /// <summary>
    /// AABB 내부에서 출발한 ray 가 박스를 빠져나가는 첫 경계점(far 교점)을 반환한다(슬랩법).
    /// 각 축의 far 평면 도달 t 중 최솟값(가장 먼저 닿는 경계)을 사용한다.
    /// 축평행(dir 성분 0) 축은 무한대 t 로 처리하여 다른 축이 결정한다.
    /// </summary>
    public static Vector2 RayAabbFarExit(Vector2 origin, Vector2 dir, Vector2 min, Vector2 max)
    {
        const float kEps = 1e-6f;
        float tx = dir.x >  kEps ? (max.x - origin.x) / dir.x
                 : dir.x < -kEps ? (min.x - origin.x) / dir.x
                 : float.PositiveInfinity;
        float tz = dir.y >  kEps ? (max.y - origin.y) / dir.y
                 : dir.y < -kEps ? (min.y - origin.y) / dir.y
                 : float.PositiveInfinity;

        float t = Mathf.Min(tx, tz);
        if (float.IsInfinity(t) || float.IsNaN(t)) t = 0f;  // dir==0 등 비정상 입력 방어
        t = Mathf.Max(0f, t);
        return origin + dir * t;
    }

    /// <summary>
    /// 카메라를 랜덤 XZ + 지정 높이에 배치하고, 중심 응시 방향의 base Pan/Tilt 를 산출·적용한다.
    ///
    /// [중요] CObjCamera 는 "폴대(pole)의 자식" 구조다(CPCamControlDlg 슬라이더 콜백과 동일 규약):
    ///   - 높이(Y): 월드 높이여야 하므로 transform.position.y 로 설정.
    ///   - X/Z   : 폴대 기준 로컬이므로 transform.localPosition.x/z 로 설정 후 UpdatePolePosition().
    /// 이전엔 X/Z/Y 를 모두 월드 position 으로 한 번에 설정해 폴대 규약과 어긋났고,
    /// 응시 기준점도 줌 돌리(dolly)로 forward 방향만큼 밀린 m_PCamera.position 을 써서
    /// 위치/조준이 의도대로 적용되지 않았다(차량 0대 + 하늘 응시의 근본원인).
    /// </summary>
    //public static void PlaceCameraRandom(
    //    CObjCamera cam, Vector3 randomXZ, float height,
    //    Vector3 boxMin, Vector3 boxMax, Vector3 target,
    //    out float basePan, out float baseTilt,
    //    float groundY = 0f, float nearCenterRadius = 3f, float nearCenterBoxScale = 2f,
    //    float minDownTilt = 0f)
    //{
    //    basePan = baseTilt = 0f;
    //    if (cam == null || cam.m_PCamera == null) return;

    //    // 1) 위치 — 폴대 자식 규약: X/Z 는 로컬, Y(높이)는 월드 (CPCamControlDlg.Initiaize 참조)
    //    Vector3 lp = cam.transform.localPosition;
    //    lp.x = randomXZ.x;
    //    lp.z = randomXZ.z;
    //    cam.transform.localPosition = lp;

    //    Vector3 wp = cam.transform.position;   // 마지막에 월드 Y 확정
    //    wp.y = height;
    //    cam.transform.position = wp;
    //    cam.UpdatePolePosition();

    //    // 2) 응시 타겟 산출 (요구사항 1, CalcAimTarget 참조):
    //    //    - 일반: 맵 "중심" 지면점 → 화면 중심에 주차장 중심, 충분한 하향 tilt 확보.
    //    //    - 근접(중심 근처): 맵 바닥 끝(far 모서리)을 응시 → 맵 끝이 화면 중심선에 오도록 tilt 조정.
    //    //    조준 기준점(eye)은 줌 돌리에 밀리지 않는 "리그 위치"(cam.transform.position) 사용.
    //    //    (nearCenterBoxScale 은 더 이상 사용하지 않음 — 레거시 CalcLookTarget 전용)
    //    Transform parent = cam.m_PCamera.transform.parent;   // = cam.transform (CObjCamera)
    //    Vector3   eye    = cam.m_PCamera.transform.position;

    //    //Vector3 target   = CalcAimTarget(eye, boxMin, boxMax, groundY, nearCenterRadius);

    //    Vector3 worldDir = target - eye;
    //    if (worldDir.sqrMagnitude < 1e-8f) return;  // 카메라와 타겟 동일 위치(이론상 없음)
    //    worldDir.Normalize();

    //    Vector3 localDir = parent != null ? parent.InverseTransformDirection(worldDir) : worldDir;

    //    CLicensePlateFocusCapture.CalcPanTilt(localDir, out basePan, out baseTilt);

    //    // 2-1) 하늘 제거: 중심 응시 tilt 가 얕으면(낮은 높이/먼 거리) 최소 하향각을 강제한다.
    //    //      사용자 선택: '정확한 중심 응시'보다 '하늘 제거'를 우선(요구사항 1 보완).
    //    //      → 낮은 카메라는 중심보다 약간 앞 지면을 보게 되지만 화면이 주차장으로 채워진다.
    //    if (minDownTilt > 0f && baseTilt < minDownTilt) baseTilt = minDownTilt;

    //    cam.pan  = basePan;
    //    cam.tilt = baseTilt;

    //    // 3) 진단: 실제 적용 결과(렌더 카메라 위치/forward.y/각도)를 한 줄로 보고.
    //    //    forward.y 가 음수여야 지면을 내려다보는 것(하향 레이 생성 가능).
    //    Camera rc = cam.getCamera;
    //    if (rc != null)
    //    {
    //        Vector3 f = rc.transform.forward;
    //        Debug.Log(
    //            $"[PlaceCameraRandom] rig={eye} 높이={height:F2} | 렌더캠 pos={rc.transform.position} " +
    //            $"forward.y={f.y:F3} (음수=하향) | basePan={basePan:F1} baseTilt={baseTilt:F1} target={target}");
    //    }
    //}

    /// <summary>
    /// 배치된 모든 차량이 카메라 화면에 들어와 있는지(뷰포트 내 + 가림 없음) 검사한다.
    /// 하나라도 화면을 벗어나거나 가려지면 false.
    /// </summary>
    public static bool IsAllVehiclesVisible(Camera camera, List<CObjCar> vehicles, bool checkOcclusion = true)
    {
        if (camera == null || vehicles == null) return false;

        for (int i = 0; i < vehicles.Count; i++)
        {
            CObjCar v = vehicles[i];
            if (v == null) continue;

            GameObject plate = v.m_FrontPlateNo;
            Vector3 p = plate != null ? plate.transform.position : v.transform.position;

            if (!CPtzSpaceVehiclePlacer.IsInCameraView(camera, p)) return false;
            if (checkOcclusion && !CPtzSpaceVehiclePlacer.IsPlateVisible(camera, v, null)) return false;
        }
        return true;
    }
}
