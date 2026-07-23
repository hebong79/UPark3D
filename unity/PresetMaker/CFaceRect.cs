using System;
using System.Collections;
using System.Collections.Generic;
using UnityEngine;

/// <summary>
/// 주차면 방향에 따라 x, 또는  z 값을 더해줄것인지 빼줄거인지 결정하기 위한 enum
/// </summary>
public enum EFaceDirType
{
    Default,    // x축 우측 방향, z축 우측 방향
    Dir,        // 특정 방향을 가진 대각선
}

/// <summary>
/// 주차라인 위치 타입 
/// 1번 카메라 위치 기준
/// </summary>
public enum EGroupPosType
{
    eNone = -1,
    eDown = 0,              // 아래쪽 : 1 번 카메라
    eUp,                    // 위쪽    
    eCenterDown,
    eCenterUp,
    eLeft,
    eRight,
}

/// <summary>
/// 주차면 사각형 그리기 클래스
/// </summary>
public class CFaceRect : CLineRect
{
    public const float DParkFaceLineWidth = 0.1f;       // 주자면 라인 두께

    public int m_Id = 0;
    public float m_faceRot = 0;                          // 주차면 회전값
    public EGroupPosType m_ePosType = EGroupPosType.eNone;

    public Color m_LineColor = new Color32(0, 240, 130, 255);
    public float m_LineWidth = 0.05f;

    public void Initialize(LineRenderer lr, int index = 0, float lineWidth = 0.02f)
    {
        m_Id = index;
        if (lr == null)
            lr = GetComponent<LineRenderer>();

        m_LineRenderer = lr;
        m_LineWidth = lineWidth;

        // m_lineRenderer 설정
        m_LineRenderer.positionCount = 0;
        m_LineRenderer.startWidth = lineWidth;    // 0.015f;
        m_LineRenderer.endWidth = lineWidth;      // 0.015f;
        m_LineRenderer.useWorldSpace = false; // Canvas UI의 좌표계를 따르도록 설정

        //Material mat = new Material(Shader.Find("Unlit/Color"));
        Material mat = new Material(Shader.Find("Universal Render Pipeline/Unlit"));
        //Material mat = new Material(Shader.Find("Standard"));
        mat.color = m_LineColor; // new Color32(0,240, 130, 255);//  UnityEngine.Color.green;
        m_LineRenderer.material = mat;
    }

    /// <summary>
    /// 3D 육면체 박스를 그릴때 사용한다.
    /// </summary>
    /// <param name="kParent"></param>
    /// <param name="index"></param>
    /// <param name="lineWidth"></param>
    /// <returns></returns>
    public static CFaceRect CreateLineRect(Transform kParent, int index, float lineWidth = 0.02f)
    {
        GameObject go = new GameObject("FaceRect");
        go.transform.parent = kParent;

        var lr = go.AddComponent<LineRenderer>();
        var kRect = go.AddComponent<CFaceRect>();
        kRect.Initialize(lr, index, lineWidth);

        return kRect;
    }

    /// <summary>
    /// 바닥면만을 그릴때 사용한다.
    /// </summary>
    /// <param name="kParent"></param>
    /// <param name="xSize"></param>
    /// <param name="zSize"></param>
    /// <param name="yPos"></param>
    /// <param name="lineWidth"></param>
    /// <returns></returns>
    public static CFaceRect CreateFaceRect(Transform kParent, float xSize, float zSize, float yPos = 0.05f, float lineWidth = 0.05f)
    {
        GameObject go = new GameObject("LineRect");
        go.transform.parent = kParent;
        go.transform.localRotation = Quaternion.identity;
        go.tag = "ParkFace";

        var lr = go.AddComponent<LineRenderer>();
        var kRect = go.AddComponent<CFaceRect>();
        kRect.MakeRect(lr, xSize, zSize, yPos, lineWidth);

        Material mat = Resources.Load<Material>("Materials/RectLine");
        //Material mat = new Material(Shader.Find("Unlit/Color"));
        lr.material = mat;
        mat.color = Color.white;
        int layer = LayerMask.NameToLayer("ParkFace");
        if (layer == -1)
            layer = 0;

        go.layer = layer;

        return kRect;
    }

    // 3d 좌표그대로 라인 출력하기
    public void SetPositionWorld(List<Vector3> list)
    {
        m_Points.Clear();

        for (int i = 0; i < list.Count; i++)
        {
            Vector3 pos = list[i];
            m_Points.Add(pos);

            m_LineRenderer.positionCount = i + 1;
            m_LineRenderer.SetPosition(i, pos);
        }

        // 4번째 점을 찍었으면 처음 점으로 연결하여 닫힌 사각형을 만듦
        m_LineRenderer.positionCount = m_Points.Count + 1;
        m_LineRenderer.SetPosition(4, m_Points[0]);
    }

    // 사각형이 회전됐을때의 폭 구하기 ( 사선으로 주차된 경우 )
    public static float CalculateRotatedWidth(float width, float height, float fAngle)
    {
        // 45도 회전 시의 폭 계산
        float angle = fAngle * Mathf.Deg2Rad; // 각도를 라디안으로 변환
        //float rotatedWidth = (width * Mathf.Cos(angle)) + (height * Mathf.Sin(angle));
        float rotatedWidth = (width / Mathf.Cos(angle));
        return rotatedWidth;
    }


}
