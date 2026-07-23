using System;
using System.Collections.Generic;
using UnityEngine;
using static CSaveCarPosData;
using static CSaveObjectPosData;
/*
 * 차량 인스턴스 관리
 */
public class CCarObjListUI : MonoBehaviour
{
    public CScoCarData m_ScoCarData = null;

    public List<GameObject> m_PrefabList = new List<GameObject>();   // 프리팹 리스트
    public List<CObjCar> m_ObjCarList = new List<CObjCar>();

    [HideInInspector] public GameObject m_CurCarPrefab = null;       // 선택된 프리팹
    [HideInInspector] public GameObject m_SelectdCarObject = null;
    
    // ✅ 추가: 오브젝트 풀링 시스템
    [Header("오브젝트 풀링")]
    [HideInInspector] public CCarObjectPool m_ObjectPool = null;
    public bool m_UseObjectPooling = true;  // 풀링 사용 여부
    
    // Start is called once before the first execution of Update after the MonoBehaviour is created
    void Start()
    {
        
    }

    public void Initialize(Camera kCamera)
    {
        if( kCamera == null)
        {
            kCamera = Camera.main;
        }
        
        // 직접 변수에서 사용됨
        //Init_LoadCarPrefabList();
        
        // ✅ 오브젝트 풀 초기화
        InitializeObjectPool();
        
        Reset_CreateCarObjectList(kCamera, true);  // 차량오브젝트를 랜덤으로 생성한다.    
    }

    // ✅ 추가: 오브젝트 풀 초기화
    private void InitializeObjectPool()
    {
        if (m_UseObjectPooling)
        {
            GameObject poolObj = new GameObject("CarObjectPool");
            poolObj.transform.SetParent(transform);
            if( m_ObjectPool == null)
                m_ObjectPool = poolObj.AddComponent<CCarObjectPool>();
            
            // 프리팹 정보를 풀에 등록 (리플렉션 사용)
            var poolInfosField = typeof(CCarObjectPool).GetField("m_PoolInfos", 
                System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
            
            if (poolInfosField != null)
            {
                var poolInfos = new List<CCarObjectPool.PoolInfo>();
                foreach (var prefab in m_PrefabList)
                {
                    poolInfos.Add(new CCarObjectPool.PoolInfo
                    {
                        prefab = prefab,
                        initialSize = 10,
                        maxSize = 100,
                        allowGrowth = true
                    });
                }
                poolInfosField.SetValue(m_ObjectPool, poolInfos);
            }
            
            Debug.Log("CCarObjListUI: 오브젝트 풀 초기화 완료");
        }
    }

    // 리소스에서 프리팹을 로딩한다.
    public void Init_LoadCarPrefabList()
    {
        m_PrefabList.Clear();

        string sBasePath = CDataMgr.DCAR_PREFAB_PATH;
        for (int i = 0; i < m_ScoCarData.m_Datas.Count; i++)
        {
            var kData = m_ScoCarData.m_Datas[i];
            string sPathName = string.Format("{0}/{1}", sBasePath, kData.m_PrefabName);
            GameObject goPrefab = Resources.Load<GameObject>(sPathName);
            if (goPrefab != null)
            {
                m_PrefabList.Add(goPrefab);
            }
        }
    }

    public GameObject GetCarPrefab(int idx)
    {
        if( idx >= 0 && idx < m_PrefabList.Count ) 
            return m_PrefabList[idx];

        return null;
    }

    // idx는 0부터 시작
    public void SetCurPrefab(int idx)
    {
        m_CurCarPrefab = GetCarPrefab(idx);
    }
    public void SetCurPfrefab(GameObject goPrefab)
    {
        m_CurCarPrefab = goPrefab;
    }
    public void AddCarObject(CObjCar kCar)
    {
        m_ObjCarList.Add(kCar); 
    }

    public CObjCar CreateCarObject(Vector3 vPos, SObjectPos kCarPos, Camera kCamera)
    {
        if (m_CurCarPrefab != null)
        {
            GameObject go = Instantiate(m_CurCarPrefab, this.transform);
            go.transform.localScale = m_CurCarPrefab.transform.localScale;
            go.transform.position = vPos;
            go.transform.localRotation = Quaternion.Euler(new Vector3(0, kCarPos.rotY, 0));
            CObjCar kObjCar = go.GetComponent<CObjCar>();
            kObjCar.Initialize(m_ObjCarList.Count, kCarPos.id, kCarPos.presetId, kCamera, kCarPos.slotId);
            
            this.AddCarObject(kObjCar);

            m_SelectdCarObject = kObjCar.gameObject;
            return kObjCar;
        }
        return null;
    }

    // ✅ 수정: 오브젝트 풀 사용
    public CObjCar CreateCarObjectByCarPos(Vector3 vPos, SObjectPos kCarPos, Camera kCamera)
    {
        GameObject goPrefab = this.GetCarPrefab(kCarPos.prefabId-1);  // prefabId는 1부터 시작함.
        Debug.Assert(goPrefab != null, $"Prefab Id = {kCarPos.prefabId}");
        if (goPrefab != null)
        {
            GameObject go;
            
            // ✅ 풀링 사용 여부에 따라 생성 방식 결정
            if (m_UseObjectPooling && m_ObjectPool != null)
            {
                go = m_ObjectPool.Spawn(goPrefab, vPos, Quaternion.Euler(new Vector3(0, kCarPos.rotY, 0)), this.transform);
            }
            else
            {
                go = Instantiate(goPrefab, this.transform);
                go.transform.position = vPos;
                go.transform.localRotation = Quaternion.Euler(new Vector3(0, kCarPos.rotY, 0));
            }
            
            if (go == null)
            {
                CLogger.Inst.Error($"차량 생성 실패: prefab={goPrefab.name}", "CarList");
                return null;
            }
            
            go.transform.localScale = goPrefab.transform.localScale;
            
            CObjCar kObjCar = go.GetComponent<CObjCar>();
            Debug.Assert(kObjCar != null, "CObjCar is null");
            
            // ✅ 수정: idx를 미리 계산 (동시성 문제 방지)
            int carIndex = m_ObjCarList.Count;
            
            // ✅ 명시적 idx 전달
            kObjCar.Initialize(carIndex, kCarPos.id, kCarPos.presetId, kCamera, kCarPos.slotId);

            // ✅ AddCarObject는 Initialize 후에
            this.AddCarObject(kObjCar);
            
            // ✅ 검증: 리스트 크기 확인
            if (m_ObjCarList.Count != carIndex + 1)
            {
                CLogger.Inst.Error($"차량 추가 불일치! 예상: {carIndex + 1}, 실제: {m_ObjCarList.Count}", "CarList");
            }

            m_SelectdCarObject = kObjCar.gameObject;
            return kObjCar;
        }
        return null;
    }

    // 매번생성하므로 pooling을 하여 재사용하는것이 좋다.
    public CObjCar CreateRandomCarObjectByCarPos(Vector3 vPos, SObjectPos kCarPos, Camera kCamera, ref int carIdx)
    {
        //여기서 프리팹은 랜덤으로 선택한다...
        //---------------------------------------------------------------------
        var kSaveData = CDataMgr.Inst.m_SaveCarPosData.m_Data;
        int seed = (int)(DateTime.Now.Ticks ^ Environment.TickCount);
        UnityEngine.Random.InitState(seed);

        //랜덤으로 선택된 carpos의 인덱스를 가져온다.
        int idx = UnityEngine.Random.Range(0, m_PrefabList.Count); //( 0 ~ Count - 1 중의 값 )
        carIdx = idx + 1;   // prefabId는 1부터 시작

        GameObject goPrefab = this.GetCarPrefab(idx);
        Debug.Assert(goPrefab != null, $"Prefab Id = {idx+1}");
        Debug.Log($"Random Car Prefab Idx = {idx}, Name = {goPrefab.name}");
        if (goPrefab != null)
        {
            GameObject go;
            
            // ✅ 풀링 사용 여부에 따라 생성 방식 결정
            if (m_UseObjectPooling && m_ObjectPool != null)
            {
                go = m_ObjectPool.Spawn(goPrefab, vPos, Quaternion.Euler(new Vector3(0, kCarPos.rotY, 0)), this.transform);
            }
            else
            {
                go = Instantiate(goPrefab, this.transform);
                go.transform.position = vPos;
                go.transform.localRotation = Quaternion.Euler(new Vector3(0, kCarPos.rotY, 0));
            }
            
            if (go == null)
            {
                CLogger.Inst.Error($"차량 생성 실패: prefab={goPrefab.name}", "CarList");
                return null;
            }
            
            go.transform.localScale = goPrefab.transform.localScale;
            
            CObjCar kObjCar = go.GetComponent<CObjCar>();
            Debug.Assert(kObjCar != null, "CObjCar is null");
            
            // ✅ 수정: idx를 미리 계산 (동시성 문제 방지)
            int carIndex = m_ObjCarList.Count;
            
            // ✅ 명시적 idx 전달
            kObjCar.Initialize(carIndex, kCarPos.id, kCarPos.presetId,kCamera, kCarPos.slotId);
            
            // ✅ AddCarObject는 Initialize 후에
            this.AddCarObject(kObjCar);
            
            // ✅ 검증: 리스트 크기 확인
            if (m_ObjCarList.Count != carIndex + 1)
            {
                CLogger.Inst.Error($"차량 추가 불일치! 예상: {carIndex + 1}, 실제: {m_ObjCarList.Count}", "CarList");
            }

            m_SelectdCarObject = kObjCar.gameObject;
            return kObjCar;
        }
        return null;
    }

    public List<int> m_CurCarIDList = new List<int>();
    
    /// <summary>
    /// 2.5m 간격으로 랜덤 차량을 옆으로 생성
    /// </summary>
    /// <param name="carCount">생성할 차량 개수</param>
    /// <param name="offsetPos">시작 위치 오프셋</param>
    /// <param name="kCamera">카메라</param>
    /// <param name="spacing">차량 간 간격 (기본 2.5m)</param>
    /// <returns>생성된 차량 리스트</returns>
    public List<GameObject> CreateRandomCarsInLine(int presetId, int carCount, Vector3 offsetPos, Camera kCamera, float spacing = 2.5f, bool bVertical = false, Vector3 rightDir = default(Vector3), float refRotY = 180f)
    {
        if (rightDir == Vector3.zero) rightDir = Vector3.right;
        CLogger.Inst.Debug($"========== CreateRandomCarsInLine 시작: {carCount}개 차량 생성, 간격={spacing}, 세로={bVertical}, rightDir={rightDir} ==========", "CarList");
        
        List<GameObject> createdCars = new List<GameObject>();
        
        if (m_PrefabList == null || m_PrefabList.Count == 0)
        {
            CLogger.Inst.Error("프리팹 리스트가 비어있습니다!", "CarList");
            return createdCars;
        }
        
        if (kCamera == null)
        {
            kCamera = Camera.main;
        }
        
        // 랜덤 시드 초기화
        int seed = (int)(DateTime.Now.Ticks ^ Environment.TickCount);
        UnityEngine.Random.InitState(seed);
        
        for (int i = 1; i <= carCount; i++)
        {
            // 랜덤 프리팹 선택 (0 ~ Count-1)
            int randomIdx = UnityEngine.Random.Range(0, m_PrefabList.Count);
            GameObject goPrefab = GetCarPrefab(randomIdx);
            
            if (goPrefab == null)
            {
                CLogger.Inst.Error($"프리팹을 찾을 수 없습니다: idx={randomIdx}", "CarList");
                continue;
            }
            
            // 세로배치: Z축 고정, 가로배치: 기준차량의 right 벡터 방향
            Vector3 spawnPos = bVertical
                ? offsetPos + new Vector3(0, 0, i * spacing)
                : offsetPos + rightDir * (i * spacing);

            // 기준차량과 동일한 회전각 적용
            float randomRotY = refRotY;
            
            GameObject carObj;
            
            // ✅ 오브젝트 풀 사용
            if (m_UseObjectPooling && m_ObjectPool != null)
            {
                carObj = m_ObjectPool.Spawn(goPrefab, spawnPos, Quaternion.Euler(0, randomRotY, 0), this.transform);
            }
            else
            {
                carObj = Instantiate(goPrefab, this.transform);
                carObj.transform.position = spawnPos;
                carObj.transform.localRotation = Quaternion.Euler(0, randomRotY, 0);
            }
            
            if (carObj == null)
            {
                CLogger.Inst.Error($"차량 생성 실패: prefab={goPrefab.name}", "CarList");
                continue;
            }
            
            carObj.transform.localScale = goPrefab.transform.localScale;
            
            
            // CObjCar 초기화
            CObjCar kObjCar = carObj.GetComponent<CObjCar>();
            if (kObjCar != null)
            {
                int carIndex = m_ObjCarList.Count;
                // prefabId는 1부터 시작하는 규약(로드 시 GetCarPrefab(prefabId-1) 사용)이므로
                // 0-based randomIdx 를 그대로 저장하면 prefabId=0(또는 off-by-one) 으로 잘못 저장된다. → +1
                SObjectPos kCarPos = CDataMgr.Inst.AddCarPosData(spawnPos, randomRotY, randomIdx + 1, carIndex);

                //string carId = $"RandomCar_{DateTime.Now.Ticks}_{carIndex + i}";
                
                kObjCar.Initialize(carIndex, kCarPos.id, presetId, kCamera, i);
                AddCarObject(kObjCar);


                // prefabId 저장 (1부터 시작)
                m_CurCarIDList.Add(randomIdx + 1);
                
                CLogger.Inst.Debug($"차량 생성: idx={carIndex}, prefab={goPrefab.name}, pos={spawnPos}, rot={randomRotY}", "CarList");
            }
            else
            {
                CLogger.Inst.Error($"CObjCar 컴포넌트를 찾을 수 없습니다: {carObj.name}", "CarList");
            }
            
            createdCars.Add(carObj);
        }
        
        CLogger.Inst.Debug($"========== CreateRandomCarsInLine 완료: {createdCars.Count}/{carCount}개 생성 ==========", "CarList");
        
        return createdCars;
    }
    
    /// <summary>
    /// 차량의 슬롯번호를 리스트 순으로 재 셋팅
    /// </summary>
    public void ResetSlotNumberForCars()
    {
        for ( int i = 0; i < m_ObjCarList.Count; i++)
        {
            CObjCar car = m_ObjCarList[i];
            car.m_iFaceSlot = i + 1;
        }
    }
    /// <summary>
    /// 차량리스트 모두 숨기기
    /// </summary>
    public void HideAll_Cars()
    {
        foreach (var car in m_ObjCarList)
        {
            if (car != null && car.gameObject != null)
            {
                car.gameObject.SetActive(false);
            }
        }
    }


    /// <summary>
    /// 랜덤으로 차량을 선택해서 숨기기
    /// </summary>
    /// <param name="hideCount">숨길 차량 개수 (0이면 랜덤 개수)</param>
    /// <returns>숨겨진 차량 리스트</returns>
    public List<CObjCar> HideRandomCars(List<CObjCar> kObjCarList, int hideCount = 0)
    {
        CLogger.Inst.Debug($"========== HideRandomCars 시작 ==========", "CarList");
        
        List<CObjCar> hiddenCars = new List<CObjCar>();
        
        if (kObjCarList == null || kObjCarList.Count == 0)
        {
            CLogger.Inst.Warning("숨길 차량이 없습니다 (리스트가 비어있음)", "CarList");
            return hiddenCars;
        }

        //// 슬롯번호 재할당
        //ResetSlotNumberForCars();

        // hideCount가 0이면 랜덤 개수 결정 (1 ~ 전체 차량의 90%)
        if (hideCount <= 0)
        {
            int maxHideCount = Mathf.Max(1, (int)(kObjCarList.Count * 0.9f));
            hideCount = UnityEngine.Random.Range(0, maxHideCount);
        }
        
        // 숨길 개수가 전체 차량 수보다 많으면 조정
        hideCount = Mathf.Min(hideCount, kObjCarList.Count-1);
        
        CLogger.Inst.Debug($"숨길 차량 개수: {hideCount} / {kObjCarList.Count}", "CarList");
        
        // 랜덤 시드 초기화
        int seed = (int)(DateTime.Now.Ticks ^ Environment.TickCount);
        UnityEngine.Random.InitState(seed);
        
        // 활성화된 차량 리스트 (null이 아니고 활성화된 것만)
        List<CObjCar> activeCars = new List<CObjCar>();
        foreach (var car in kObjCarList)
        {
            if (car != null && car.gameObject != null && car.gameObject.activeSelf)
            {
                activeCars.Add(car);
            }
        }
        
        if (activeCars.Count == 0)
        {
            CLogger.Inst.Warning("숨길 수 있는 활성 차량이 없습니다", "CarList");
            return hiddenCars;
        }
        
        // 실제 숨길 개수 재조정
        hideCount = Mathf.Min(hideCount, activeCars.Count-1);  //1개는 무조건 출력
        
        // 랜덤으로 차량 선택 (중복 방지)
        HashSet<int> selectedIndices = new HashSet<int>();
        
        while (selectedIndices.Count < hideCount)
        {
            int randomIdx = UnityEngine.Random.Range(0, activeCars.Count);
            selectedIndices.Add(randomIdx);
        }
        
        // 선택된 차량 숨기기
        foreach (int idx in selectedIndices)
        {
            CObjCar car = activeCars[idx];
            
            if (car != null && car.gameObject != null)
            {
                car.gameObject.SetActive(false);
                hiddenCars.Add(car);
                
                CLogger.Inst.Debug($"차량 숨김: {car.m_NameId} (idx={car.m_Idx}, 위치={car.transform.position})", "CarList");
            }
        }
        
        CLogger.Inst.Debug($"========== HideRandomCars 완료: {hiddenCars.Count}개 숨김 ==========", "CarList");
        
        return hiddenCars;
    }

    /// <summary>
    /// 노이즈 차량 랜덤 숨기기
    /// ─ 동작 원칙 ──────────────────────────────────────────────────
    ///   1) 전달된 리스트의 모든 차량을 먼저 전부 숨김
    ///   2) GetNoiseShowCount() 로 표시할 차량 수(0~2) 확률 결정
    ///      - 0대 표시: 50%
    ///      - 1대 표시: 45%
    ///      - 2대 표시:  5%
    ///   3) Fisher-Yates 셔플 후 showCount 만큼만 다시 활성화
    ///   4) 숨겨진 차량 리스트 반환
    /// ───────────────────────────────────────────────────────────────
    /// </summary>
    /// <param name="kObjCarList">노이즈 차량 리스트 (전체)</param>
    /// <returns>최종적으로 숨겨진 차량 리스트</returns>
    public List<CObjCar> HideRandomNoiseCars(List<CObjCar> kObjCarList)
    {
        CLogger.Inst.Debug($"========== HideRandomNoiseCars 시작 ==========", "CarList");

        List<CObjCar> hiddenCars = new List<CObjCar>();

        if (kObjCarList == null || kObjCarList.Count == 0)
        {
            CLogger.Inst.Warning("숨길 차량이 없습니다 (리스트가 비어있음)", "CarList");
            return hiddenCars;
        }

        // ── Step 1: 유효한 차량만 필터링 ────────────────────────────
        List<CObjCar> validCars = new List<CObjCar>();
        foreach (var car in kObjCarList)
        {
            if (car != null && car.gameObject != null)
                validCars.Add(car);
        }

        if (validCars.Count == 0)
        {
            CLogger.Inst.Warning("유효한 차량이 없습니다", "CarList");
            return hiddenCars;
        }

        // ── Step 2: 모든 차량을 먼저 숨김 ─────────────────────────
        // (리스트 외부에서 보이는 차량이 남는 버그 방지)
        foreach (var car in validCars)
            car.gameObject.SetActive(false);

        // ── Step 3: 표시할 차량 수 확률 결정 ───────────────────────
        // 0대: 50% / 1대: 45% / 2대: 5%
        int showCount = GetNoiseShowCount();
        showCount = Mathf.Min(showCount, validCars.Count);

        CLogger.Inst.Debug(
            $"노이즈 표시 차량: {showCount}대 / 전체: {validCars.Count}대 / 숨김: {validCars.Count - showCount}대",
            "CarList"
        );

        // ── Step 4: showCount > 0 이면 Fisher-Yates 셔플 후 복원 ──
        if (showCount > 0)
        {
            int seed = (int)(DateTime.Now.Ticks ^ Environment.TickCount);
            UnityEngine.Random.InitState(seed);

            // 인덱스 배열 셔플 (원본 리스트 변경 없음)
            List<int> indices = new List<int>(validCars.Count);
            for (int i = 0; i < validCars.Count; i++) indices.Add(i);

            for (int i = indices.Count - 1; i > 0; i--)
            {
                int j = UnityEngine.Random.Range(0, i + 1);
                int tmp = indices[i]; indices[i] = indices[j]; indices[j] = tmp;
            }

            // 앞 showCount 개만 다시 활성화
            for (int i = 0; i < showCount; i++)
                validCars[indices[i]].gameObject.SetActive(true);
        }

        // ── Step 5: 최종 숨겨진 차량 수집 ──────────────────────────
        foreach (var car in validCars)
        {
            if (!car.gameObject.activeSelf)
            {
                hiddenCars.Add(car);
                CLogger.Inst.Debug(
                    $"차량 숨김: {car.m_NameId} (idx={car.m_Idx})",
                    "CarList"
                );
            }
        }

        CLogger.Inst.Debug(
            $"========== HideRandomNoiseCars 완료: 표시={showCount}대 / 숨김={hiddenCars.Count}대 ==========",
            "CarList"
        );

        return hiddenCars;
    }

    /// <summary>
    /// 노이즈 차량 표시 개수를 확률에 따라 결정
    /// - 0대: 50% / 1대: 45% / 2대: 5%
    /// </summary>
    /// <returns>표시할 차량 수 (0~2)</returns>
    public int GetNoiseShowCount()
    {
        // [0  ~ 49]  → 50% → 0대 표시 (전체 숨김)
        // [50 ~ 94]  → 45% → 1대 표시
        // [95 ~ 99]  →  5% → 2대 표시
        int roll = UnityEngine.Random.Range(0, 100);

        if (roll < 50) return 0;
        if (roll < 95) return 1;
        return 2;
    }
        #region 숨겨진 차량 다시 표시 기능 (주석 처리됨)
        ///// <summary>
        ///// 숨겨진 모든 차량 다시 표시
        ///// </summary>
        ///// <returns>다시 표시된 차량 개수</returns>
        //public int ShowAllCars()
        //{
        //    CLogger.Inst.Debug("========== ShowAllCars 시작 ==========", "CarList");

        //    int shownCount = 0;

        //    foreach (var car in m_ObjCarList)
        //    {
        //        if (car != null && car.gameObject != null && !car.gameObject.activeSelf)
        //        {
        //            car.gameObject.SetActive(true);
        //            shownCount++;

        //            CLogger.Inst.Debug($"차량 표시: {car.m_NameId} (idx={car.m_Idx})", "CarList");
        //        }
        //    }

        //    CLogger.Inst.Debug($"========== ShowAllCars 완료: {shownCount}개 표시 ==========", "CarList");

        //    return shownCount;
        //}

        ///// <summary>
        ///// 특정 차량들을 다시 표시
        ///// </summary>
        ///// <param name="carsToShow">표시할 차량 리스트</param>
        ///// <returns>표시된 차량 개수</returns>
        //public int ShowSpecificCars(List<CObjCar> carsToShow)
        //{
        //    if (carsToShow == null || carsToShow.Count == 0)
        //    {
        //        CLogger.Inst.Warning("표시할 차량 리스트가 비어있습니다", "CarList");
        //        return 0;
        //    }

        //    int shownCount = 0;

        //    foreach (var car in carsToShow)
        //    {
        //        if (car != null && car.gameObject != null && !car.gameObject.activeSelf)
        //        {
        //            car.gameObject.SetActive(true);
        //            shownCount++;

        //            CLogger.Inst.Debug($"차량 표시: {car.m_NameId} (idx={car.m_Idx})", "CarList");
        //        }
        //    }

        //    CLogger.Inst.Debug($"특정 차량 {shownCount}개 표시 완료", "CarList");

        //    return shownCount;
        //}

        ///// <summary>
        ///// 숨겨진 차량 개수 반환
        ///// </summary>
        //public int GetHiddenCarCount()
        //{
        //    int count = 0;

        //    foreach (var car in m_ObjCarList)
        //    {
        //        if (car != null && car.gameObject != null && !car.gameObject.activeSelf)
        //        {
        //            count++;
        //        }
        //    }

        //    return count;
        //}

        ///// <summary>
        ///// 활성화된 차량 개수 반환
        ///// </summary>
        //public int GetActiveCarCount()
        //{
        //    int count = 0;

        //    foreach (var car in m_ObjCarList)
        //    {
        //        if (car != null && car.gameObject != null && car.gameObject.activeSelf)
        //        {
        //            count++;
        //        }
        //    }

        //    return count;
        //}
        #endregion

    public void Reset_CreateCarObjectList(Camera kCamera, bool bRandomCreate=true)
    {
        CLogger.Inst.Debug("========== Reset_CreateCarObjectList 시작 ==========", "CarList");
        
        // ✅ 수정: 리스트 복사 (반복 중 수정 방지)
        List<CObjCar> carsToRemove = new List<CObjCar>(m_ObjCarList);
        
        CLogger.Inst.Debug($"기존 차량 수: {carsToRemove.Count}", "CarList");
        
        // ✅ 오브젝트 풀 사용 시 Despawn
        if (m_UseObjectPooling && m_ObjectPool != null)
        {
            // 풀로 반환
            foreach (var car in carsToRemove)
            {
                if (car != null && car.gameObject != null)
                {
                    CLogger.Inst.Debug($"차량 반환: {car.m_NameId} (idx={car.m_Idx}, ID={car.gameObject.GetInstanceID()})", "CarList");
                    m_ObjectPool.Despawn(car.gameObject);
                }
            }
        }
        else
        {
            // 기존 방식: 파괴
            RemoveAll();
        }
        
        // ✅ Clear는 Despawn 후에
        m_ObjCarList.Clear();
        m_CurCarIDList.Clear();
        
        // ✅ 추가: 씬에 남아있는 미아 차량 강제 제거
        CleanupOrphanedCars();

        var kSaveData = CDataMgr.Inst.m_SaveCarPosData.m_Data;
        
        CLogger.Inst.Debug($"차량 생성 시작: {kSaveData.datas.Count}개", "CarList");
        
        for (int i = 0; i < kSaveData.datas.Count; i++)
        {
            SObjectPos kPos = kSaveData.datas[i];
            int carIdx = kPos.prefabId;
            CObjCar kObjCar = null;
            if (bRandomCreate)
                kObjCar = CreateRandomCarObjectByCarPos(kPos.pos.Get(), kPos, kCamera, ref carIdx);
            else
                kObjCar = CreateCarObjectByCarPos(kPos.pos.Get(), kPos, kCamera);

            kObjCar.m_iPresetId = kPos.presetId;
            m_CurCarIDList.Add(carIdx);
        }

        CLogger.Inst.Debug($"차량 생성 완료: {m_ObjCarList.Count}개 (활성: {m_CurCarIDList.Count})", "CarList");

        // ✅ 검증 1: 생성된 차량 수 확인
        if (m_ObjCarList.Count != kSaveData.datas.Count)
        {
            CLogger.Inst.Error($"❌ 차량 생성 불일치! 예상: {kSaveData.datas.Count}, 실제: {m_ObjCarList.Count}", "CarList");
        }
        
        // ✅ 검증 2: 중복 idx 확인
        HashSet<int> indices = new HashSet<int>();
        foreach (var car in m_ObjCarList)
        {
            if (car != null)
            {
                if (!indices.Add(car.m_Idx))
                {
                    CLogger.Inst.Error($"❌ 중복 idx 발견: {car.m_Idx} (차량: {car.m_NameId})", "CarList");
                }
            }
        }
        
        // ✅ 검증 3: 중복 GameObject 확인 (인스턴스 ID)
        HashSet<int> instanceIds = new HashSet<int>();
        foreach (var car in m_ObjCarList)
        {
            if (car != null && car.gameObject != null)
            {
                int instanceId = car.gameObject.GetInstanceID();
                if (!instanceIds.Add(instanceId))
                {
                    CLogger.Inst.Error($"❌❌ 치명적: 같은 GameObject 인스턴스가 2번 추가됨! (차량: {car.m_NameId}, ID: {instanceId})", "CarList");
                }
            }
        }
        
        // ✅ 검증 4: 중복 위치 확인
        List<Vector3> positions = new List<Vector3>();
        foreach (var car in m_ObjCarList)
        {
            if (car != null && car.gameObject != null)
            {
                Vector3 pos = car.transform.position;
                foreach (var existingPos in positions)
                {
                    if (Vector3.Distance(pos, existingPos) < 0.01f)
                    {
                        CLogger.Inst.Error($"❌ 중복 위치 발견: {pos} (차량: {car.m_NameId}, idx={car.m_Idx}, ID={car.gameObject.GetInstanceID()})", "CarList");
                    }
                }
                positions.Add(pos);
            }
        }

        if (m_ObjCarList.Count > 0)
        {
            m_SelectdCarObject = m_ObjCarList[0].gameObject;
        }
        
        // ✅ 풀 통계 출력 (디버깅용)
        if (m_UseObjectPooling && m_ObjectPool != null)
        {
            m_ObjectPool.PrintPoolStats();
        }
        
        CLogger.Inst.Debug("========== Reset_CreateCarObjectList 완료 ==========", "CarList");
    }
    
    /// <summary>
    /// 씬에 남아있는 미아 차량 강제 제거
    /// </summary>
    private void CleanupOrphanedCars()
    {
        CLogger.Inst.Debug("미아 차량 검사 시작...", "CarList");
        
        // 씬의 모든 CObjCar 찾기
        CObjCar[] allCars = FindObjectsOfType<CObjCar>();
        
        int orphanCount = 0;
        foreach (var car in allCars)
        {
            // m_ObjCarList에 없으면 미아
            if (!m_ObjCarList.Contains(car))
            {
                CLogger.Inst.Warning($"⚠️ 미아 차량 발견: {car.name} (idx={car.m_Idx}, ID={car.gameObject.GetInstanceID()}, 활성={car.gameObject.activeInHierarchy})", "CarList");
                
                if (m_UseObjectPooling && m_ObjectPool != null)
                {
                    m_ObjectPool.Despawn(car.gameObject);
                }
                else
                {
                    Destroy(car.gameObject);
                }
                
                orphanCount++;
            }
        }
        
        if (orphanCount > 0)
        {
            CLogger.Inst.Warning($"✅ 총 {orphanCount}개 미아 차량 정리 완료", "CarList");
        }
        else
        {
            CLogger.Inst.Debug("미아 차량 없음", "CarList");
        }
    }
    
    // ✅ 수정: 오브젝트 풀 사용
    public void RemoveCarObject(CObjCar kCar)
    {
        if (kCar != null)
        {
            GameObject go = kCar.gameObject;
            m_ObjCarList.Remove(kCar);
            
            // ✅ 수정: 풀링 사용 시 Despawn
            if (m_UseObjectPooling && m_ObjectPool != null)
            {
                m_ObjectPool.Despawn(go);
            }
            else
            {
                Destroy(go, 0.02f);
            }
        }
    }

    public void RemoveAll()
    {
        // ✅ 수정: 풀링 사용 시 Despawn
        if (m_UseObjectPooling && m_ObjectPool != null)
        {
            foreach (var kObj in m_ObjCarList)
            {
                if (kObj != null && kObj.gameObject != null)
                    m_ObjectPool.Despawn(kObj.gameObject);
            }
        }
        else
        {
            for( int i = 0; i < m_ObjCarList.Count; i++)
            {
                CObjCar kObj = m_ObjCarList[i];
                if( kObj != null && kObj.gameObject != null)
                {
                    Destroy(kObj.gameObject);
                }
            }
        }
        m_ObjCarList.Clear();
    }

    // 차량 색상을 무작위로 변경한다.
    public void SetRandomColorOfCarList()
    {
        foreach (var kCarObj in m_ObjCarList)
        {
            if (kCarObj != null && kCarObj.gameObject != null)
            {
                if( kCarObj.gameObject.activeSelf == true)
                {
                    // 차량 색상 무작위화
                    kCarObj.SetVehicleColor(CSaveCarPosData.GetRandomColor());
                }
            }
        }
    }
}