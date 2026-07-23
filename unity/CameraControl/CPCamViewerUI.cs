using System;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.UI;

public class CPCamViewerUI : CDialogUI, IBeginDragHandler, IDragHandler, IEndDragHandler
{

    // 뷰 사이즈 구조체
    [Serializable]
    public class SViewSize
    {
        public Vector2 pos;
        public Vector2 size;
        public SViewSize(Vector2 p, Vector2 s)
        {
            pos = p;
            size = s;
        }
    }

    public List<SViewSize> m_ViewSizeList = new();  // 뷰 사이즈 리스트

    public RawImage m_RawImage = null; // 카메라 뷰어 이미지

    public int m_CurViewSizeIdx = 0;   // 현재 뷰 사이즈 인덱스

    // Start is called once before the first execution of Update after the MonoBehaviour is created
    void Start()
    {
        Init_ViewSize();
    }

    // 뷰 사이즈 초기화
    public void Init_ViewSize()
    {
        float w = 1920f;
        float h = 1080f;
        float w1 = w * 0.5f;
        float h1 = h * 0.5f;
        float x1 = w1 * 0.5f;
        float y1 = -h1 * 0.5f;

        m_ViewSizeList.Add(new SViewSize(new Vector2(x1, y1), new Vector2(w1, h1)));   // 0
        m_ViewSizeList.Add(new SViewSize(new Vector2(280, 0), new Vector2(1351.1f, 760f))); // 1
        m_ViewSizeList.Add(new SViewSize(new Vector2(0, 0), new Vector2(w,h)));             // 2
    }

    // 렌더 텍스쳐 설정
    public void SetRenderTexture(RenderTexture rt) {
        if (m_RawImage != null) {
            m_RawImage.texture = rt;
        }
    }

    // 뷰 사이즈 설정
    public void SetViewSize(int idx)
    {
        if (idx < 0 || idx >= m_ViewSizeList.Count) return;

        SViewSize vs = m_ViewSizeList[idx];
        //RectTransform rt = m_RawImage.GetComponent<RectTransform>();
        RectTransform rt = GetComponent<RectTransform>();
        rt.anchoredPosition = vs.pos;
        rt.sizeDelta = vs.size;

        // Z 위치 고정
        Vector3 vPos = rt.localPosition;
        vPos.z = 0;
        rt.localPosition = vPos;
    }

    // 뷰 사이즈 갯수 반환
    public int GetViewSizeCount()
    {
        return m_ViewSizeList.Count;
    }

    // 다음 뷰 사이즈로 변경
    public void NextViewSize()
    {
        m_CurViewSizeIdx++;
        if (m_CurViewSizeIdx >= m_ViewSizeList.Count)
            m_CurViewSizeIdx = 0;

        SetViewSize(m_CurViewSizeIdx);
    }


    public void OnBeginDrag(PointerEventData data)
    {
    }
    public void OnDrag(PointerEventData data)
    {
    }
    public void OnEndDrag(PointerEventData data)
    {
    }

}
