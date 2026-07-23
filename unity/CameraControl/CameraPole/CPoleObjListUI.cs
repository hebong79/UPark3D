using System.Collections.Generic;
using UnityEngine;



public class CPoleObjListUI : MonoBehaviour
{
    public List<GameObject> m_PrefabList = new List<GameObject>();   // 프리팹 리스트
    public List<CObjPole> m_ObjPoleList = new List<CObjPole>();

    [HideInInspector] public GameObject m_CurPrefab = null;       // 선택된 프리팹
    [HideInInspector] public GameObject m_SelectdPloeObject = null;

    // Start is called once before the first execution of Update after the MonoBehaviour is created
    void Start()
    {
        
    }
    public void Initialize()
    {
        //Init_LoadCameraPrefabList();
        Reset_CreatePoleObjectList();
    }

    // 리소스에서 프리팹을 로딩한다.
    //public void Init_LoadCameraPrefabList()
    //{
    //    m_PrefabList.Clear();

    //    string sBasePath = CDataMgr.DCAMERAOBJ_PREFAB_PATH;
    //}

    public GameObject GetObjectPrefab(int idx)
    {
        if (idx >= 0 && idx < m_PrefabList.Count)
            return m_PrefabList[idx];

        return null;
    }

    public void SetCurPrefab(int idx)
    {
        m_CurPrefab = GetObjectPrefab(idx);
    }

    public void AddPoleObject(CObjPole kCamObj)
    {
        m_ObjPoleList.Add(kCamObj);
    }


    public CObjPole GetPoleObject(int idx)
    {
        if (idx >= 0 && idx < m_ObjPoleList.Count)
            return m_ObjPoleList[idx];

        return null;
    }


    public GameObject CreatePoleObject(Vector3 vPos, SObjectPos kObjPos )
    {
        if (m_CurPrefab != null)
        {
            GameObject go = Instantiate(m_CurPrefab, this.transform);
            go.transform.localScale = m_CurPrefab.transform.localScale;
            go.transform.position = vPos;
            go.transform.localRotation = Quaternion.Euler(new Vector3(0, kObjPos.rotY, 0));
            CObjPole kObj = go.GetComponent<CObjPole>();
            kObj.Initialize(m_ObjPoleList.Count, kObjPos.id);
            this.AddPoleObject(kObj);

            m_SelectdPloeObject = kObj.gameObject;
            return m_SelectdPloeObject;
        }
        return null;
    }

    public GameObject CreatePoleObjectByPolePos(Vector3 vPos, SObjectPos kObjPos)
    {
        GameObject goPrefab = this.GetObjectPrefab(kObjPos.prefabId);
        Debug.Assert(goPrefab != null, $"Prefab Id = {kObjPos.prefabId}");
        if (goPrefab != null)
        {
            GameObject go = Instantiate(goPrefab, this.transform);
            go.transform.localScale = goPrefab.transform.localScale;
            go.transform.position = vPos;
            go.transform.localRotation = Quaternion.Euler(new Vector3(0, kObjPos.rotY, 0));
            CObjPole kObjPole = go.GetComponent<CObjPole>();
            kObjPole.Initialize(m_ObjPoleList.Count, kObjPos.id);
            this.AddPoleObject(kObjPole);

            m_SelectdPloeObject = kObjPole.gameObject;
            return m_SelectdPloeObject;
        }
        return null;

    }
    public void Reset_CreatePoleObjectList()
    {
        this.RemoveAll();

        var kSaveData = CDataMgr.Inst.m_SavePolePosData.m_Data;
        for (int i = 0; i < kSaveData.datas.Count; i++)
        {
            SObjectPos kPos = kSaveData.datas[i];
            CreatePoleObjectByPolePos(kPos.pos.Get(), kPos);
        }

        if (m_ObjPoleList.Count > 0)
        {
            m_SelectdPloeObject = m_ObjPoleList[0].gameObject;
        }
    }

    public void RemovePoleObject(CObjPole kObj)
    {
        if (kObj != null)
        {
            GameObject go = kObj.gameObject;
            m_ObjPoleList.Remove(kObj);
            Destroy(go, 0.05f);
        }
    }

    public void RemoveAll()
    {
        for (int i = 0; i < m_ObjPoleList.Count; i++)
        {
            CObjPole kObj = m_ObjPoleList[i];
            if (kObj != null && kObj.gameObject != null)
            {
                CObjCamera kObjCam = kObj.m_trCamPos.GetComponentInChildren<CObjCamera>();
                if (kObjCam != null)
                    kObjCam.transform.parent = null;

                Destroy(kObj.gameObject, 0.1f);
            }
        }

        m_ObjPoleList.Clear();
    }

    public void RemoveAll_Destroy()
    {
        for (int i = 0; i < m_ObjPoleList.Count; i++)
        {
            CObjPole kObj = m_ObjPoleList[i];
            if (kObj != null && kObj.gameObject != null)
            {
                Destroy(kObj.gameObject);
            }
        }
        m_ObjPoleList.Clear();
    }
}
