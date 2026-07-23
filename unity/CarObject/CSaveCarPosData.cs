using System;
using System.Collections.Generic;
using System.IO;
using Newtonsoft.Json;
using UnityEngine;

public enum ECarType
{
    None = 0,
    Small = 1,  // 소형       폭: 1.595m, 길이:3.595m, 높이:1.485 (모닝)
    Medium,     // 중형       폭: 1.835m, 길이:4.82m,  높이:1.470 (소나타, k5, k3, 아벤테) 
    Large,      // 대형       폭: 1.905m, 길이:5.05m,  높이:1.465 (그랜저, 제너시스)
    Suv,        // suv        폭: 1.90m, 길이    :4.91m, 높이:1.74  (쏘렌토, 산타페, 투싼) 
    Bongo,      // 승합차1    폭: 1.985m, 길이    :5.115m, 높이:1.74  (카니발, 펠리세이드)  or 봉고차(스타렉스, 등)   
    Truck,       // 트럭(탑차) 폭: 1.80m,  길이:5.05m,  높이:2.305 (미니밴-르노마스터 밴 S), 트럭(우편배달 트럭) 
    Max = 6
}

public enum ECarColor
{
    White = 0,
    Black,
    Silver,
    Gray,
    Red,
    Blue,
    Green,
    Yellow,
    Orange,
    Purple
}

/// <summary>
/// 차량 위치 정보를 저장 관리 한다.
/// </summary>
/// 
public class CSaveCarPosData : CSaveObjectPosData
{
    public static readonly Color COLOR_SILVER = new Color(0.75f, 0.75f, 0.75f);
    public static readonly Color COLOR_ORANGE = new Color(1.0f, 0.5f, 0.0f);
    public static readonly Color COLOR_PURPLE = new Color(0.5f, 0.0f, 0.5f);

    public static Color GetRandomColor()
    {
        Array colors = Enum.GetValues(typeof(ECarColor));
        ECarColor randomColor = (ECarColor)colors.GetValue(UnityEngine.Random.Range(0, colors.Length));
        return GetColor(randomColor);
    }

    public static Color GetColor(ECarColor eColor)
    {
        switch (eColor)
        {
            case ECarColor.White:  return Color.white;
            case ECarColor.Black:  return Color.black;
            case ECarColor.Silver: return COLOR_SILVER;
            case ECarColor.Gray:   return Color.gray;
            case ECarColor.Red:    return Color.red;
            case ECarColor.Blue:   return Color.blue;
            case ECarColor.Green:  return Color.green;
            case ECarColor.Yellow: return Color.yellow;
            case ECarColor.Orange: return COLOR_ORANGE;
            case ECarColor.Purple: return COLOR_PURPLE;
            default:               return Color.white; // 안전한 기본값 (검정 방지)
        }
    }


    public static string GetCarTypeName(int carType)
    {
        switch ((ECarType)carType)
        {
            case ECarType.Small:  return "소형차";
            case ECarType.Medium: return "중형차";
            case ECarType.Large:  return "대형차";
            case ECarType.Suv:    return "SUV";
            case ECarType.Bongo:  return "봉고차";
            case ECarType.Truck:  return "트럭";
        }
        return "중형차";
    }
}

//public class CSaveCarPosData
//{
//    public class SCarPos
//    {
//        public string id = "";                  // 차량obj id(시간으로 된 구분용)
//        public int slotId = 0;                  // 주차장의 주차면 id                                              
//        public int prefabId = 1;                // 차량오브젝트(프리팹) id
//        public SVector3 pos = new SVector3();   // 위치값
//        public float rotY = 0;                  // y축 회전값
//    }


//    public class SCarPosDatas
//    {
//        public List<SCarPos> datas = new List<SCarPos>();   
//    }


//    public SCarPosDatas m_Data = new SCarPosDatas();


//    public void Add(SCarPos data)
//    {
//        m_Data.datas.Add(data);
//    }

//    public void Remove(SCarPos data)
//    {
//        m_Data.datas.Remove(data);
//    }

//    public void RemoveById(string id)
//    {
//        //SCarPos removeData = null;

//        SCarPos removeData = m_Data.datas.Find(x => x.id == id);

//        //foreach (var data in m_Data.datas)
//        //{
//        //    if (data.id == id)
//        //    {
//        //        removeData = data;
//        //        break;
//        //    }
//        //}
//        if (removeData != null)
//        {
//            m_Data.datas.Remove(removeData);
//        }
//    }


//    public SCarPos GetCarPosData(int idx)
//    {
//        if (idx >= 0 && idx < m_Data.datas.Count) {
//            return m_Data.datas[idx];
//        }
//        return null;
//    }
   

//    public bool SaveToJson(string path, string fileName)
//    {
//        if (!Directory.Exists(path))
//        {
//            Directory.CreateDirectory(path);
//        }
//        string sPathName = Path.Combine(path, fileName);
//        string sringData = JsonConvert.SerializeObject(m_Data);
//        //string sringData = MakeDataToString();

//        try
//        {
//            File.WriteAllText(sPathName, sringData);
//            return true;
//        }
//        catch (Exception e)
//        {
//            Debug.Log(e);
//        }
//        return false;
//    }

//    public bool LoadFromJson(string path, string fileName)
//    {
//        Clear();

//        string sPathName = fileName;
//        if( path != string.Empty )
//            sPathName = Path.Combine(path, fileName);

//        try
//        {
//            string stringData = File.ReadAllText(sPathName);
//            m_Data = JsonConvert.DeserializeObject<SCarPosDatas>(stringData);
//        }
//        catch (Exception e)
//        {
//            Debug.Log(e);
//            return false;
//        }
//        return true;
//    }


//    public void Clear()
//    {
//        m_Data.datas.Clear();
//    }

//}
