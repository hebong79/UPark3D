using UnityEngine;
using System.Collections.Generic;

/// <summary>
/// 차량의 색상을 제어하는 클래스
/// GLB 모델의 특정 Material을 찾아 색상을 변경합니다.
/// glTFast 임포터 기반 GLB 파일의 baseColorFactor 프로퍼티를 지원합니다.
/// 금속성(메탈릭) 재질을 적용하여 차량 도색의 은은한 광택을 표현합니다.
/// </summary>
public class CVehicleColorController : MonoBehaviour
{
    [Header("설정")]
    // GLB(glTFast) 임포트 모델의 재질/오브젝트 이름 기준 키워드
    // 주의: "Base" 제거 - Bulb_Base / headlamplamp_base / taillight_Base 등 헤드라이트·미러 베이스를 잘못 타겟팅하는 버그 수정
    [SerializeField] private string[] m_TargetMaterialKeywords = { "carpaint", "Body", "Paint", "CarBody", "Outer" };
    [SerializeField] private Color m_DefaultColor = Color.white;
    [SerializeField] private Color m_CurColor = Color.white;

    [Header("금속성 재질 설정")]
    [Tooltip("금속성 강도 (0=비금속, 1=완전금속). 차량 도색 특성상 0.7 권장")]
    [Range(0f, 1f)]
    [SerializeField] private float m_MetallicValue = 0.7f;
    [Tooltip("표면 매끈함 (0=완전거침, 1=완전매끈). 은은한 광택을 위해 0.6 권장")]
    [Range(0f, 1f)]
    [SerializeField] private float m_SmoothnessValue = 0.6f;

    private List<Renderer> m_TargetRenderers = new List<Renderer>();
    private MaterialPropertyBlock m_PropBlock;

    // URP Lit / SimpleLit 쉐이더 프로퍼티
    private static readonly int s_BaseColorId = Shader.PropertyToID("_BaseColor");
    private static readonly int s_MetallicId = Shader.PropertyToID("_Metallic");
    private static readonly int s_SmoothnessId = Shader.PropertyToID("_Smoothness");
    // Standard 쉐이더 프로퍼티
    private static readonly int s_ColorId = Shader.PropertyToID("_Color");
    private static readonly int s_GlossinessId = Shader.PropertyToID("_Glossiness");
    // glTFast(GLTF/GLB) 쉐이더 프로퍼티 - carpaint.mat 등에서 사용
    private static readonly int s_BaseColorFactorId = Shader.PropertyToID("baseColorFactor");
    private static readonly int s_MetallicFactorId = Shader.PropertyToID("metallicFactor");
    private static readonly int s_RoughnessFactorId = Shader.PropertyToID("roughnessFactor");

    private void Awake()
    {
        m_PropBlock = new MaterialPropertyBlock();
        FindTargetRenderers();
    }

    void Start()
    {

    }

    /// <summary>
    /// 모델 내에서 색상 변경 대상이 되는 Renderer를 찾습니다.
    /// sharedMaterial 이름 및 렌더러 오브젝트 이름 모두 검사하여 GLB 모델과 호환합니다.
    /// Renderer 탐색 후 첫 번째 대상 머티리얼의 실제 색상으로 m_CurColor를 초기화합니다.
    /// (SetColor()가 한 번도 호출되지 않은 경우 GetCurrentColor()가 흰색을 반환하는 버그 방지)
    /// </summary>
    public void FindTargetRenderers()
    {
        m_TargetRenderers.Clear();
        Renderer[] allRenderers = GetComponentsInChildren<Renderer>(true);

        foreach (var renderer in allRenderers)
        {
            if (IsTargetRenderer(renderer))
            {
                m_TargetRenderers.Add(renderer);
            }
        }

        // 첫 번째 대상 Renderer의 sharedMaterial 실제 색상으로 m_CurColor 초기화
        // SetColor()가 아직 호출되지 않았을 때 GetCurrentColor()가 올바른 색을 반환하도록 보장
        if (m_TargetRenderers.Count > 0)
        {
            Material mat = m_TargetRenderers[0].sharedMaterial;
            if (mat != null)
            {
                Color matColor;
                if (mat.HasProperty(s_BaseColorId))
                    matColor = mat.GetColor(s_BaseColorId);
                else if (mat.HasProperty(s_BaseColorFactorId))
                    matColor = mat.GetColor(s_BaseColorFactorId);
                else if (mat.HasProperty(s_ColorId))
                    matColor = mat.GetColor(s_ColorId);
                else
                    matColor = m_DefaultColor;

                m_CurColor     = matColor;
                m_DefaultColor = matColor;
            }
        }

        CLogger.Inst.Debug($"차량 '{gameObject.name}'에서 색상 변경 대상 Renderer {m_TargetRenderers.Count}개 발견", "CarColor");
    }

    /// <summary>
    /// 해당 Renderer가 색상 변경 대상인지 판별합니다.
    /// GLB 임포트 시 오브젝트 이름에 재질명이 포함되는 경우도 처리합니다.
    /// </summary>
    private bool IsTargetRenderer(Renderer renderer)
    {
        // 0) 번호판 태그("PlateNum") 오브젝트는 색상 변경 대상에서 명시적으로 제외
        //    LicPlate01 / LicPlate02 게임오브젝트에 PlateNum 태그가 설정되어 있음
        if (renderer.CompareTag("PlateNum"))
            return false;

        // 0-1) 오브젝트 이름에 "licplate" 포함 시에도 제외 (이중 안전장치)
        string objNameLower = renderer.gameObject.name.ToLower();
        if (objNameLower.Contains("licplate"))
            return false;

        // 1) 렌더러가 부착된 오브젝트 이름 검사 (GLB: "Kia_K5_2019_mirror_carpaint" 등)
        foreach (var keyword in m_TargetMaterialKeywords)
        {
            if (objNameLower.Contains(keyword.ToLower()))
                return true;
        }

        // 2) sharedMaterials 이름 검사 (직접 지정된 mat 파일 참조 시)
        //    먼저 번호판 재질 포함 여부를 전체 검사 후, 키워드 매칭 여부 판정
        foreach (var mat in renderer.sharedMaterials)
        {
            if (mat == null) continue;
            // 번호판 재질명 포함 시 전체 렌더러 제외 (LicPlate_white / LicPlate_black / LicPlate_blue)
            if (mat.name.ToLower().Contains("licplate"))
                return false;
        }

        foreach (var mat in renderer.sharedMaterials)
        {
            if (mat == null) continue;
            string matName = mat.name.ToLower();
            foreach (var keyword in m_TargetMaterialKeywords)
            {
                if (matName.Contains(keyword.ToLower()))
                    return true;
            }
        }

        return false;
    }

    /// <summary>
    /// 차량의 색상을 변경합니다.
    /// URP(_BaseColor), Standard(_Color), glTFast(baseColorFactor) 쉐이더를 모두 지원합니다.
    /// 금속성 재질(Metallic/Smoothness)을 함께 적용하여 차량 도색의 은은한 광택을 표현합니다.
    /// </summary>
    /// <param name="color">변경할 색상</param>
    public void SetColor(Color color)
    {
        if (m_PropBlock == null) m_PropBlock = new MaterialPropertyBlock();

        m_CurColor = color;  // 현재 색상 상태 갱신

        foreach (var renderer in m_TargetRenderers)
        {
            if (renderer == null) continue;

            renderer.GetPropertyBlock(m_PropBlock);

            // URP Lit/SimpleLit 쉐이더 - 색상
            m_PropBlock.SetColor(s_BaseColorId, color);
            // URP Lit 쉐이더 - 금속성 재질 (은은한 광택)
            m_PropBlock.SetFloat(s_MetallicId, m_MetallicValue);
            m_PropBlock.SetFloat(s_SmoothnessId, m_SmoothnessValue);

            // Standard 쉐이더 - 색상
            m_PropBlock.SetColor(s_ColorId, color);
            // Standard 쉐이더 - 금속성/광택
            m_PropBlock.SetFloat(s_GlossinessId, m_SmoothnessValue);

            // glTFast(GLTF) 쉐이더 - carpaint 등에서 사용하는 baseColorFactor
            m_PropBlock.SetColor(s_BaseColorFactorId, color);
            // glTFast 쉐이더 - 금속성/거칠기 (roughness = 1 - smoothness)
            m_PropBlock.SetFloat(s_MetallicFactorId, m_MetallicValue);
            m_PropBlock.SetFloat(s_RoughnessFactorId, 1.0f - m_SmoothnessValue);

            renderer.SetPropertyBlock(m_PropBlock);
        }
    }

    /// <summary>
    /// 금속성 재질 값을 런타임에 변경합니다.
    /// </summary>
    /// <param name="metallic">금속성 강도 (0~1)</param>
    /// <param name="smoothness">표면 매끈함 (0~1)</param>
    public void SetMetallic(float metallic, float smoothness)
    {
        m_MetallicValue = Mathf.Clamp01(metallic);
        m_SmoothnessValue = Mathf.Clamp01(smoothness);
        SetColor(m_CurColor);  // 현재 색상에 새 금속성 재질 재적용
    }

    /// <summary>현재 메탈릭 값을 반환합니다. (디버깅/테스트용)</summary>
    public float GetMetallicValue() => m_MetallicValue;

    /// <summary>현재 스무스니스 값을 반환합니다. (디버깅/테스트용)</summary>
    public float GetSmoothnessValue() => m_SmoothnessValue;

    /// <summary>
    /// HSV 기반으로 채도와 명도가 높은 랜덤 차량 색상을 설정합니다.
    /// </summary>
    public void SetRandomColor()
    {
        float hue = Random.value;
        float saturation = Random.Range(0.5f, 1.0f);
        float brightness = Random.Range(0.6f, 1.0f);
        Color randomColor = Color.HSVToRGB(hue, saturation, brightness);
        randomColor.a = 1.0f;

        m_CurColor = randomColor;
        SetColor(randomColor);
    }

    /// <summary>
    /// 기본 색상으로 복원합니다.
    /// </summary>
    public void ResetColor()
    {
        m_CurColor = m_DefaultColor;
        SetColor(m_DefaultColor);
    }

    /// <summary>
    /// 현재 적용 중인 색상을 반환합니다.
    /// </summary>
    public Color GetCurrentColor() => m_CurColor;

    /// <summary>
    /// 색상 변경 대상으로 발견된 Renderer 수를 반환합니다. (디버깅/테스트용)
    /// </summary>
    public int GetTargetRendererCount() => m_TargetRenderers.Count;
}
