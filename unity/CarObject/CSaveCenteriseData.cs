using System;
using System.Collections.Generic;
using UnityEngine;
using Newtonsoft.Json;
//using JetBrains.Annotations;



[Serializable]
public class SCenteriseItem
{
    public int target_slot = -1;       // 타겟 주차면 아이디 ( 주차면 아이디 -> 1 부터 시작 )
    public float pan = 0f;      // Pan
    public float tilt = 0f;     // Tilt
    public float zoom = 0f;     // Zoom
    //public bool is_front = true;   // 주차면에서 차량이 보이는 방향 ( true: 전면, false: 후면 )
    public string img = "";     // 이미지 경로및 이름
    public int check = 0;       // 체크포인트 여부 ( 0: none 1: 체크포인트 ), target_slot으로 완전히 이동했을때 1로 변경

    public SCenteriseItem() {
    }
    //public SCenteriseItem(int cameId, int _presetId, int _slotId, float _pen, float _tilt, float _zoom, string _img)
    public SCenteriseItem(int _slotId, float _pan, float _tilt, float _zoom, string _img, int _check)
    {
        //cam_id = cameId;
        //preset_id = _presetId;
        target_slot = _slotId;
        pan = _pan;
        tilt = _tilt;
        zoom = _zoom;
        //is_front = _isfront;
        img = _img;
        this.check = _check;
    }

    public void SetPTZ(float _pan, float _tilt, float _zoom)
    {
        pan = _pan;
        tilt = _tilt;
        zoom = _zoom;
    }
}

[Serializable]
public class SCenterPresetData
{
    //public int preset_id = -1;      // 프리셋 아이디 ( 프리셋 아이디 -> 1 부터 시작 )
    //public int cam_id = 1;          // 카메라 아이디 ( 1 부터 시작 )  
    public float pan = 0f;
    public float tilt = 0f;
    public float zoom = 0f;
    public string img = "";

    public List<SCenteriseItem> datas = new List<SCenteriseItem>();

    public void Add(SCenteriseItem item)
    {
        //SCenteriseItem data = new SCenteriseItem();
        datas.Add(item);
    }

    //public int CameraId() { return cam_id;    }
}



/// <summary>
/// 카메라 센터라이징 데이터   
/// </summary>

public class CSaveCenteriseData 
{
    public List<SCenterPresetData> m_Datas = new List<SCenterPresetData>();

    /// <summary>
    /// jsonl 형식으로 여러 데이터 저장
    /// </summary>
    /// <param name="pathName">저장 파일 경로</param>
    /// <param name="appendMode">true: 기존 파일에 추가(append), false: 덮어쓰기(overwrite)</param>
    public void SaveFile_Jsonl(string pathName, bool appendMode = false)
    {
        try
        {
            // ✅ [메모리 절약] StringBuilder 사용으로 string 연결 GC 부하 감소
            System.Text.StringBuilder sb = new System.Text.StringBuilder();
            foreach (var data in m_Datas)
            {
                string json = JsonConvert.SerializeObject(data);
                sb.Append(json).Append('\n');
            }

            // ✅ [메모리 절약] appendMode 지원: 에피소드마다 flush 후 append 가능
            if (appendMode)
                System.IO.File.AppendAllText(pathName, sb.ToString());
            else
                System.IO.File.WriteAllText(pathName, sb.ToString());
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
                    var data = JsonConvert.DeserializeObject<SCenterPresetData>(line);
                    m_Datas.Add(data);
                }
            }
        }
        catch (Exception e)
        {
            Debug.LogError(e.Message);
        }
    }

    /// <summary>SCenterPresetData 1건을 jsonl 한 줄로 파일 끝에 추가한다.</summary>
    public static void AppendJsonl(string pathName, SCenterPresetData data)
    {
        try
        {
            string json = JsonConvert.SerializeObject(data);
            System.IO.File.AppendAllText(pathName, json + "\n");
        }
        catch (Exception e)
        {
            Debug.LogError(e.Message);
        }
    }

    public static void SaveJson(string pathName, SCenterPresetData data)
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

    public static SCenterPresetData LoadJson(string pathName)
    {
        var data = new SCenterPresetData();
        try
        {
            if (System.IO.File.Exists(pathName))
            {
                string json = System.IO.File.ReadAllText(pathName);
                data = JsonConvert.DeserializeObject<SCenterPresetData>(json);
                return data;
            }
        }
        catch (Exception e)
        {
            Debug.LogError(e.Message);
        }
        return null;
    }

    public void Add(SCenterPresetData data)
    {
        m_Datas.Add(data);
    }

    public void Add(List<SCenteriseItem> slots)
    {
        SCenterPresetData data = new SCenterPresetData();
        foreach (var slot in slots)
        {
            data.Add(slot);
        }
        m_Datas.Add(data);
    }

    public void Remove(SCenterPresetData data)
    {
        m_Datas.Remove(data);
    }

    public void Clear()
    {
        m_Datas.Clear();
    }
}

