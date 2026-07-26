using UnityEngine;

/// <summary>
/// VLA 이미지 캡처 시 한 PTZ 스텝당 배치할 차량 수를 가중치 분포로 선택한다.
/// P1-2: 분포를 GT 평균(≈4.4)에 정합하도록 재조정.
/// 분포: 1대 2%, 2대 5%, 3대 13%, 4대 30%, 5대 30%, 6대 15%, 7대 5%.
/// 기대 평균 = 0.02*1 + 0.05*2 + 0.13*3 + 0.30*4 + 0.30*5 + 0.15*6 + 0.05*7
///           = 0.02 + 0.10 + 0.39 + 1.20 + 1.50 + 0.90 + 0.35 = 4.46 ≈ 4.4
/// </summary>
public static class CVehicleCountRandomizer
{
    /// <summary>가중치 분포에 따라 차량 수를 반환한다. 결과 범위: 1 ~ 7.</summary>
    public static int Pick()
    {
        int roll = Random.Range(0, 100);
        if (roll <  2) return 1;   //  2%
        if (roll <  7) return 2;   //  5%
        if (roll < 20) return 3;   // 13%
        if (roll < 50) return 4;   // 30%
        if (roll < 80) return 5;   // 30%
        if (roll < 95) return 6;   // 15%
        return 7;                  //  5%
    }
}
