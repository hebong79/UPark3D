using System.Collections.Generic;
using UnityEngine;

/// <summary>
/// 차량 오브젝트 풀링 시스템
/// 메모리 누수 및 GC 부하를 줄이기 위해 오브젝트를 재사용합니다.
/// </summary>
public class CCarObjectPool : MonoBehaviour
{
    [System.Serializable]
    public class PoolInfo
    {
        public GameObject prefab;           // 프리팹
        public int initialSize = 10;        // 초기 풀 크기
        public int maxSize = 30;            // 현재 최대 풀 크기 (유동적으로 증가)
        public bool allowGrowth = true;     // 동적 확장 허용 여부
        public int growthStep = 10;         // maxSize 초과 시 확장 단위
    }

    [Header("풀 설정")]
    [SerializeField] private List<PoolInfo> m_PoolInfos = new List<PoolInfo>();
    
    // 풀 오브젝트의 부모 Transform
    public Transform m_PoolParent;
    
    // 프리팹별 풀 저장소
    private Dictionary<GameObject, Queue<GameObject>> m_PoolDictionary = new Dictionary<GameObject, Queue<GameObject>>();
    
    // 활성 오브젝트 추적 (반환 시 검증용)
    private Dictionary<GameObject, GameObject> m_ActiveObjects = new Dictionary<GameObject, GameObject>(); // 인스턴스 -> 프리팹
    
    // 풀 통계
    private Dictionary<GameObject, int> m_PoolStats = new Dictionary<GameObject, int>(); // 프리팹 -> 총 생성 개수

    

    void Awake()
    {
        if (m_PoolParent == null)
        {
            m_PoolParent = new GameObject("CarObjectPool").transform;
            m_PoolParent.SetParent(transform);
        }
        InitializePools();
    }

    /// <summary>
    /// 풀 초기화 - 프리팹별로 초기 개수만큼 미리 생성
    /// </summary>
    private void InitializePools()
    {
        foreach (var info in m_PoolInfos)
        {
            if (info.prefab == null)
            {
                Debug.LogWarning("CCarObjectPool: Prefab이 null입니다. 스킵합니다.");
                continue;
            }

            Queue<GameObject> pool = new Queue<GameObject>();
            
            for (int i = 0; i < info.initialSize; i++)
            {
                GameObject obj = CreateNewPoolObject(info.prefab);
                pool.Enqueue(obj);
            }

            m_PoolDictionary.Add(info.prefab, pool);
            m_PoolStats.Add(info.prefab, info.initialSize);

            Debug.Log($"CCarObjectPool: '{info.prefab.name}' 풀 초기화 완료 ({info.initialSize}개)");
        }
    }

    /// <summary>
    /// 새 풀 오브젝트 생성
    /// </summary>
    private GameObject CreateNewPoolObject(GameObject prefab)
    {
        GameObject obj = Instantiate(prefab, m_PoolParent);
        obj.SetActive(false);
        return obj;
    }

    /// <summary>
    /// 풀에서 오브젝트 가져오기
    /// </summary>
    /// <param name="prefab">프리팹</param>
    /// <param name="position">위치</param>
    /// <param name="rotation">회전</param>
    /// <param name="parent">부모 Transform</param>
    /// <returns>활성화된 오브젝트 (null이면 실패)</returns>
    public GameObject Spawn(GameObject prefab, Vector3 position, Quaternion rotation, Transform parent = null)
    {
        if (prefab == null)
        {
            Debug.LogError("CCarObjectPool: Prefab이 null입니다!");
            return null;
        }

        // 풀이 존재하지 않으면 생성
        if (!m_PoolDictionary.ContainsKey(prefab))
        {
            Debug.LogWarning($"CCarObjectPool: '{prefab.name}' 풀이 없습니다. 자동 생성합니다.");
            m_PoolDictionary.Add(prefab, new Queue<GameObject>());
            m_PoolStats.Add(prefab, 0);
        }

        GameObject obj = null;
        Queue<GameObject> pool = m_PoolDictionary[prefab];

        // ✅ 검증 실패한 오브젝트를 임시 저장 (풀에 다시 넣기 위해)
        List<GameObject> rejectedObjs = new List<GameObject>();
        
        // ✅ 수정: 초기 풀 크기를 저장 (Dequeue 전)
        int initialPoolSize = pool.Count;
        int maxAttempts = Mathf.Max(initialPoolSize, 1);
        int attempts = 0;
        
        CLogger.Inst.Debug($"풀에서 검색 시작: {prefab.name} (풀 크기: {initialPoolSize})", "ObjectPool");
        
        while (pool.Count > 0 && attempts < maxAttempts)
        {
            attempts++;
            GameObject candidate = pool.Dequeue();
            
            // ✅ 오브젝트 검증
            bool isValid = false;
            string rejectReason = "";
            
            if (candidate == null)
            {
                rejectReason = "null 오브젝트";
            }
            else if (candidate.activeInHierarchy)
            {
                rejectReason = "이미 활성화됨";
            }
            else if (m_ActiveObjects.ContainsKey(candidate))
            {
                rejectReason = "활성 목록에 존재";
            }
            else
            {
                isValid = true;
            }
            
            if (isValid)
            {
                obj = candidate;
                CLogger.Inst.Debug($"✅ 사용 가능한 오브젝트 발견: {obj.name}", "ObjectPool");
                break;
            }
            else
            {
                // ✅ 검증 실패 → 임시 리스트에 저장
                if (candidate != null)
                {
                    rejectedObjs.Add(candidate);
                    CLogger.Inst.Warning($"{candidate.name}이(가) 비활성화 되지 않음 - 반환되었습니다.", "ObjectPool");
                }
            }
        }
        
        // ✅ 검증 실패한 오브젝트들을 풀에 다시 넣기
        foreach (var rejected in rejectedObjs)
        {
            pool.Enqueue(rejected);
        }
        
        if (rejectedObjs.Count > 0)
        {
            CLogger.Inst.Warning($"검증 실패한 오브젝트 {rejectedObjs.Count}개를 풀에 복원", "ObjectPool");
        }

        // 풀이 비었거나 모든 오브젝트가 사용 불가능한 경우
        if (obj == null)
        {
            // 동적 확장 가능 여부 확인
            PoolInfo info = m_PoolInfos.Find(p => p.prefab == prefab);
            bool canGrow = info == null || info.allowGrowth;

            if (canGrow)
            {
                // maxSize 초과 시 growthStep만큼 자동 확장
                if (info != null && m_PoolStats[prefab] >= info.maxSize)
                {
                    int prevMax = info.maxSize;
                    info.maxSize += info.growthStep;
                    CLogger.Inst.Warning($"⚠️ 풀 maxSize 자동 확장: '{prefab.name}' ({prevMax} → {info.maxSize})", "ObjectPool");
                }

                obj = CreateNewPoolObject(prefab);
                m_PoolStats[prefab]++;
                int currentMax = info?.maxSize ?? m_PoolStats[prefab];
                CLogger.Inst.Info($"풀 확장: '{prefab.name}' ({m_PoolStats[prefab]}/{currentMax})", "ObjectPool");
            }
            else
            {
                int maxSize = info?.maxSize ?? 30;
                CLogger.Inst.Error($"❌ 풀 최대 크기 도달 (확장 불가): '{prefab.name}' ({maxSize})", "ObjectPool");
                return null;
            }
        }

        // ✅ 오브젝트 활성화 및 설정
        obj.transform.SetParent(parent ?? m_PoolParent);
        obj.transform.position = position;
        obj.transform.rotation = rotation;
        
        // ✅ 최종 검증: 활성 목록에 이미 있는지 확인
        if (m_ActiveObjects.ContainsKey(obj))
        {
            CLogger.Inst.Error($"❌❌ 치명적 오류: 오브젝트가 이미 활성 목록에 존재 (Spawn 직전): {obj.name}", "ObjectPool");
            // 새 오브젝트 강제 생성
            obj = CreateNewPoolObject(prefab);
            m_PoolStats[prefab]++;
        }

        m_ActiveObjects[obj] = prefab;
        
        // ✅ 활성화
        obj.SetActive(true);

        CLogger.Inst.Debug($"풀에서 생성 완료: {obj.name} (활성: {m_ActiveObjects.Count}, 풀: {pool.Count})", "ObjectPool");

        return obj;
    }

    /// <summary>
    /// 오브젝트를 풀로 반환
    /// </summary>
    /// <param name="obj">반환할 오브젝트</param>
    public void Despawn(GameObject obj)
    {
        if (obj == null)
        {
            CLogger.Inst.Warning("CCarObjectPool: 반환할 오브젝트가 null입니다!", "ObjectPool");
            return;
        }

        string objName = obj.name;
        int objInstanceId = obj.GetInstanceID();
        
        CLogger.Inst.Debug($"Despawn 시작: {objName} (ID: {objInstanceId}, 활성: {obj.activeInHierarchy})", "ObjectPool");

        // ✅ 2. 부모 변경 (풀로 이동)
        //obj.transform.SetParent(m_PoolParent);

        // ✅ 1. 무조건 즉시 비활성화 (최우선)
        if (obj.activeInHierarchy)
        {
            obj.SetActive(false);
            CLogger.Inst.Debug($"오브젝트 비활성화: {objName}", "ObjectPool");
        }
        
        // ✅ 3. 활성 오브젝트인지 확인
        bool wasActive = m_ActiveObjects.ContainsKey(obj);
        GameObject prefab = null;
        
        if (wasActive)
        {
            prefab = m_ActiveObjects[obj];
            m_ActiveObjects.Remove(obj);
            CLogger.Inst.Debug($"활성 목록에서 제거: {objName} (남은 활성: {m_ActiveObjects.Count})", "ObjectPool");
        }
        else
        {
            // ✅ 4. 미아 오브젝트 처리 (ActiveObjects에 없는 경우)
            CLogger.Inst.Warning($"⚠️ '{objName}'은 활성 목록에 없습니다! 미아 오브젝트 복구 시도...", "ObjectPool");
            
            // 프리팹 추정 (이름으로)
            prefab = FindPrefabByObjectName(obj);
            
            if (prefab == null)
            {
                CLogger.Inst.Error($"❌ '{objName}' 프리팹을 찾을 수 없어 파괴합니다", "ObjectPool");
                Destroy(obj);
                return;
            }
            
            CLogger.Inst.Info($"✅ 미아 오브젝트 프리팹 발견: {prefab.name}", "ObjectPool");
        }

        // ✅ 5. 풀에 추가 (중복 방지)
        if (m_PoolDictionary.ContainsKey(prefab))
        {
            // 중복 체크
            bool isDuplicate = false;
            int duplicateCount = 0;
            
            foreach (var pooledObj in m_PoolDictionary[prefab])
            {
                if (pooledObj == obj)
                {
                    isDuplicate = true;
                    duplicateCount++;
                }
            }

            if (!isDuplicate)
            {
                m_PoolDictionary[prefab].Enqueue(obj);
                
                if (wasActive)
                {
                    CLogger.Inst.Debug($"✅ 풀로 반환 완료: {objName} (풀 크기: {m_PoolDictionary[prefab].Count})", "ObjectPool");
                }
                else
                {
                    CLogger.Inst.Info($"✅ 미아 오브젝트 복구 완료: {objName} (풀 크기: {m_PoolDictionary[prefab].Count})", "ObjectPool");
                }
            }
            else
            {
                CLogger.Inst.Error($"❌❌ 중복 반환 감지: '{objName}'이 풀에 {duplicateCount}번 존재합니다!", "ObjectPool");
                
                // 중복된 오브젝트 제거
                Queue<GameObject> cleanedPool = new Queue<GameObject>();
                bool kept = false;
                
                while (m_PoolDictionary[prefab].Count > 0)
                {
                    GameObject pooledObj = m_PoolDictionary[prefab].Dequeue();
                    if (pooledObj == obj && !kept)
                    {
                        // 첫 번째는 유지
                        cleanedPool.Enqueue(pooledObj);
                        kept = true;
                    }
                    else if (pooledObj != obj)
                    {
                        cleanedPool.Enqueue(pooledObj);
                    }
                    else
                    {
                        CLogger.Inst.Warning($"중복 제거: {pooledObj.name}", "ObjectPool");
                    }
                }
                
                m_PoolDictionary[prefab] = cleanedPool;
                CLogger.Inst.Info($"중복 정리 완료. 정리 후 풀 크기: {m_PoolDictionary[prefab].Count}", "ObjectPool");
            }
        }
        else
        {
            CLogger.Inst.Error($"❌ 풀이 없음: '{prefab.name}' - 오브젝트를 파괴합니다.", "ObjectPool");
            Destroy(obj);
        }
    }
    
    /// <summary>
    /// 오브젝트 이름으로 프리팹 찾기 (미아 오브젝트 복구용)
    /// </summary>
    private GameObject FindPrefabByObjectName(GameObject obj)
    {
        string objName = obj.name.Replace("(Clone)", "").Trim();
        
        foreach (var kvp in m_PoolDictionary)
        {
            if (kvp.Key != null && kvp.Key.name == objName)
            {
                return kvp.Key;
            }
        }
        
        return null;
    }

    /// <summary>
    /// 모든 활성 오브젝트를 풀로 반환
    /// </summary>
    public void DespawnAll()
    {
        // 활성 오브젝트 목록을 복사 (반복 중 수정 방지)
        List<GameObject> activeObjects = new List<GameObject>(m_ActiveObjects.Keys);

        foreach (var obj in activeObjects)
        {
            if (obj != null)
                Despawn(obj);
        }

        Debug.Log($"CCarObjectPool: 모든 활성 오브젝트 반환 완료 ({activeObjects.Count}개)");
    }

    /// <summary>
    /// 특정 프리팹의 풀 통계 반환
    /// </summary>
    public int GetPoolSize(GameObject prefab)
    {
        if (m_PoolDictionary.ContainsKey(prefab))
            return m_PoolDictionary[prefab].Count;
        return 0;
    }

    /// <summary>
    /// 특정 프리팹의 활성 오브젝트 수 반환
    /// </summary>
    public int GetActiveCount(GameObject prefab)
    {
        int count = 0;
        foreach (var kvp in m_ActiveObjects)
        {
            if (kvp.Value == prefab)
                count++;
        }
        return count;
    }

    /// <summary>
    /// 풀 통계 출력 (디버깅용)
    /// </summary>
    public void PrintPoolStats()
    {
        CLogger.Inst.Info("========== CCarObjectPool 통계 ==========", "ObjectPool");
        foreach (var kvp in m_PoolStats)
        {
            GameObject prefab = kvp.Key;
            int totalCreated = kvp.Value;
            int poolSize = GetPoolSize(prefab);
            int active = GetActiveCount(prefab);

            PoolInfo info = m_PoolInfos.Find(p => p.prefab == prefab);
            string maxSizeStr = info != null ? $"{info.maxSize} (step:{info.growthStep})" : "-";

            CLogger.Inst.Info($"[{prefab.name}] 총 생성: {totalCreated}, 풀: {poolSize}, 활성: {active}, maxSize: {maxSizeStr}", "ObjectPool");
        }
        CLogger.Inst.Info("=========================================", "ObjectPool");
    }

    /// <summary>
    /// 풀 무결성 검증 (디버깅용)
    /// </summary>
    public bool ValidatePoolIntegrity()
    {
        bool isValid = true;
        
        CLogger.Inst.Info("========== 풀 무결성 검증 시작 ==========", "ObjectPool");
        
        foreach (var kvp in m_PoolDictionary)
        {
            GameObject prefab = kvp.Key;
            Queue<GameObject> pool = kvp.Value;
            
            // ✅ 검증 1: 풀 내 중복 오브젝트 확인
            HashSet<int> instanceIds = new HashSet<int>();
            List<GameObject> poolList = new List<GameObject>(pool);
            
            foreach (var obj in poolList)
            {
                if (obj != null)
                {
                    int id = obj.GetInstanceID();
                    if (!instanceIds.Add(id))
                    {
                        CLogger.Inst.Error($"❌ [{prefab.name}] 풀 내 중복 오브젝트 발견: {obj.name} (ID: {id})", "ObjectPool");
                        isValid = false;
                    }
                }
            }
            
            // ✅ 검증 2: 풀과 활성 목록 교집합 확인
            foreach (var obj in poolList)
            {
                if (obj != null && m_ActiveObjects.ContainsKey(obj))
                {
                    CLogger.Inst.Error($"❌ [{prefab.name}] 활성 오브젝트가 풀에 존재: {obj.name}", "ObjectPool");
                    isValid = false;
                }
            }
            
            // ✅ 검증 3: 활성화된 오브젝트가 풀에 있는지 확인
            foreach (var obj in poolList)
            {
                if (obj != null && obj.activeInHierarchy)
                {
                    CLogger.Inst.Error($"❌ [{prefab.name}] 활성화된 오브젝트가 풀에 존재: {obj.name}", "ObjectPool");
                    isValid = false;
                }
            }
        }
        
        // ✅ 검증 4: 활성 목록 검증
        foreach (var kvp in m_ActiveObjects)
        {
            GameObject obj = kvp.Key;
            GameObject prefab = kvp.Value;
            
            if (obj == null)
            {
                CLogger.Inst.Error($"❌ 활성 목록에 null 오브젝트 존재", "ObjectPool");
                isValid = false;
            }
            else if (!obj.activeInHierarchy)
            {
                CLogger.Inst.Warning($"⚠️ 활성 목록에 비활성화된 오브젝트: {obj.name}", "ObjectPool");
            }
        }
        
        if (isValid)
        {
            CLogger.Inst.Info("✅ 풀 무결성 검증 통과", "ObjectPool");
        }
        else
        {
            CLogger.Inst.Error("❌ 풀 무결성 검증 실패!", "ObjectPool");
        }
        
        CLogger.Inst.Info("=========================================", "ObjectPool");
        
        return isValid;
    }

    void OnDestroy()
    {
        // 모든 풀 오브젝트 파괴
        foreach (var pool in m_PoolDictionary.Values)
        {
            while (pool.Count > 0)
            {
                GameObject obj = pool.Dequeue();
                if (obj != null)
                    Destroy(obj);
            }
        }

        m_PoolDictionary.Clear();
        m_ActiveObjects.Clear();
        m_PoolStats.Clear();

        Debug.Log("CCarObjectPool: 모든 리소스 정리 완료");
    }
}
