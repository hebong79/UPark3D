using System.Collections.Generic;
using UnityEngine;

/// <summary>
/// 현재 프리셋의 주차면(CFaceRect) 위치에만 차량을 배치하는 유틸리티.
///
/// - 배치 대수 = 전달된 슬롯 수(= 프리셋 faceCount).
/// - 차량 방향(축): 저장 차량위치정보(carpos)의 rotY 기준. 해당 슬롯 항목이 없으면 슬롯 사각형 지오메트리로 폴백.
/// - 전/후면: 카메라가 바라보는 기준 — 전면(번호판)이 카메라를 향하도록 축 방향(±180)을 결정. m_IsFront=true.
/// - 일반배치(bRandom=false): 슬롯 중심·정렬 고정, 차종은 슬롯 순 결정적 순환.
/// - 랜덤배치(bRandom=true):  차종(prefab)·위치 미세 오프셋·회전 지터 무작위(방향축·전후면 규칙은 동일).
/// </summary>
public static class CPresetSlotPlacer
{
    public readonly struct SSlotRef
    {
        public readonly int presetId;
        public readonly CFaceRect slot;

        public SSlotRef(int presetId, CFaceRect slot)
        {
            this.presetId = presetId;
            this.slot = slot;
        }
    }

    // 랜덤배치 위치 오프셋 범위(기존 SetRandomCarPos_ByParkSlot 와 동일)
    private const float DRAND_OFFSET_X = 0.15f;   // X 방향 좌우 흔들림(m)
    private const float DRAND_OFFSET_Z = 1.0f;    // Z 방향 앞뒤 흔들림(m)
    private const float DRAND_ANGLE    = 5f;      // 회전 지터(도)

    /// <summary>
    /// 슬롯 사각형의 두 변 중 긴 쪽(주차 깊이=차량 길이축)의 월드 yaw를 반환한다.
    /// Unity yaw 규약: 0=+Z, 90=+X. (car forward = (sin,0,cos))
    /// </summary>
    public static float CalcHeadingYFromEdges(Vector3 edgeAlongZ, Vector3 edgeAlongX)
    {
        Vector3 depth = (edgeAlongZ.sqrMagnitude >= edgeAlongX.sqrMagnitude) ? edgeAlongZ : edgeAlongX;
        depth.y = 0f;
        if (depth.sqrMagnitude < 1e-6f) return 0f;
        return Mathf.Atan2(depth.x, depth.z) * Mathf.Rad2Deg;
    }

    /// <summary>
    /// 슬롯 월드 코너에서 차량 heading(긴 변 방향)을 계산한다. carpos rotY가 없을 때의 폴백.
    /// m_Points 로컬 A(0)B(1)C(2)D(3): A→B=로컬Z변(zSize), A→D=로컬X변(xSize).
    /// </summary>
    public static float CalcSlotHeadingY(CFaceRect slot)
    {
        if (slot == null) return 0f;
        List<Vector3> p = slot.m_Points;
        if (p == null || p.Count < 4) return slot.transform.eulerAngles.y;

        Transform t = slot.transform;
        Vector3 a     = t.TransformPoint(p[0]);
        Vector3 edgeZ = t.TransformPoint(p[1]) - a;   // 로컬 Z변
        Vector3 edgeX = t.TransformPoint(p[3]) - a;   // 로컬 X변
        return CalcHeadingYFromEdges(edgeZ, edgeX);
    }

    /// <summary>
    /// axisYaw(차량 전면 가정 방향)과 그 반대(axisYaw+180) 중, 전면(nose)이 카메라를 향하는 yaw를 반환한다.
    /// 차량 전면 방향 = (sin(yaw), 0, cos(yaw)). 카메라 방향과 내적이 양이면 전면이 카메라를 향한다.
    /// </summary>
    public static float OrientFrontTowardCamera(float axisYaw, Vector3 carPos, Vector3 camPos)
    {
        Vector3 toCam = camPos - carPos;
        toCam.y = 0f;
        if (toCam.sqrMagnitude < 1e-6f) return axisYaw;

        float rad = axisYaw * Mathf.Deg2Rad;
        Vector3 fwd = new Vector3(Mathf.Sin(rad), 0f, Mathf.Cos(rad));
        return (Vector3.Dot(fwd, toCam) >= 0f) ? axisYaw : axisYaw + 180f;
    }

    /// <summary>
    /// 저장 차량위치정보(carpos)에서 (presetId, slotId)에 해당하는 rotY를 찾는다.
    /// 항목이 없으면 found=false.
    /// </summary>
    public static float LookupSavedRotY(int presetId, int slotId, out bool found)
    {
        found = false;
        CDataMgr mgr = CDataMgr.Inst;
        var data = mgr != null ? mgr.m_SaveCarPosData : null;
        if (data == null || data.m_Data == null || data.m_Data.datas == null) return 0f;

        var pos = data.m_Data.datas.Find(x => x.presetId == presetId && x.slotId == slotId);
        if (pos == null) return 0f;

        found = true;
        return pos.rotY;
    }

    /// <summary>
    /// slots 각 주차면 위치에 차량을 1대씩 배치한다.
    /// 스폰된 차량은 carListUI.m_ObjCarList 에 등록되고, 배치 리스트로도 반환된다.
    /// </summary>
    /// <param name="carListUI">차량 목록/풀 관리자</param>
    /// <param name="slots">현재 프리셋 주차면 목록(CFaceRect). 각 슬롯의 transform.position/m_idx 사용</param>
    /// <param name="presetId">현재 프리셋 ID(1-based)</param>
    /// <param name="cam">차량 번호판 캔버스 연결 + 전/후면 판정 기준 카메라</param>
    /// <param name="bRandom">true=랜덤배치, false=일반배치</param>
    /// <param name="randomSeed">랜덤배치 시드(0=미지정). 지정 시 동일 시드는 동일 레이아웃을 재현한다(프리셋별 결정적 배치).</param>
    /// <param name="persistToCarPosData">true면 생성 차량의 위치·프리팹·프리셋·슬롯 정보를 저장한다.</param>
    /// <param name="randomColor">true면 차량 초기화 시 랜덤 색상을 적용한다.</param>
    public static List<CObjCar> PlaceVehiclesOnSlots(
        CCarObjListUI carListUI,
        IReadOnlyList<CFaceRect> slots,
        int presetId,
        Camera cam,
        bool bRandom,
        int randomSeed = 0,
        bool persistToCarPosData = false,
        bool randomColor = false)
    {
        var placed = new List<CObjCar>();

        if (carListUI == null || slots == null)
        {
            Debug.LogWarning("[CPresetSlotPlacer] carListUI 또는 slots 가 null → 배치 0대.");
            return placed;
        }
        if (carListUI.m_PrefabList == null || carListUI.m_PrefabList.Count == 0)
        {
            Debug.LogWarning("[CPresetSlotPlacer] 프리팹 리스트가 비어 있습니다 → 배치 0대.");
            return placed;
        }

        // 랜덤배치는 지정된 시드로 결정적 재현(같은 프리셋은 세션 내내 동일 레이아웃).
        if (bRandom && randomSeed != 0)
            Random.InitState(randomSeed);

        int prefabCount = carListUI.m_PrefabList.Count;
        bool hasCam = cam != null;
        Vector3 camPos = hasCam ? cam.transform.position : Vector3.zero;

        for (int i = 0; i < slots.Count; i++)
        {
            CFaceRect slot = slots[i];
            if (slot == null) continue;

            int slotId = slot.m_idx;      // 1-based 슬롯 번호(CLineRect.m_idx)

            // ① 방향(축): 저장 차량위치정보 rotY 기준(없으면 슬롯 지오메트리 폴백)
            float axisYaw = LookupSavedRotY(presetId, slotId, out bool hasSaved);
            if (!hasSaved) axisYaw = CalcSlotHeadingY(slot);

            // ② 전/후면: 전면(번호판)이 카메라를 향하도록 ±180 결정. 카메라 없으면 파일 방향 유지.
            float baseYaw = hasCam
                ? OrientFrontTowardCamera(axisYaw, slot.transform.position, camPos)
                : axisYaw;

            int     prefabIdx;
            float   jitter;
            Vector3 offset;

            if (bRandom)
            {
                prefabIdx = Random.Range(0, prefabCount);
                jitter    = Random.Range(-DRAND_ANGLE, DRAND_ANGLE);
                offset    = new Vector3(
                    Random.Range(-DRAND_OFFSET_X, DRAND_OFFSET_X),
                    0f,
                    Random.Range(-DRAND_OFFSET_Z, DRAND_OFFSET_Z));
            }
            else
            {
                // 일반배치: 슬롯 중심·정렬 고정, 차종은 슬롯 순 결정적 순환
                prefabIdx = i % prefabCount;
                jitter    = 0f;
                offset    = Vector3.zero;
            }

            GameObject prefab = carListUI.m_PrefabList[prefabIdx];
            if (prefab == null) continue;

            float      rotY = baseYaw + jitter;
            Vector3    pos  = slot.transform.position + offset;
            Quaternion rot  = Quaternion.Euler(0f, rotY, 0f);

            GameObject go = carListUI.m_UseObjectPooling && carListUI.m_ObjectPool != null
                ? carListUI.m_ObjectPool.Spawn(prefab, pos, rot, carListUI.transform)
                : Object.Instantiate(prefab, pos, rot, carListUI.transform);

            if (go == null) continue;
            go.transform.localScale = prefab.transform.localScale;

            CObjCar car = go.GetComponent<CObjCar>();
            if (car == null)
            {
                Object.Destroy(go);
                continue;
            }

            SObjectPos savedPos = null;
            if (persistToCarPosData)
            {
                savedPos = CDataMgr.Inst.AddCarPosData(
                    pos,
                    rotY,
                    prefabIdx + 1,
                    carListUI.m_ObjCarList.Count);
                savedPos.presetId = presetId;
                savedPos.slotId = slotId;
                savedPos.isFront = true;
            }

            car.Initialize(
                carListUI.m_ObjCarList.Count,
                savedPos != null ? savedPos.id : "",
                presetId,
                cam,
                slotId,
                randomColor);
            car.m_iPresetId = presetId;
            car.m_iFaceSlot = slotId;
            car.m_IsFront   = true;   // 전면이 카메라를 향하도록 배치 → 전면 노출(Initialize가 설정 안 하므로 명시)

            carListUI.AddCarObject(car);
            carListUI.m_CurCarIDList.Add(prefabIdx + 1);
            placed.Add(car);
        }

        Debug.Log($"[CPresetSlotPlacer] 프리셋 {presetId} 슬롯 {slots.Count}개 / 배치 {placed.Count}대 (랜덤={bRandom})");
        return placed;
    }
}
