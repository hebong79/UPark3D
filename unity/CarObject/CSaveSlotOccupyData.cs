using System;
using System.Collections.Generic;
using UnityEngine;
using Newtonsoft.Json;


[Serializable]
public class SSOSlotItem
{
    public int slot = 0;                // 주차면 번호 ( 1 부터 시작 )
    public bool occupied = false;       // 주차면 점유 여부
    public bool is_front = true;        // 주차면에서 차량이 보이는 방향 ( true: 전면, false: 후면 )
    public float rotation_y = 0f;       // 차량 회전 각도 ( Y축 기준 )
    public int preset_id = -1;          // 프리셋 아이디 ( 프리셋 아이디 - 1 부터 시작 )
    public string vehicle_type= "";     // 주차면 점유 차량 종류 (ex) car, suv, truck, bus ... )
}

[Serializable]
public class SSOData
{
    public string img = "";                     // 이미지 경로및 이름   
    public List<SSOSlotItem> slots = new();     // 주차면 리스트
    public int total_slots = 0;                 // 전체 주차면 수
    public int occupied_count = 0;              // 점유된 주차면 수   
}

public class CSaveSlotOccupyData
{
    //jsonl 형식으로 여러 데이터 저장 가능
    public List<SSOData> m_Datas = new List<SSOData>();

    /// <summary>
    /// jsonl 형식으로 여러 데이터 저장 
    /// </summary>
    /// <param name="pathName"></param>
    public void SaveFile_Jsonl(string pathName)
    {
        try
        {
            string jsonl_data = "";
            foreach (var data in m_Datas)
            {
                string json = JsonConvert.SerializeObject(data);
                jsonl_data += json + "\n";
            }
            System.IO.File.WriteAllText(pathName, jsonl_data);
        }
        catch (Exception e)
        {
            Debug.LogError(e.Message);
        }

    }

    public void LoadFile_Jsonl(string pathName)
    {
        m_Datas.Clear();
        try
        {
            if (System.IO.File.Exists(pathName))
            {
                string[] jsonl_lines = System.IO.File.ReadAllLines(pathName);
                foreach (var line in jsonl_lines)
                {
                    var data = JsonConvert.DeserializeObject<SSOData>(line);
                    m_Datas.Add(data);
                }
            }
        }
        catch (Exception e)
        {
            Debug.LogError(e.Message);
        }
    }

    public static void SaveJson(string pathName, SSOData data)
    {
        try
        {
            string json = JsonConvert.SerializeObject(data);
            System.IO.File.WriteAllText(pathName, json);
        }
        catch (Exception e)
        {
            Debug.LogError(e.Message);
        }
    }

    public static SSOData LoadJson(string pathName)
    {
        var data = new SSOData();
        try
        {
            if (System.IO.File.Exists(pathName))
            {
                string json = System.IO.File.ReadAllText(pathName);
                data = JsonConvert.DeserializeObject<SSOData>(json);
                return data;
            }
        }
        catch (Exception e)
        {
            Debug.LogError(e.Message);
        }
        return null;
    }

    public void Add(SSOData data)
    {
        m_Datas.Add(data);
    }

    public void Add(string imgName, List<SSOSlotItem> slots)
    {
        SSOData data = new SSOData();
        data.img = imgName;
        data.slots = slots;
        data.total_slots = slots.Count;
        data.occupied_count = slots.FindAll(x => x.occupied).Count;

        m_Datas.Add(data);
    }

    public void Remove(SSOData data)
    {
        m_Datas.Remove(data);
    }

    public void Clear()
    {
        m_Datas.Clear();
    }
}