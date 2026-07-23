using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class CLineRect : MonoBehaviour
{
    public int m_idx = -1;
    //public Vector3 pointA = Vector3.zero;
    //public Vector3 pointB = Vector3.zero;
    //public Vector3 pointC = Vector3.zero;
    //public Vector3 pointD = Vector3.zero;

    public List<Vector3> m_Points = new List<Vector3>();

    protected LineRenderer m_LineRenderer;

    void Start() {
        m_LineRenderer = GetComponent<LineRenderer>();
    }
    
    public LineRenderer GetLineRenderer() {  return m_LineRenderer;   }

    public void MakeRect(LineRenderer lineRender, Vector3 posA, Vector3 posB, Vector3 posC, Vector3 posD, float lineWidth = 0.1f) {

        lineRender.useWorldSpace = false;  // 라인의 위치 이동 가능하게 처리
        lineRender.positionCount = 5; // 사각형을 그리려면 5개의 점이 필요합니다.
        lineRender.loop = true; // 선을 루프로 만듭니다.

        // 선의 폭을 설정합니다.
        lineRender.startWidth = lineWidth; // 0.1f;
        lineRender.endWidth = lineWidth; // 0.1f;

        // 선의 색상을 설정합니다.
        lineRender.startColor = Color.red;
        lineRender.endColor = Color.red;

        // 사각형의 네 꼭짓점을 설정합니다.
        lineRender.SetPosition(0, posA);
        lineRender.SetPosition(1, posB);
        lineRender.SetPosition(2, posC);
        lineRender.SetPosition(3, posD);
        lineRender.SetPosition(4, posA); // 처음 점으로 돌아옵니다.
    }
    public void MakeRect(LineRenderer lineRender, List<Vector3> points, float lineWidth = 0.05f)
    {
        Debug.Assert(points.Count == 4, $"points count = {points.Count}");
        if (points.Count != points.Count) return;

        lineRender.useWorldSpace = false;   // 라인의 위치 이동 가능하게 처리
        lineRender.positionCount = 5;       // 사각형을 그리려면 5개의 점이 필요합니다.
        lineRender.loop = true;             // 선을 루프로 만듭니다.

        // 선의 폭을 설정합니다.
        lineRender.startWidth = lineWidth;      // 0.1f;
        lineRender.endWidth = lineWidth;        // 0.1f;

        // 선의 색상을 설정합니다.
        lineRender.startColor = Color.red;
        lineRender.endColor = Color.red;

        // 사각형의 네 꼭짓점을 설정합니다.
        for (int i = 0; i < points.Count; i++) {
            lineRender.SetPosition(i, points[i]);
        }
        lineRender.SetPosition(points.Count, points[0]); // 처음 점으로 돌아옵니다.
    }

    public void MakeRect(LineRenderer lineRender, float xSize, float zSize, float y=0.02f, float lineWidth = 0.05f) {
        m_LineRenderer = lineRender;
        Vector3 pointA = new Vector3(-xSize * 0.5f, y, -zSize * 0.5f);
        Vector3 pointB = new Vector3(-xSize * 0.5f, y, zSize * 0.5f);
        Vector3 pointC = new Vector3(xSize * 0.5f, y, zSize * 0.5f);
        Vector3 pointD = new Vector3(xSize * 0.5f, y, -zSize * 0.5f);

        m_Points.Clear();
        m_Points.Add(pointA);
        m_Points.Add(pointB);
        m_Points.Add(pointC);
        m_Points.Add(pointD);

        //MakeRect(m_LineRenderer, pointA, pointB, pointC, pointD, lineWidth);
        MakeRect(m_LineRenderer, m_Points, lineWidth);
    }

    /// <summary>
    /// 월드좌표로 저장한다.
    /// </summary>
    /// <param name="vWorldPos"></param>
    public void SetPosition( Vector3 vWorldPos ) {
        transform.position = vWorldPos;  
    }

    // xSize : 사각형 가로 길이, zSize: 사각형 세로길이, y : y좌표 
    public static CLineRect CreateLineRect(Transform kParent, float xSize, float zSize, float y = 0.05f) {
        GameObject go = new GameObject("LineRect");
        go.transform.parent = kParent;
        go.transform.localRotation = Quaternion.identity;

         
        var lr = go.AddComponent<LineRenderer>();
        var kRect = go.AddComponent<CLineRect>();
        kRect.MakeRect(lr, xSize, zSize, y);
        Material mat = Resources.Load<Material>("Matreials/RectLine");
        lr.material = mat;

        mat.color = Color.white;

        return kRect;
    }


    public void SetLineColor(Color color)
    {
        LineRenderer lr = GetComponent<LineRenderer>();
        lr.material.color = color;
    }

}
