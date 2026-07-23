using UnityEngine;


public class CObjPole : CObjBase
{
    public Transform m_trPoleBody = null;    // 폴대 오브젝트
    public Transform m_trCamPos = null;      // 카메라 위치 오브젝트

    void Start()
    {
        m_InitScale = m_trPoleBody.localScale;
    }

    public void Initialize(int idx, string nameId)
    {
        m_Idx = idx;
        m_NameId = nameId;
        m_ObjType = EObjectType.Pole;
    }

    public void SetPoleHeight(float height)
    {
        Vector3 scale = m_trPoleBody.localScale;
        scale.y = height/2;     // 폴대의 높이는 Y축 스케일의 2배가 된다.
        m_trPoleBody.localScale = scale;

        Vector3 pos = m_trPoleBody.localPosition;
        pos.y = height / 2;     // 폴대의 중심점이 바닥에서 높이의 절반 지점에 위치해야 한다.
        m_trPoleBody.localPosition = pos;
    }

    public void SetAttachedCamera(CBaseCamera kCam)
    {
        if (kCam == null)
            return;
        m_ObjType = EObjectType.Pole;
        
        m_trCamPos = kCam.transform;
    }
}


public enum EObjectType
{
    None = -1,
    Car = 1,
    Pole = 2,
}


public class CObjBase : MonoBehaviour
{
    public int m_Idx = -1;                              // 주차면 인덱스와 동일하지 않다. ( -1: 없음 )
    public string m_NameId = "";                        // 구분할수 있는 이름 id
    public EObjectType m_ObjType = EObjectType.None;    // 오브젝트 타입 1: 차량, 2: 폴대
    public Vector3 m_InitScale = Vector3.one;           // 기존 스케일값을 보관한다.   // 주로 Y 값만 확대/축소 한다.
    

    

    void Start()
    {
        m_InitScale = transform.localScale;
    }
    // 오브젝트를 확대/축소 한다.
    public void SetScaling(Vector3 scale)
    {
        transform.localScale = scale;
    }

    public bool IsShow()
    {
        return gameObject.activeSelf;
    }

    public void Show(bool bShow)
    {
        gameObject.SetActive(bShow);
    }
}