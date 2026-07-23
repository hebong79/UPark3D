using System;
using System.IO;
using UnityEngine;

/*
 * [ Preset Maker UI 관리자 ]
 * 
 *  주차면 폭
 *    - 일반 주차장                 : 2500 * 5000
 *    - 평행 주차                   : 2500 * 6000
 *    - 보차도 구분이 없는 주거지역 : 2000 * 5000 
 * 
 * 
 */

public class CPMakerGameUI : MonoBehaviour
{
    public CPMakerParkSpaceUI m_ParkSpaceUI = null;
    public CCarObjListUI m_CarObjListUI = null;
    //public CMapSizeUI m_MapSizeUI = null;
    public CPCamObjListUI m_CamObjListUI = null;
    public CPoleObjListUI m_PoleObjListUI = null;

    public CRainShower m_RainShower = null; // 비 시뮬레이터
    public CSnowShower m_SnowShower = null; // 눈 시뮬레이터
    public bool m_UseDrawQubeLine = true;
    public bool m_UseSameMainCamera  = false;

    // Start is called once before the first execution of Update after the MonoBehaviour is created
    void Start()
    {

    }


    public void Initialize()
    {
        //m_MapSizeUI.Initialize();
        m_CamObjListUI.Initialize((bLoadComplete, kSelectedCamObj) =>
        {
            if (bLoadComplete && kSelectedCamObj != null && kSelectedCamObj.m_PCamera != null)
            {
                if (m_RainShower != null)
                    m_RainShower.m_Camera = kSelectedCamObj.getCamera;

                if (m_SnowShower != null)
                    m_SnowShower.m_Camera = kSelectedCamObj.getCamera;
            }
        });

        m_CamObjListUI.m_CurCamViewerUI?.Show(false);
    }

    public void SetToMainCamera(Camera kCamera)
    {
        if (kCamera != null)
        {
            Camera.main.transform.position = kCamera.transform.position;
            Camera.main.transform.rotation = kCamera.transform.rotation;
            Camera.main.fieldOfView = kCamera.fieldOfView;
        }
    }

    //화면 스크린샷 저장
    public virtual void SaveFile_ScreenShot(string fileName = "")
    {
        Camera kCamera = m_CamObjListUI.m_SelectedCamObj.getCamera;
        if (kCamera != null)
        {
            Texture2D kTexture = CImageHelper.CaptureImage(kCamera);
            string dir = "Save/CamCapture";
            if (!Directory.Exists(dir))
                Directory.CreateDirectory(dir);

            if (string.IsNullOrEmpty(fileName))
                fileName = $"CamCapture_{DateTime.Now:yyyyMMdd_HHmmss}.jpg";

            string outPath = Path.Combine(dir, fileName);
            CImageHelper.SaveTextureToFile(kTexture, outPath, true, 90);
        }
    }

    // Update is called once per frame
    void Update()
    {

        if (Input.GetKey(KeyCode.LeftControl))
        {
            // 큐브 라인 표시 여부 토글
            if (Input.GetKeyDown(KeyCode.L))
            {
                m_UseDrawQubeLine = !m_UseDrawQubeLine;
                m_ParkSpaceUI.ShowQubeLineList(m_UseDrawQubeLine);
            }

            if ( Input.GetKey(KeyCode.LeftShift))
            {
                // 메인 카메라와 동일한 뷰어 토글
                if (Input.GetKeyDown(KeyCode.F8))
                {
                    m_UseSameMainCamera = !m_UseSameMainCamera;
                    SetToMainCamera(m_CamObjListUI.GetCurObjCamera().getCamera);
                }

                // 카메라 뷰어 텍스쳐 저장 ( 스크린샷 )
                if (Input.GetKeyDown(KeyCode.Alpha9))
                {
                    SaveFile_ScreenShot();
                }

                // 카메라 뷰어 크기 변경
                if (Input.GetKeyDown(KeyCode.Alpha1))
                {
                    m_CamObjListUI.m_CurCamViewerUI.NextViewSize();
                }
                // 카메라 뷰어 토글
                if (Input.GetKeyDown(KeyCode.Alpha2))
                {
                    bool bShow = m_CamObjListUI.m_CurCamViewerUI.IsShow();
                    m_CamObjListUI.m_CurCamViewerUI.Show(!bShow);
                }
            }
        }
        // 카메라 뷰어(랜더타겟) 토글
        if (Input.GetKeyDown(KeyCode.F9))
        {
            if (m_CamObjListUI.m_CurCamViewerUI != null)
            {
                bool bShow = m_CamObjListUI.m_CurCamViewerUI.IsShow();
                m_CamObjListUI.m_CurCamViewerUI.Show(!bShow);
            }
        }

        if (m_UseSameMainCamera)
        {
            SetToMainCamera(m_CamObjListUI.GetCurObjCamera().getCamera);
        }



    }


}
