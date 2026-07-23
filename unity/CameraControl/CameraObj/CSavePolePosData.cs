using System;
using System.Collections.Generic;
using Newtonsoft.Json;
using UnityEngine;
using System.IO;
//using Newtonsoft.Json;


public class SObjectPos
{
    public string id = "";                  // 차량obj id(시간으로 된 구분용)
    public int type = 0;                    // 오브젝트 타입 ( 사용처에 맞는 타입, 1부터 시작, 예) 차량타입, 오브젝트 메쉬 타입 등 )
    public int presetId = -1;               // 프리셋 아이디 ( 1부터 시작 )
    public int slotId = -1;                 // 주차장의 주차면 id ( 1부터 시작, -1: 슬롯외의 영역에 주차됨 )                                              
    public int prefabId = -1;               // 오브젝트(프리팹) id ( 1부터 시작 )
    public SVector3 pos = new SVector3();   // 위치값
    public float rotY = 0;                  // y축 회전값
    public bool isFront = true;             // 주차면을 카메라가 바라볼때 차량의 앞 / 뒤 ( true: 전면, false: 후면 )
}

/// <summary>
///                                                                                        
/// </summary>
public class CSavePolePosData : CSaveObjectPosData
{

}

/// <summary>
/// 오브젝트 위치 정보를 저장 관리 한다. 
/// </summary>
public class CSaveObjectPosData
{
    /// <summary>
    /// 오브젝트 위치 정보 리스트
    /// </summary>
    public class SObjectPosDatas
    {
        public List<SObjectPos> datas = new List<SObjectPos>();
    }

    //변수 -----------------------------------------------------------

    public SObjectPosDatas m_Data = new SObjectPosDatas();

    //---------------------------------------------------------------
    public void Add(SObjectPos data)
    {
        m_Data.datas.Add(data);
    }

    public void Remove(SObjectPos data)
    {
        m_Data.datas.Remove(data);
    }

    public void RemoveById(string id)
    {
        SObjectPos removeData = m_Data.datas.Find(x => x.id == id);
        if (removeData != null) {
            m_Data.datas.Remove(removeData);
        }
    }

    public SObjectPos GetObjectPosData(int idx)
    {
        if (idx >= 0 && idx < m_Data.datas.Count){
            return m_Data.datas[idx];
        }
        return null;
    }
    public SObjectPos GetObjectPosDataById(string  id)
    {
        return m_Data.datas.Find(x =>  x.id.Equals(id));
    }

    public bool SaveToJson(string path, string fileName)
    {
        if (!Directory.Exists(path)) {
            Directory.CreateDirectory(path);
        }
        string sPathName = Path.Combine(path, fileName);
        string sringData = JsonConvert.SerializeObject(m_Data);
        //string sringData = MakeDataToString();

        try
        {
            File.WriteAllText(sPathName, sringData);
            return true;
        }
        catch (Exception e)
        {
            Debug.Log(e);
        }
        return false;
    }

    public bool LoadFromJson(string path, string fileName)
    {
        Clear();

        string sPathName = fileName;
        if (path != string.Empty)
            sPathName = Path.Combine(path, fileName);

        try
        {
            string stringData = File.ReadAllText(sPathName);
            m_Data = JsonConvert.DeserializeObject<SObjectPosDatas>(stringData);
        }
        catch (Exception e)
        {
            Debug.Log(e);
            return false;
        }
        return true;
    }

    public void Clear()
    {
        m_Data.datas.Clear();
    }


}
