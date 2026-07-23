using UnityEngine;


/// <summary>
/// 주차면(2.5x6m) 회전 배치 시 각종 거리 값을 계산하는 유틸리티 클래스.
///
/// [좌표 기준]
///   - e / j : 수직축(Y축)으로부터의 각도 (도 단위, 이미지 기준)
///   - 회전 중심 : 주차면의 기하학적 중심
///   - 중심선(Center Line) : 주차면들이 배치되는 기준 수직선 (이미지1의 빨간 세로선)
/// </summary>
public static class CParkingGeometry
{
    // ─────────────────────────────────────────────
    //  이미지 1 : 단일 주차면 회전 치수
    // ─────────────────────────────────────────────

    /// <summary>
    /// 이미지 1 - 2.5x6m 주차면을 각도 e(도)만큼 회전했을 때의 치수를 반환합니다.
    ///
    ///  a : 회전된 사각형의 전체 수평 폭 (bounding box width)
    ///  b : 중심선(Center Line)에서 왼쪽 끝까지의 거리
    ///  c : 중심선에서 오른쪽으로 돌출된 거리  (a = b + c)
    ///  d : 회전된 사각형의 전체 수직 높이 (band height / bounding box height)
    ///
    /// [계산식]
    ///  회전된 사각형의 bounding box:
    ///    BW = |W * cos(e)| + |L * sin(e)|   (수평 폭, W=2.5, L=6)
    ///    BH = |W * sin(e)| + |L * cos(e)|   (수직 높이)
    ///
    ///  이미지 1에서 중심선은 사각형 중심을 통과합니다.
    ///  따라서 b = c = BW / 2  (대칭 배치 기준)
    ///
    ///  단, 이미지에서는 사각형이 중심선 왼쪽에 치우쳐 있으므로
    ///  실제 오프셋(offsetX)을 매개변수로 받아 b, c를 개별 계산합니다.
    ///  offsetX > 0 이면 중심이 중심선 오른쪽에 위치.
    /// </summary>
    /// <param name="angleDeg">회전 각도 e (도, 수직축 기준)</param>
    /// <param name="width">주차면 폭 (기본 2.5m)</param>
    /// <param name="length">주차면 길이 (기본 6m)</param>
    /// <param name="centerOffsetX">
    ///   주차면 중심의 중심선으로부터 수평 오프셋 (기본 0 = 중심선 위).
    ///   양수 = 오른쪽, 음수 = 왼쪽.
    /// </param>
    /// <returns>a, b, c, d 값을 담은 구조체</returns>
    public static SingleParkingResult GetSingleParkingDimensions(
        float angleDeg,
        float width = 2.5f,
        float length = 6.0f,
        float centerOffsetX = 0f)
    {
        float rad = angleDeg * Mathf.Deg2Rad;
        float cosA = Mathf.Abs(Mathf.Cos(rad));
        float sinA = Mathf.Abs(Mathf.Sin(rad));

        // Bounding box 크기
        float bw = width * cosA + length * sinA;   // 전체 수평 폭
        float bh = width * sinA + length * cosA;   // 전체 수직 높이

        float halfBW = bw * 0.5f;

        // b = 중심선에서 왼쪽 끝까지
        // c = 중심선에서 오른쪽 끝까지
        // 중심이 오른쪽(+X)으로 offset 되어 있으면 b가 늘고 c가 줄어듦
        float b = halfBW - centerOffsetX;  // 왼쪽 거리
        float c = halfBW + centerOffsetX;  // 오른쪽 거리
        float a = b + c;                   // == bw (검증용)

        return new SingleParkingResult
        {
            boxWidth = a,
            leftSlopeW = Mathf.Max(0f, b),
            rightSlopW = Mathf.Max(0f, c),
            boxHeight = bh
        };
    }

    // ─────────────────────────────────────────────
    //  이미지 2 : 복수 주차면 배열 치수
    // ─────────────────────────────────────────────

    /// <summary>
    /// 이미지 2 - N개의 주차면을 각도 j(도)로 회전하여 연속 배치할 때의 치수를 반환합니다.
    ///
    ///  f : 전체 배열의 수평 길이 (첫 주차면 왼쪽 끝 ~ 마지막 주차면 오른쪽 끝)
    ///  g : 배열 시작 기준선에서 첫 번째 주차면 중심까지의 거리 (= a / 2)
    ///  h : 인접 주차면 중심 간의 수평 간격
    ///  i : 배열의 수직 높이 (= d, bounding box height)
    ///
    /// [계산식]
    ///  인접 주차면은 서로 꼭짓점이 맞닿는(touching) 방식으로 배치됩니다.
    ///  이미지에서 주차면들이 지그재그(alternating) 방식으로 맞물려 있으므로
    ///  중심 간격 h는 회전된 사각형의 수평 폭 절반이 만나는 지점으로 결정됩니다.
    ///
    ///  h = W * cos(j)
    ///    (폭 방향의 수평 투영 거리 - 꼭짓점이 겹치는 배치)
    ///
    ///  g = a / 2   (첫 주차면 bounding box 절반)
    ///
    ///  f = g + (N-1) * h + g
    ///    = a + (N-1) * h
    ///
    ///  i = d  (단일 bounding box 높이와 동일)
    ///
    /// [주의]
    ///  실제 배치 방식(겹침/간격)에 따라 h 계산이 달라질 수 있습니다.
    ///  spacing 매개변수로 추가 간격을 조정하세요.
    /// </summary>
    /// <param name="count">주차면 개수 N</param>
    /// <param name="angleDeg">회전 각도 j (도, 수직축 기준)</param>
    /// <param name="width">주차면 폭 (기본 2.5m)</param>
    /// <param name="length">주차면 길이 (기본 6m)</param>
    /// <param name="additionalSpacing">중심 간격에 추가할 여분 간격 (기본 0)</param>
    /// <returns>f, g, h, i 값을 담은 구조체</returns>
    public static MultiParkingResult GetMultiParkingDimensions(
        int count,
        float angleDeg,
        float width = 2.5f,
        float length = 6.0f,
        float additionalSpacing = 0f)
    {
        if (count <= 0)
            return default;

        // 단일 주차면의 bounding box 먼저 계산
        SingleParkingResult single = GetSingleParkingDimensions(angleDeg, width, length);

        float rad = angleDeg * Mathf.Deg2Rad;
        float cosJ = Mathf.Abs(Mathf.Cos(rad));

        // h : 인접 주차면 중심 간 수평 간격
        // 폭(width) 방향이 수평으로 투영된 거리 = width / cos(j)
        float h = width / cosJ + additionalSpacing;

        // g : 배열 기준선에서 첫 중심까지 = bounding box 폭의 절반
        float g = single.boxWidth * 0.5f;

        // f : 전체 배열 수평 길이
        // = 왼쪽 여백(g) + 중심 간격 * (N-1) + 오른쪽 여백(g)
        float f = single.boxWidth + (count - 1) * h;
        // i : 배열의 수직 높이 = 단일 bounding box 높이
        float i = single.boxHeight;

        return new MultiParkingResult
        {
            totWidth = f,
            oneBoxW = single.boxWidth,
            slopHoriDist = g,
            stepW = h,
            oneH = i
        };
    }

    // ─────────────────────────────────────────────
    //  보조 함수 : 주차면 중심 좌표 배열 생성
    // ─────────────────────────────────────────────

    /// <summary>
    /// 이미지 2처럼 N개의 주차면 중심 위치(Vector2)를 반환합니다.
    /// 배열 시작점(origin)을 기준으로 수평으로 h 간격씩 배치합니다.
    /// </summary>
    /// <param name="origin">첫 번째 주차면 중심 위치</param>
    /// <param name="count">주차면 개수</param>
    /// <param name="angleDeg">회전 각도 j (도)</param>
    /// <param name="width">주차면 폭</param>
    /// <param name="length">주차면 길이</param>
    /// <param name="additionalSpacing">추가 간격</param>
    public static Vector2[] GetParkingCenters(
        Vector2 origin,
        int count,
        float angleDeg,
        float width = 2.5f,
        float length = 6.0f,
        float additionalSpacing = 0f)
    {
        MultiParkingResult multi = GetMultiParkingDimensions( count, angleDeg, width, length, additionalSpacing);

        Vector2[] centers = new Vector2[count];
        for (int idx = 0; idx < count; idx++)
        {
            centers[idx] = new Vector2(origin.x + idx * multi.stepW, origin.y);
        }
        return centers;
    }
}

// ─────────────────────────────────────────────
//  결과 구조체
// ─────────────────────────────────────────────

/// <summary>
/// 이미지 1 단일 주차면 회전 치수 결과.
/// </summary>
public struct SingleParkingResult
{
    /// <summary>회전된 사각형의 전체 수평 폭 (bounding box width)</summary>
    public float boxWidth;

    /// <summary>중심선에서 왼쪽 끝까지의 거리</summary>
    public float leftSlopeW;

    /// <summary>중심선에서 오른쪽 끝까지의 거리</summary>
    public float rightSlopW;

    /// <summary>회전된 사각형의 전체 수직 높이 (band height)</summary>
    public float boxHeight;

    public override string ToString() =>
        $"a={boxWidth:F3}m  b={leftSlopeW:F3}m  c={rightSlopW:F3}m  d={boxHeight:F3}m";
}

/// <summary>
/// 이미지 2 복수 주차면 배열 치수 결과.
/// </summary>
public struct MultiParkingResult
{
    /// <summary>전체 배열의 수평 길이</summary>
    public float totWidth;

    /// <summary>왼쪽 하단 꼭지점 부터 왼쪽 상단 꼭지점까지의 수평 거리, 배열 시작 기준선에서 첫 번째 주차면 중심까지의 거리</summary>
    public float slopHoriDist;

    /// <summary>box 1개의 전체 수평 폭 (bounding box width)</summary>
    public float oneBoxW;

    /// <summary>인접 주차면 중심 간의 수평 간격</summary>
    public float stepW;

    /// <summary>배열의 수직 높이 (bounding box height)</summary>
    public float oneH;

    public override string ToString() =>
        $"f={totWidth:F3}m  g={slopHoriDist:F3}m  h={stepW:F3}m  i={oneH:F3}m";
}


// ─────────────────────────────────────────────
//  사용 예시 (MonoBehaviour)
// ─────────────────────────────────────────────

/*
public class ParkingTest : MonoBehaviour
{
    [Header("단일 주차면 (이미지 1)")]
    [Range(0f, 89f)] public float anglE = 30f;
    public float offsetX = 0f;

    [Header("복수 주차면 (이미지 2)")]
    public int   parkingCount = 10;
    [Range(0f, 89f)] public float anglJ = 30f;
    public float extraSpacing = 0f;

    void Start()
    {
        // 이미지 1
        var s = ParkingGeometry.GetSingleParkingDimensions(anglE, 2.5f, 6f, offsetX);
        Debug.Log($"[단일] {s}");

        // 이미지 2
        var m = ParkingGeometry.GetMultiParkingDimensions(parkingCount, anglJ, 2.5f, 6f, extraSpacing);
        Debug.Log($"[복수] {m}");

        // 중심 좌표 배열
        Vector2[] centers = ParkingGeometry.GetParkingCenters(
            Vector2.zero, parkingCount, anglJ, 2.5f, 6f, extraSpacing);
        for (int i = 0; i < centers.Length; i++)
            Debug.Log($"  주차면[{i+1}] 중심 = {centers[i]}");
    }
}
*/
