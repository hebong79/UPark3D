using System;
using System.Collections;
using System.Collections.Generic;
using UnityEngine;


[Serializable]
public class SConfigFile
{
    public int m_Idx = 0;
    public string m_PlaceName;
    public string m_FaceGroupFile;
    public string m_CameraPosFile;
}


//[CreateAssetMenu(fileName = "ConfigFileList", menuName = "ScriptableObjects/ConfigFiles", order = 1)]
//public class CSOConfigFile : ScriptableObject
//{
//    public List<SConfigFile> m_Datas = new List<SConfigFile>();


//    public string GetFaceGroupFileName(int idx)
//    {
//        if (idx >= 0 && idx < m_Datas.Count)
//            return m_Datas[idx].m_FaceGroupFile;

//        return "";
//    }

//    public SConfigFile GetConfigFile(int idx)
//    {
//        if (idx >= 0 && idx < m_Datas.Count)
//            return m_Datas[idx];

//        return null;
//    }
//}



//public class CConfigFileMgr
//{
//    static CConfigFileMgr _inst = null;
//    public static CConfigFileMgr Inst {
//        get {
//            if( _inst == null)
//                _inst = new CConfigFileMgr();
//            return _inst;
//        }
//    }

//    public CSOConfigFile m_ConfigFile = null;
//    public int m_SelectSaveIndex = 0;

//    public void Initialize(CSOConfigFile kFile, int nSelectIndex)
//    {
//        m_ConfigFile = kFile;
//        m_SelectSaveIndex = nSelectIndex;
//    }


//    public string GetFaceGroupFileName()
//    {
//        return m_ConfigFile.GetFaceGroupFileName(m_SelectSaveIndex);
//    }

//    public SConfigFile GetConfigFile()
//    {
//        return m_ConfigFile.GetConfigFile(m_SelectSaveIndex);
//    }




//}
