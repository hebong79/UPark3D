using System.Collections.Generic;
using System;
using UnityEngine;

/// <summary>
/// CCarObjListUI의 m_PrefabList 갯수 및 순서가 동일해야 한다.
/// </summary>

[CreateAssetMenu(fileName = "CarObjList", menuName = "ScriptableObjects/CarObjects", order = 1)]
public class CScoCarData : ScriptableObject
{
    [Serializable]
    public class SCarData
    {
        public int m_Idx = 1;               // 1부터 시작한다.
        //public string m_Name = "";
        public ECarType m_Type = ECarType.Medium;
        public string m_PrefabName = ""; // 차량 종류 ( 예: car, suv, truck, bus... )
        //public string m_CarKind = "";      
    }

    public List<SCarData> m_Datas = new List<SCarData>();

    public SCarData GetCarData(int idx)
    {
        return m_Datas.Find(x => x.m_Idx == idx);
        //if( idx >= 0 && idx < m_Datas.Count )
        //    return m_Datas[idx];

        //return null;

    }

    /// <summary>
    /// 프리팹 이름 얻기
    /// </summary>
    /// <param name="idx"></param>
    /// <returns></returns>
    public string GetCarName(int idx)
    {
        SCarData kData = GetCarData(idx);
        if( kData != null)
            return kData.m_PrefabName;

        return "";
    }


}



