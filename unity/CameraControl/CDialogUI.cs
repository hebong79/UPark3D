using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class CBaseUI : MonoBehaviour
{
    public void Show(bool bShow)
    {
        gameObject.SetActive(bShow);
    }
    public virtual bool IsShow()
    {
        return gameObject.activeInHierarchy;
    }

    public static void Show(GameObject go, bool bShow)
    {
        Debug.Assert(go != null);
        if( go != null)
            go.SetActive(bShow);
    }

}

/// <summary>
/// 대화상자 또는 대부분 UI에서 상속 받아 사용한다.
/// </summary>
public class CDialogUI : CBaseUI
{
    public virtual void OpenUI()
    {
        Show(true);
    }

    public virtual void CloseUI()
    {
        Show(false);
    }

  
}
