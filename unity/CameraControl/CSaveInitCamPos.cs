using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using System;
using System.IO;
using Newtonsoft.Json;
using static SPtz;

public class CSaveInitCampPos 
{
    //[Serializable]
    //public class SVPtz
    //{
    //    public float p = 0.0f;     // Pan 각도
    //    public float t = 0.0f;    // Tilt 각도
    //    public float z = 1.0f;    // Zoom 레벨
    //    public SVPtz()
    //    {
    //    }
    //    public SVPtz(float _pan, float _tilt, float _zoom)
    //    {
    //        p = _pan;
    //        t = _tilt;
    //        z = _zoom;
    //    }

    //    public void Set(float _pan, float _tilt, float _zoom)
    //    {
    //        p = _pan;
    //        t = _tilt;
    //        z = _zoom;
    //    }
    //}
    // 카메라 뷰 방향 정보 (프리셋 개수, 순서가 동일하다.)
    [Serializable]
    public class SCamDir
    {
        public int idx = 0;                     // 프리셋 id와 동일하고 1부터 시작한다. ( 0 : 설정이 안됨 )   
        public string sname = "";               // 카메라 뷰 방향 이름
        public int cam_id = 1;                  // 카메라 아이디 ( 1 부터 시작, 1개 있을때 기본으로 1 설정 )
        public int preset_id = 1;               // 프리셋 아이디 ( 1 부터 시작 )
        public SVector3 pos = new SVector3();   // 카메라 오브젝트의 위치
        public SVector3 rot = new SVector3();   // 카메라 오브젝트의 회전 ( Euler Angles )
        
        public float pan = 0.0f;                // 카메라 Pan 각도 ( 회전 y 값과 동일하다. )
        public float tilt = 0.0f;               // 카메라 Tilt 각도 ( 회전 x 값과 동일하다. )
        public float zoom = 0.0f;               // 카메라 줌 레벨 ( 오브젝트 위치 z 값과는 다르다. camera의 fov 변환값 : 1 ~ 36배줌 )

        public SPtz ptzmin = new SPtz(-180.0f, -90.0f, 1.0f);   // 최소 Pan, Tilt, Zoom 값
        public SPtz ptzmax = new SPtz(180.0f, 90.0f, 36.0f);    // 최대 Pan, Tilt, Zoom 값

        public Vector3 Pos()           { return pos.Get();   }
        public void Pos(Vector3 v)     {  pos.Set(v);    }

        public Vector3 Rot() { return rot.Get(); }
        public void Rot(Vector3 v) { 
            rot.Set(v); 
            pan = rot.y;   // 회전 y 값과 동일하다.
            tilt = rot.x;  // 회전 x 값과 동일하다.
        }

        public float Pan() { 
            return rot.y;      
        }
        public float Tilt() { 
            return rot.x;       
        }

        public SCamDir(string _name = "")
        {
            sname = _name;
            pos.y = 5.0f;   // 기본 위치는 y=5.0f
        }
    }

    [Serializable]
    public class SCameraPos
    {
        public float target_pos = 0.0f;   // 타겟 위치
        public List<SCamDir> datas = new List<SCamDir>();   // 카메라 뷰 방향 정보 리스트 (프리셋 리스트와 갯수 동일)

        public int GetCount()
        {
            return datas.Count;
        }
        public void Add(SCamDir data)
        {
            datas.Add(data);
        }

        public SCamDir Get(int idx)
        {
            if (idx >= 0 && idx < datas.Count)
                return datas[idx];
            return null;
        }

        public SCamDir FindByPresetId(int preset_id)
        {
            SCamDir kCamDir = datas.Find(x => x.preset_id == preset_id);
            return kCamDir;
            //for (int i = 0; i < datas.Count; i++)
            //{
            //    if (datas[i].preset_id == preset_id)
            //        return datas[i];
            //}
            //return null;
        }

        public void ResetPanTilt()
        {
            for( int i = 0; i < datas.Count; i++)
            {
                datas[i].pan = datas[i].Pan();   // 회전 y 값과 동일하다.
                datas[i].tilt = datas[i].Tilt();  // 회전 x 값과 동일하다.
            }
        }
    }

    public class SCameraPosList
    {
        public List<SCameraPos> datas = new List<SCameraPos>();   // 카메라 뷰 방향 정보 리스트 (프리셋 리스트와 갯수 동일)
        public int GetCount()
        {
            return datas.Count;
        }
        public void Add(SCameraPos data)
        {
            datas.Add(data);
        }
        public SCameraPos Get(int idx)
        {
            if (idx >= 0 && idx < datas.Count)
                return datas[idx];
            return null;
        }

        public void ResetPanTilt()
        {
            for (int i = 0; i < datas.Count; i++)
            {
                datas[i].ResetPanTilt();
            }
        }
    }

    //-------------------------------------------------------------
    /// <summary>
    /// 카메라 위치 정보리스트 ( 카메라 갯수와 동일 )
    /// </summary>
    public SCameraPosList m_CamPosList = new SCameraPosList();

    /// <summary>
    /// 카메라 인덱스 기준으로 SCameraPos를 반환한다. 없으면 생성한다.
    /// </summary>
    public SCameraPos GetCameraPos(int camIdx = 1)
    {
        int idx = camIdx - 1;
        while (m_CamPosList.GetCount() <= idx)
            m_CamPosList.Add(new SCameraPos());
        return m_CamPosList.Get(idx);
    }

    public void Add(int camId, SCamDir kCamDir)
    {
        SCameraPos camPos = m_CamPosList.Get(camId - 1);
        if (camPos == null)
        {
            camPos = new SCameraPos();
            camPos.target_pos = 0.0f;   // 타겟 위치
            m_CamPosList.Add(camPos);
        }
        camPos.Add(kCamDir);
    }

    public int Count()
    {
        return m_CamPosList.datas.Count;
    }
    public void Add(SCamDir data)
    {
        GetCameraPos().Add(data);
    }
    public void Clear()
    {
        m_CamPosList.datas.Clear();
    }

    /// <summary>
    /// 카메라 인덱스에 해당하는 SCameraPos 슬롯을 제거한다.
    /// </summary>
    public void RemoveCameraPos(int camIdx)
    {
        if (camIdx >= 0 && camIdx < m_CamPosList.datas.Count)
            m_CamPosList.datas.RemoveAt(camIdx);
    }

    public bool SaveToJson(string path, string fileName)
    {
        string sPathName = fileName;
         if ( path != "" )
            sPathName = Path.Combine(path, fileName);

        string sringData = JsonConvert.SerializeObject(m_CamPosList);

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
    /// <summary>
    /// 카메라 위치 정보 리스트를 JSON 파일에서 로드한다.
    /// </summary>
    /// <param name="path"></param>
    /// <param name="fileName"></param>
    /// <returns></returns>
    public bool LoadFromJson(string path, string fileName)
    {
        Clear();

        string sPathName = Path.Combine(path, fileName);

        try
        {
            string stringData = File.ReadAllText(sPathName);
            m_CamPosList = JsonConvert.DeserializeObject<SCameraPosList>(stringData);
        }
        catch (Exception e)
        {
            Debug.Log(e);
            return false;
        }
        return true;
    }







}


