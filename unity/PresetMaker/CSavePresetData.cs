using System;
using System.Collections.Generic;
using System.IO;
using UnityEngine;
using Newtonsoft.Json;


/// <summary>
/// 주차면 번호 할당 결과를 담는 클래스.
/// 카메라 기준으로 프리셋별 주차면 번호가 순차 할당된 결과를 표현한다.
/// </summary>
[Serializable]
public class SPSpaceAssignment
{
    public int camIdx;          // 카메라 인덱스 ( 1 부터 시작  )
    public int presetIdx;       // 프리셋 인덱스 ( 1 부터 시작  )
    public int startFaceNum;    // 시작 주차면 번호 (1-based)
    public int faceCount;       // 주차면 갯수
    public List<int> faceNumbers = new List<int>(); // 할당된 주차면 번호 리스트

    public int EndFaceNum => startFaceNum + faceCount - 1;  // 마지막 주차면 번호

    public SPSpaceAssignment() { }

    public SPSpaceAssignment(int cam, int preset, int start, int count)
    {
        camIdx      = cam;
        presetIdx   = preset;
        startFaceNum = start;
        faceCount   = count;
        faceNumbers = new List<int>(count);
        for (int i = 0; i < count; i++)
            faceNumbers.Add(start + i);
    }


    public int GeSlotNumerByFullIdx(int _camIdx, int _presetIdx, int localSlotIdx)
    {
        if (_camIdx == camIdx && _presetIdx == presetIdx && localSlotIdx > 0 && localSlotIdx <= faceNumbers.Count)
        {
            return faceNumbers[localSlotIdx - 1];
        }
        return -1; // 유효하지 않은 경우
    }

    public int GeSlotNumerByFullIdx( int localSlotIdx)
    {
        if (localSlotIdx > 0 && localSlotIdx <  faceNumbers.Count)
        {
            return faceNumbers[localSlotIdx-1];
        }
        return -1; // 유효하지 않은 경우
    }
}


[Serializable]
public class SDPresetInfo
{
    public int idx = 0;                 // 프리셋 인덱스 ( 1부터 시작, 0 : 설정이 안됨) 
    public string presetName = "";      // 프리셋 이름
    public int faceCount = 0;           // 이 그룹이 가지고 있는 주차면 갯수
    public SVector3 offsetPos = new SVector3();       // 주차면 시작 위치
    public float faceRot = 0;                   // 주차면 회전값
    public float groupRot = 0;                 // 프리셋 주차면 회전값 ( 주차면 회전값과 별도로 프리셋 주차면을 회전할 때 사용한다. )                                                
    public float xSize = 0;                     // 2.5f;   // 박스 x 크기    (  일맞 주차장 - 2500 * 5000 ) 
    public float zSize = 0;                     // 5.0f;   // 박스 z 크기
    public int dirType = 0;                     // 시작점 부터 박스를 그리기 위한 이동 방향 타입 ( EFaceDirType )
    public bool useBaseWidth = true;            // xSize가 주차면의 폭으로 사용할지 여부 ( true : xSize를 폭으로 사용, false : zSize를 폭으로 사용 )
    public int camIdx = 1;                      // 카메라 인덱스( 기본 1 으로 사용한다. )

    public SDPresetInfo(int id, int _faceCount, Vector3 vOffsetPos, float _faceRot, float _groupRot, int _dirType, int _camIdx, bool _useBaseWidth=true, float _xSize = 2.5f, float _zSize = 5.0f)
    {
        idx = id;
        faceCount = _faceCount;
        offsetPos.Set(vOffsetPos);
        faceRot = _faceRot;
        groupRot = _groupRot;
        useBaseWidth = _useBaseWidth;
        xSize = _xSize;
        zSize = _zSize;
        dirType = _dirType;
        camIdx = _camIdx;
    }
    public SDPresetInfo()
    {
    }

    // FOV가 수평으로 맞춰 있으므로 수직FOV로 변경해서 사용해야 한다. 
    //public float RealFOV()
    //{
    //    return fov * 9.0f / 16;
    //}
}


[Serializable]
public class SDPresetDatas
{
    public List<SDPresetInfo> datas = new List<SDPresetInfo>();
}

/// <summary>
/// 프리셋 정보를 저장 관리 한다.
/// </summary>
public class CSavePresetData 
{
    public SDPresetDatas m_Datas = new SDPresetDatas();   // 실제 저장 데이터 리스트 (파일저장 데이터)

    public List<SPSpaceAssignment> m_FaceNoList = new List<SPSpaceAssignment>();  //(파일저장 데이터 아님-풀주차번호 사용) 카메라 기준으로 프리셋별 주차면 번호 할당 결과 리스트

    public int Count()
    {
        return m_Datas.datas.Count;
    }
    public void Add(SDPresetInfo data)
    {
        m_Datas.datas.Add(data);
    }
    public void Clear()
    {
        m_Datas.datas.Clear();
    }
    public void Remove(int idx)
    {
        SDPresetInfo kInfo = m_Datas.datas.Find(x=>x.idx == idx);
        if (kInfo != null)
        {
            m_Datas.datas.Remove(kInfo);
        }
        //m_Datas.datas.RemoveAt(idx);
    }

    public void RemoveAll()
    {
        m_Datas.datas.Clear();
    }

    public SDPresetInfo GetPresetInfo(int idx)
    {
        SDPresetInfo kInfo = m_Datas.datas.Find(x => x.idx == idx);
        //if( idx >= 0 &&  idx < m_Datas.datas.Count )
        //    return m_Datas.datas[idx];
        
        return kInfo;
    }

    public bool IsExistPresetIdx(int idx)
    {
        SDPresetInfo kInfo = m_Datas.datas.Find(x => x.idx == idx);
        if( kInfo != null )
            return true;

        return false;
    }


    public bool SaveToJson(string path, string fileName)
    {
        if (!Directory.Exists(path))
        {
            Directory.CreateDirectory(path);
        }
        string sPathName = Path.Combine(path, fileName);
        string sringData = JsonConvert.SerializeObject(m_Datas);
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

    public string MakeDataToString()
    {
        string str = "";
        str = JsonUtility.ToJson(m_Datas);
        Debug.Log(str);

        return str;
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
            //m_Datas = JsonUtility.FromJson<SPresetDatas>(stringData);
            m_Datas = JsonConvert.DeserializeObject<SDPresetDatas>(stringData);
        }
        catch (Exception e)
        {
            Debug.Log(e);
            return false;
        }
        return true;
    }

    /// <summary>
    /// 카메라 기준으로 프리셋별 주차면 번호를 순차 할당하여 결과를 반환한다.
    /// 우선순위: 1. 카메라 인덱스(camIdx) 오름차순, 2. 프리셋 인덱스(idx) 오름차순, 3. 주차면 순서
    /// MonoBehaviour 의존성 없이 순수 데이터 처리만 수행하므로 유닛 테스트가 가능하다.
    /// </summary>
    /// <param name="presets">계산에 사용할 프리셋 목록</param>
    /// <returns>카메라/프리셋 순으로 정렬된 주차면 번호 할당 결과 목록</returns>
    public static List<SPSpaceAssignment> CalculateParkingSpaceAssignments(List<SDPresetInfo> presets)
    {
        var assignments = new List<SPSpaceAssignment>();
        if (presets == null || presets.Count == 0)
            return assignments;

        // 1순위: camIdx 오름차순 / 2순위: preset idx 오름차순
        var sorted = new List<SDPresetInfo>(presets);
        sorted.Sort((a, b) =>
        {
            if (a.camIdx != b.camIdx)
                return a.camIdx.CompareTo(b.camIdx);
            return a.idx.CompareTo(b.idx);
        });

        // 주차면 번호를 1부터 순차 할당
        int currentFaceNum = 1;
        foreach (var preset in sorted)
        {
            var assignment = new SPSpaceAssignment( preset.camIdx, preset.idx, currentFaceNum, preset.faceCount);
            assignments.Add(assignment);
            currentFaceNum += preset.faceCount;
        }

        return assignments;
    }

    // 현재 m_Datas.datas 를 기반으로 카메라/프리셋 순으로 주차면 번호를 재할당하여 m_FaceNoList 를 갱신한다.
    public List<SPSpaceAssignment> ResetFullFaceNoList()
    {
        m_FaceNoList.Clear();
        m_FaceNoList = CalculateParkingSpaceAssignments(m_Datas.datas);
        return m_FaceNoList;
    }

}

