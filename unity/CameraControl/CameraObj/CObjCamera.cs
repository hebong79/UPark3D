using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Rendering;


/// <summary>
/// 카메라와 폴대 오브젝트까지 관리한다.
/// </summary>
public class CObjCamera : MonoBehaviour
{
    public const float DEFAULT_CAM_HEIGHT = 5.0f;   // 카메라 기본 높이  
    //public int m_Idx = -1;                          // 1부터 시작
    //public string m_NameId = "";                    // 구분할수 있는 이름 id
    public Vector3 m_InitScale = Vector3.one;       // 기존 스케일값을 보관한다.   // 주로 Y 값만 확대/축소 한다.
    public CBaseCamera.ECameraType m_CameraType = CBaseCamera.ECameraType.PTZ;     // PTZ or Static 카메라 구분

    public CBaseCamera m_PCamera = null;     // PTZ or Static camera
    public Transform m_trCamPos = null;      // 카메라 위치 오브젝트
    public GameObject m_goPole = null;       // 폴대 오브젝트 (x, z 만 사용 ) - (카메라 위치 오브젝트의 자식으로 존재한다.)

    [Space]
    public RenderTexture m_TargetRenderTexture = null; // 렌더타겟용 카메라일경우 셋팅되는 렌더타겟

    public float m_FOV = 15.0f;          // 카메라 FOV (Field of View)

    public const float DEFAULT_FOV =58.0f;    // ==>( 수평: 58.0f);   // 기본 FOV          //89.195f;
    public const float DEFAULT_VERT_FOV = 34.6348f;    // ==>( 수직: 34.6348f);   // 기본 수직 FOV (수평 58°일 때)  // 53.13f;
    public float DMAX_ZOOM = 36.0f;     // 최대 줌 배율 (휴컴스 카메라 기준 36배줌 )

    public int Idx
    {
        get => m_PCamera.m_Index;
        set => m_PCamera.m_Index = value;
    }
    /// <summary>
    /// PTZ camera의 실제 카메라
    /// </summary>
    public Camera getCamera { get => m_PCamera.cam;   }

    public float getFOV
    {
        get => m_PCamera.GetFOV();
    }

    public string GetCameraName()
    {
        if (m_PCamera != null)
            return m_PCamera.GetCameraName();
        return "No Camera";
    }

    /// <summary>
    /// static camera1의 실제 카메라
    /// </summary>

    // Start is called before the first frame update
    void Start()
    {
        // 인스펙터에서 설정한 값을 그대로 유지한다.
        // (이전: transform.localPosition = new Vector3(0, DEFAULT_CAM_HEIGHT, 0) 로 강제 초기화 → 제거)
        m_InitScale = transform.localScale;   // 인스펙터 스케일값 보관

        if (m_TargetRenderTexture != null)
        {
            // 렌더타겟용 카메라일경우 셋팅
            if ( m_PCamera.cam != Camera.main)
                m_PCamera.SetTargetRenderTexture(m_TargetRenderTexture);
        }
    }
    
    void OnDestroy()
    {
        RemovePole();
    }

    public void Initilaize(float fMaxZoom=36.0f)
    {
        DMAX_ZOOM = fMaxZoom;
    }

    /// <summary>
    /// 강제로 카메라를 셋팅한다. ( 랜더타겟용 카메라외에 mainCamera도 셋팅 가능하다.)
    /// </summary>
    /// <param name="kCamera"></param>
    public void SetForceInitCamera(Camera kCamera)
    {
        if( kCamera == Camera.main && m_PCamera != null)
        {
            var kPtzCam = kCamera.gameObject.AddComponent<CPTZCam>();
            kPtzCam.transform.SetParent(m_PCamera.transform.parent);
            kPtzCam.transform.localPosition = Vector3.zero;
            kPtzCam.transform.localRotation = m_PCamera.transform.localRotation;
            
            //kPtzCam.SetFOV(DEFAULT_FOV);
            SetZoomByFOV(kPtzCam, 1.0f, DMAX_ZOOM);   // 1배줌으로 초기화
            
            kPtzCam.SetTargetRenderTexture(null);
            m_PCamera = kPtzCam;
            
        }
    }
    

    public void SetPosition(Vector3 pos)
    {

    }

    public float pan { get {
            if (m_PCamera != null)
                return m_PCamera.GetPen(); // cam.transform.localEulerAngles.y;
            return 0.0f;
        }
        set
        {
            if (m_PCamera != null)
            {
                m_PCamera.SetPen(value);
                //Vector3 euler = m_PCamera.cam.transform.localEulerAngles;
                //euler.y = value;
                //m_PCamera.cam.transform.localEulerAngles = euler;
            }
        }
    }

    public float tilt { get {
            if (m_PCamera != null)
                return m_PCamera.GetTilt();// cam.transform.localEulerAngles.x;
            return 0.0f;
        }
        set {
            if (m_PCamera != null){
                m_PCamera.SetTilt(value);
            }
        }
    }
    
    /// <summary>
    /// 수직 FOV(도)로부터 zoom 배율을 계산한다.
    /// zoom getter와 동일 공식(DEFAULT_FOV / horizontalFov, [1, DMAX_ZOOM] clamp)을 사용한다.
    /// cam이 null이거나 aspect가 0 이하이면 aspect=1f로 fallback한다.
    /// </summary>
    /// <param name="verticalFov">수직 FOV (도)</param>
    /// <returns>zoom 배율 [1, DMAX_ZOOM]</returns>
    public float ZoomFromFov(float verticalFov)
    {
        if (verticalFov <= 0f) return 1.0f;
        float aspect = (m_PCamera != null && m_PCamera.cam != null && m_PCamera.cam.aspect > 0f)
            ? m_PCamera.cam.aspect : 1f;
        float horizontalFov = Camera.VerticalToHorizontalFieldOfView(verticalFov, aspect);
        float zoomLevel = DEFAULT_FOV / horizontalFov;
        return Mathf.Clamp(zoomLevel, 1.0f, DMAX_ZOOM);
    }

    public float zoom {
        get {
            if (m_PCamera != null) {
                float verticalFov = m_PCamera.GetFOV();
                return ZoomFromFov(verticalFov);
            }
            return 1.0f;
        }
        set {
            SetZoomByFOV(value, DMAX_ZOOM);
        }
    }

    public void SetPtzValue(float pan, float tilt, float zoom)
    {
        // Pan/Tilt 설정
        if (m_PCamera != null)
        {
            Vector3 euler = m_PCamera.transform.localEulerAngles;
            euler.x = tilt;
            euler.y = pan;
            m_PCamera.transform.localEulerAngles = euler;
            
            // Zoom 설정 (1 ~ DMAX_ZOOM 범위)
            this.SetZoomByFOV(zoom, DMAX_ZOOM);
            
            Debug.Log($"[CObjCamera] PTZ 업데이트: Pan={pan:F2}, Tilt={tilt:F2}, Zoom={zoom:F2}, FOV={m_PCamera.GetFOV():F2}");
        }
    }

    public void SetPenTilt(float pen, float tilt)
    {
        Camera kCamera = getCamera;
        
        Vector3 euler = kCamera.transform.localEulerAngles;
        euler.x = tilt;
        euler.y = pen;
        kCamera.transform.localEulerAngles = euler;
    }

    public void SetTargetRenderTexture(RenderTexture rt)
    {
        m_TargetRenderTexture = rt;   // 필드 갱신 — SetSelect에서 참조하므로 반드시 동기화
        if (m_PCamera != null)
        {
            m_PCamera.SetTargetRenderTexture(rt);
        }
    }

    /// <summary>
    /// 줌 배율(1~fMax)을 광학 줌 공식으로 FOV에 적용한다.
    /// DEFAULT_FOV는 수평 화각 기준(58°)이며, Unity Camera.fieldOfView(수직)로 변환해 적용한다.
    ///   horizontalFov = DEFAULT_FOV / zoom  (수평 화각, 반비례 관계)
    ///   zoom=1  → 수평 58.0°  (최광각)
    ///   zoom=2  → 수평 29.0°
    ///   zoom=36 → 수평 ≈1.61°  (최망원)
    /// cam.aspect 가 유효하지 않으면 1f fallback(수평=수직, 종전 동작 유지)
    /// </summary>
    public void SetZoomByFOV(float zoom, float fMax)
    {
        float verticalFov = SetZoomByFOV(m_PCamera, zoom, fMax);
        m_FOV = verticalFov;   // 필드 갱신 — SetSelect에서 참조하므로 반드시 동기화

        // if (m_PCamera != null)
        // {
        //     float zoomClamped = Mathf.Clamp(zoom, 1.0f, fMax);
        //     float horizontalFov = DEFAULT_FOV / zoomClamped;
        //     float aspect = (m_PCamera.cam != null && m_PCamera.cam.aspect > 0f)
        //         ? m_PCamera.cam.aspect : 1f;
        //     float verticalFov = Camera.HorizontalToVerticalFieldOfView(horizontalFov, aspect);
        //     m_PCamera.SetFOV(verticalFov);
        // }
    }

    public float SetZoomByFOV(CBaseCamera kCamera, float zoom, float fMax)
    {
        if (kCamera != null)
        {
            float zoomClamped = Mathf.Clamp(zoom, 1.0f, fMax);
            float horizontalFov = DEFAULT_FOV / zoomClamped;
            float aspect = (kCamera.cam != null && kCamera.cam.aspect > 0f)
                ? kCamera.cam.aspect : 1f;
            float verticalFov = Camera.HorizontalToVerticalFieldOfView(horizontalFov, aspect);
            kCamera.SetFOV(verticalFov);
            return verticalFov;
        }
        return 0f;
    }
    

    public void SetCameraFOV(float fov)
    {
        m_FOV = fov;
        if( m_PCamera != null )
        {
            m_PCamera.SetFOV(fov);
        }
    }

    /// <summary>
    /// 카메라의 XZ 월드 좌표, Y=0(바닥) 위치에 폴대를 생성한다.
    /// 부모는 CObjCamera와 동일한 부모(씬 계층 유지)로 설정한다.
    /// 기존 폴대가 있으면 먼저 제거 후 재생성한다.
    /// </summary>
    public GameObject CreatePole(GameObject goPrefab)
    {
        if (goPrefab == null) return null;

        // 기존 폴대가 있으면 제거
        RemovePole();

        // 폴대 생성 위치: 카메라의 XZ 좌표를 유지하되 Y=0(바닥)
        Vector3 vWorldPos = transform.position;
        vWorldPos.y = 0f;

        // CObjCamera 와 동일한 부모 아래에 생성 (씬 계층 정리)
        Transform parent = transform.parent;
        m_goPole = Instantiate(goPrefab, vWorldPos, Quaternion.identity, parent);
        m_goPole.transform.localScale = goPrefab.transform.localScale;

        return m_goPole;
    }

    public void ShowPole(bool bShow)
    {
        if (m_goPole != null)
        {
            m_goPole.SetActive(bShow);
        }
    }

    /// <summary>
    /// 폴대 위치를 현재 카메라 XZ 좌표(Y=0)로 갱신한다.
    /// 카메라가 이동된 후 폴대 위치를 동기화할 때 호출한다.
    /// </summary>
    public void UpdatePolePosition()
    {
        if (m_goPole == null) return;
        Vector3 worldPos = transform.position;
        worldPos.y = 0f;
        m_goPole.transform.position = worldPos;
    }

    public void RemovePole()
    {
        if (m_goPole != null)
        {
            Destroy(m_goPole);
            m_goPole = null;
        }
    }

}
