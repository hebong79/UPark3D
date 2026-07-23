using System.Collections;
using System.Collections.Generic;
using UnityEngine;


public class CBaseCamera : MonoBehaviour
{
    public enum ECameraType
    {
        Static = 0,
        PTZ = 1,
    }

    public int m_Index = 0;         // 1부터 시작하는 카메라 번호 ( UI 표시용 )
    public ECameraType m_CameraType = ECameraType.PTZ;
    protected Camera m_Camera = null;

    public Vector3 m_InitPos = Vector3.zero;
    public Vector3 m_InitRot = Vector3.zero;

    public Camera cam
    {
        get => m_Camera;
    }

    public string GetCameraName(int index)
    {
        if( m_CameraType == ECameraType.PTZ )
            return "PTZ Camera " + index.ToString();
        else
            return "Static Camera " + index.ToString();

    }

    public string GetCameraName()
    {
        if (m_CameraType == ECameraType.PTZ)
            return "PTZ Camera " + m_Index.ToString();
        else
            return "Static Camera " + m_Index.ToString();

    }

    public void Init_SetCamera(Camera kCamera)
    {
        m_Camera = kCamera;
    }

    //초기 위치와 회전값 보관
    public void Init_Transform()
    {
        m_InitPos = transform.position;
        m_InitRot = transform.localEulerAngles;
    }

    // 위치/회전 모두 초기화
    public void SetInitTransform()
    {
        transform.position = m_InitPos;
        transform.localRotation = Quaternion.Euler(m_InitRot);
    }
    // 위치값 초기화
    public void SetInitPos()
    {
        transform.position = m_InitPos;
    }

    // 회전값 초기화
    public void SetInitRot()
    {
        transform.localRotation = Quaternion.Euler(m_InitRot);
    }

    public void SetTargetRenderTexture(RenderTexture rt)
    {
        if (m_Camera != null)
        {
            m_Camera.targetTexture = rt;
        }
    }

    public void SetTilt(float tilt)
    {
        Vector3 euler = transform.localEulerAngles;
        euler.x = tilt;
        transform.localEulerAngles = euler;
    }
    public float GetTilt()
    {
         return transform.localEulerAngles.x;
    }
    public void SetPen(float pen)
    {
        Vector3 euler = transform.localEulerAngles;
        euler.y = pen;
        transform.localEulerAngles = euler;
    }
    public float GetPen()
    {
        return transform.localEulerAngles.y;
    }

    public void SetFOV(float fov)
    {
        if (m_Camera != null)
        {
            m_Camera.fieldOfView = fov;
        }
    }

    public float GetFOV()
    {
        if (m_Camera != null)
        {
            return m_Camera.fieldOfView;
        }
        return 58.0f;
    }

    public virtual void OnUpdate(float elapsedTime)
    {
        
    }

}

public class CStaticCam : CBaseCamera
{
    // Start is called before the first frame update
    void Awake()
    {
        if (m_Camera == null)
            m_Camera = GetComponent<Camera>();

        m_CameraType = ECameraType.Static;

        Init_Transform();
    }


}
