using System.Collections;
using System.Collections.Generic;
using TMPro;
using UnityEngine;

/// <summary>
/// 차량 오브젝트
/// 차량 오브젝트에 번호판 이미지를 매핑하자 ( 별도 오브젝트를 붙여 메터리얼을 사용하자 )
/// </summary>
public class CObjCar : CObjBase
{
    public GameObject m_FrontPlateNo;       // 앞쪽 번호판
    public GameObject m_BackPlateNo;        // 뒤쪽 번호판

    public TMP_Text m_FrontNumber;
    public TMP_Text m_BackNumber;
    public Camera m_Camera = null;

    public int m_iPresetId = 0;         // 프리셋 아이디 ( 1부터 시작 ) 
    public int m_iFaceSlot = -1;         // 주차면 슬롯 인덱스 (-1: 없음), (1부터 시작)
    public string m_VehicleType = "car"; // 차량 타입 문자열 ( car, truck, bus ... )

    public bool m_UseRandomColor = false; // 랜덤 색상 사용 여부 ( 초기화 시 적용 )
    public bool m_IsFront = true;       // 앞면이 보이게 주차함.

    private CVehicleColorController m_ColorController;
    [HideInInspector] public CCarSelectHighlight m_SelectHighlight;

    // Start is called before the first frame update
    void Start()
    {
        if (m_Camera == null)
            m_Camera = Camera.main;

        m_InitScale = transform.localScale;

        // 색상 컨트롤러 캐싱
        m_ColorController = GetComponent<CVehicleColorController>();
        if (m_ColorController == null){
            m_ColorController = gameObject.AddComponent<CVehicleColorController>();
        }

        // 선택 하이라이트 컴포넌트 자동 추가
        m_SelectHighlight = GetComponent<CCarSelectHighlight>();
        if (m_SelectHighlight == null)
            m_SelectHighlight = gameObject.AddComponent<CCarSelectHighlight>();

        if(m_UseRandomColor)
            m_ColorController.SetRandomColor();
    }

    /// <summary>
    /// idx-생성된 차량 순서, iFaceSlot-주차면 슬롯 인덱스
    /// </summary>
    public void Initialize(int idx, string nameId, int presetId, Camera kCamera, int iFaceSlot=-1, bool bUseRandomColor = false)
    {
        // ✅ 명시적 초기화
        m_Idx = idx;
        m_NameId = nameId;
        m_ObjType = EObjectType.Car;
        m_iFaceSlot = iFaceSlot;
        m_iPresetId = presetId;

        // ✅ 카메라 설정
        SetCamera(kCamera);

        string sPlateNum = CMakerPlateNumber.GenerateRandomPlate();
        // ✅ 상태 리셋 (풀링 재사용 시 이전 데이터 제거)
        if (m_FrontNumber != null) m_FrontNumber.text = sPlateNum;
        if (m_BackNumber != null) m_BackNumber.text = sPlateNum;
        
        // ✅ 색상 초기화 (랜덤 색상 적용)
        if (m_ColorController == null)
            m_ColorController = GetComponent<CVehicleColorController>();
            
        if (m_ColorController != null)
        {
            if(bUseRandomColor)   
                m_ColorController.SetRandomColor();
        }

        CLogger.Inst.Debug($"차량 초기화: idx={idx}, id={nameId}", "Car");
    }

    public void Show(bool bShow)
    {
        gameObject.SetActive(bShow);
    }

    /// <summary>
    /// 차량 색상을 직접 설정합니다.
    /// </summary>
    public void SetVehicleColor(Color color)
    {
        if (m_ColorController != null)
        {
            m_ColorController.SetColor(color);
        }
    }

    public void SetCamera(Camera kCam)
    {
        m_Camera = kCam;
        
        if(m_FrontNumber == null || m_BackNumber == null)
            return;

        Canvas kCanvasF = m_FrontNumber.transform.parent.GetComponent<Canvas>();
        Canvas kCanvasB = m_BackNumber.transform.parent.GetComponent<Canvas>();
        if (kCanvasF != null && kCanvasB != null)
        {
            kCanvasF.worldCamera = m_Camera;
            kCanvasB.worldCamera = m_Camera;
        }
    }

}
