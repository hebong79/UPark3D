using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class CPTZCam : CBaseCamera
{
    private void Awake()
    {
        if (m_Camera == null)
            m_Camera = GetComponent<Camera>();

        Init_Transform();
        m_CameraType = ECameraType.PTZ;
    }

   
    public override void OnUpdate(float elapsedTime)
    {
        

    }


    //void Update()
    //{
    //    OnUpdate(Time.deltaTime);
    //}










}
