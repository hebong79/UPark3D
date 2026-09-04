# Unreal Engine 오브젝트를 NVIDIA Omniverse용 USD로 변환하는 방법

> 작성일: 2026-09-04  
> 목적: Unreal Engine의 차량·오브젝트·레벨을 USD로 내보내 NVIDIA Omniverse, Isaac Sim 또는 Omniverse Kit 기반 앱에서 사용하는 방법을 정리합니다.

---

## 1. 결론

권장 파이프라인은 다음과 같습니다.

```text
Unreal Engine
  ↓ Unreal 기본 USD Exporter
원본 USD
  ↓ 0.01 크기 보정용 Wrapper USD
Omniverse / Isaac Sim / ov-stage
  ↓
RTX 렌더링
```

최신 Unreal Engine에서는 NVIDIA의 별도 Omniverse Unreal Connector보다 Unreal 기본 `USD Importer` 플러그인을 우선 사용하는 것이 안전합니다.

NVIDIA 공식 문서에서 Omniverse Unreal Connector가 지원한다고 표시된 Unreal Editor 버전은 Windows용 UE 5.2와 5.3입니다. 반면 메모보드의 실제 검증 사례에서는 UE 5.8과 Unreal 기본 `USDImporter` 플러그인을 사용해 차량과 전체 레벨 변환에 성공했습니다.

---

## 2. 메모보드에서 확인한 실검증 기록

| 메모 | 확인된 내용 |
|---|---|
| #124 | UE 차량 오브젝트 → USD → Isaac Sim/Omniverse 렌더링 성공 |
| #153 | Unreal 레벨 전체 → USD 변환 성공 |
| #143 | Unreal 기반 주차장 시뮬레이터를 Omniverse로 이전한 후속 기록 |

주요 검증 결과는 다음과 같습니다.

- Unreal 차량 에셋을 USD로 내보내고 Omniverse에서 렌더링하는 데 성공했습니다.
- 전체 주차장 레벨 325 Actors, 808 Materials를 USD로 내보내는 데 성공했습니다.
- Unreal의 센티미터 단위 콘텐츠를 Omniverse의 미터 단위 Stage에 Reference할 때 Wrapper Prim에 `0.01` Scale이 필요했습니다.
- 재질 굽기 없이 전체 레벨 Export는 약 49초, 512×512 Material Baking을 적용하면 약 18분이 걸렸습니다.
- Commandlet 방식의 Material Baking에서는 `-NoTextureStreaming`이 중요했습니다.
- Decal과 일부 SplineMesh Component는 빈 Xform으로 변환될 수 있었습니다.

---

## 3. Unreal Engine 플러그인 활성화

Unreal Editor에서 다음 순서로 진행합니다.

```text
Edit
 └─ Plugins
     └─ 검색: USD
```

다음 플러그인을 활성화합니다.

- **USD Importer**
- 자동 변환이 필요하면 **Python Editor Script Plugin**

활성화한 뒤 Unreal Editor를 재시작합니다.

| 플러그인 | 용도 |
|---|---|
| USD Importer | USD 가져오기 및 내보내기 |
| Python Editor Script Plugin | 여러 오브젝트를 자동으로 일괄 내보내기 |

---

## 4. 단일 오브젝트를 USD로 내보내기

### 4.1 Content Browser의 원본 에셋 내보내기

Static Mesh 등 원본 에셋을 내보낼 때 사용합니다.

1. **Content Browser**에서 대상 Static Mesh를 선택합니다.
2. 마우스 오른쪽 버튼을 누릅니다.
3. **Asset Actions → Export**를 선택합니다.
4. USD 파일 형식을 선택합니다.
5. 처음에는 `.usda` 형식으로 저장합니다.

예:

```text
Sonata.usda
```

### USD 확장자 선택

| 확장자 | 특징 | 권장 용도 |
|---|---|---|
| `.usda` | 사람이 읽을 수 있는 텍스트 형식 | 최초 시험과 오류 분석 |
| `.usdc` | 바이너리 형식 | 최종 배포와 빠른 로딩 |
| `.usd` | 일반 USD 확장자 | 일반적인 사용 |
| `.usdz` | 관련 파일을 하나로 묶은 패키지 | 파일 전달 |

처음에는 참조 경로와 속성을 직접 확인할 수 있는 `.usda`가 편리합니다.

### 4.2 레벨에 배치된 Actor 내보내기

레벨 안에서 위치·회전·크기가 적용된 오브젝트를 내보낼 때 사용합니다.

1. 변환할 Unreal 레벨을 엽니다.
2. **World Outliner**에서 대상 Actor를 선택합니다.
3. 차량이 여러 부품으로 구성되었다면 관련 Actor를 모두 선택합니다.
4. **File → Export Selected**를 선택합니다.
5. USD 형식으로 저장합니다.

이 방법은 다음 정보를 함께 보존하는 데 적합합니다.

- 레벨상의 위치
- 회전
- Scale
- 오브젝트 계층 구조
- 여러 Static Mesh의 배치 관계

---

## 5. 레벨 전체를 USD로 내보내기

1. 변환할 Unreal 레벨을 엽니다.
2. **File → Export All** 또는 해당 버전의 레벨 Export 메뉴를 선택합니다.
3. USD 형식을 선택합니다.
4. USD Export Options를 설정합니다.
5. 먼저 작은 시험 레벨을 내보냅니다.
6. 정상 확인 후 실제 레벨을 내보냅니다.

메모보드 #153의 실측 결과:

| 항목 | 결과 |
|---|---:|
| 레벨 | `LV_Park_01` |
| Actor 수 | 325개 |
| Material 수 | 808개 |
| Material Baking 없음 | 약 49초 |
| 512×512 Material Baking | 약 18분 |

### 권장 시험 순서

```text
1차: Material Baking OFF
2차: 선택한 오브젝트만 256×256
3차: 문제가 없으면 512×512
4차: 꼭 필요한 주요 오브젝트만 1024 이상
```

---

## 6. Material Baking

Unreal Material Graph는 Omniverse로 그대로 복사되지 않을 수 있습니다. 다음과 같은 기능은 특히 주의해야 합니다.

- 복잡한 Material Graph
- Runtime Virtual Texture
- Material Parameter Collection
- Unreal 전용 Material Function
- Blueprint에서 실시간으로 변경되는 재질
- 일부 투명·유리 재질
- World Position Offset
- Decal Material

따라서 Omniverse에서 비슷한 모습을 유지하려면 Material Baking이 필요합니다.

### 권장 시작값

| 설정 | 권장값 |
|---|---|
| Material Baking | 활성화 |
| Texture 크기 | 512×512 |
| Base Color | 포함 |
| Normal | 포함 |
| Roughness | 포함 |
| Metallic | 포함 |
| Opacity | 투명 재질에만 포함 |
| Emissive | 발광 재질에만 포함 |

차량처럼 가까이 촬영되는 오브젝트는 최종적으로 1024 또는 2048 텍스처가 필요할 수 있지만, 변환 시험은 512부터 시작하는 것이 안전합니다.

---

## 7. Unreal과 Omniverse의 단위 차이

| 시스템 | 일반 단위 |
|---|---|
| Unreal Engine | 1 Unit = 1cm |
| Omniverse/Isaac 계열 Stage | 1 Unit = 1m |

Unreal에서 내보낸 콘텐츠를 Omniverse의 미터 기반 Stage에 Reference하면 오브젝트가 100배 크게 나타날 수 있습니다.

메모보드 #124와 #153에서는 다음 보정을 적용했습니다.

```text
Scale = 0.01
```

### 중요

원본 USD에 다음과 같은 메타데이터가 있어도:

```usda
metersPerUnit = 0.01
```

다른 USD Stage에서 이 파일을 Reference할 때 참조된 Prim이 자동으로 축소된다고 가정하면 안 됩니다. 실검증에서는 상위 Wrapper USD에 명시적으로 `0.01` Scale을 적용해야 했습니다.

---

## 8. Wrapper USD 만들기

원본을 직접 수정하지 않고 Wrapper USD를 별도로 만드는 것이 안전합니다.

예를 들어 Unreal에서 다음 파일을 내보냈다고 가정합니다.

```text
D:/USD/Sonata/Sonata.usda
```

같은 폴더에 다음 파일을 만듭니다.

```text
D:/USD/Sonata/Sonata_Omniverse.usda
```

내용 예시:

```usda
#usda 1.0
(
    defaultPrim = "Sonata"
    metersPerUnit = 1
    upAxis = "Z"
)

def Xform "Sonata" (
    prepend references = @./Sonata.usda@
)
{
    double3 xformOp:scale = (0.01, 0.01, 0.01)
    uniform token[] xformOpOrder = ["xformOp:scale"]
}
```

Omniverse에서는 원본 대신 다음 Wrapper 파일을 엽니다.

```text
Sonata_Omniverse.usda
```

### Wrapper 방식의 장점

- Unreal에서 다시 Export해도 Wrapper 설정이 유지됩니다.
- 원본 USD를 훼손하지 않습니다.
- 크기 보정을 한곳에서 관리할 수 있습니다.
- Omniverse용 재질 Override를 추가할 수 있습니다.
- 위치·회전 보정을 별도 Layer로 관리할 수 있습니다.

---

## 9. 텍스처 경로 구성

USD만 복사하고 텍스처를 빠뜨리면 Omniverse에서 오브젝트가 회색 또는 흰색으로 나타날 수 있습니다.

권장 폴더 구조:

```text
Sonata/
├─ Sonata.usda
├─ Sonata_Omniverse.usda
├─ Materials/
│  └─ Sonata_Materials.usda
└─ Textures/
   ├─ body_basecolor.png
   ├─ body_normal.png
   ├─ body_roughness.png
   ├─ glass_basecolor.png
   └─ tire_normal.png
```

USD 내부의 텍스처 경로는 가능하면 상대 경로를 사용합니다.

```usda
@./Textures/body_basecolor.png@
```

다음과 같은 Windows 절대 경로는 피합니다.

```text
D:\UnrealProject\Content\Textures\body.png
```

절대 경로를 사용하면 Ubuntu RTX-3090 서버나 DGX에서 해당 파일을 찾지 못할 수 있습니다.

---

## 10. Omniverse에서 검증하기

변환된 Wrapper USD를 다음 프로그램 중 하나에서 엽니다.

- Omniverse USD Composer
- Isaac Sim
- Omniverse Kit 기반 앱
- `ovstage + ovrtx` 기반 프로젝트

### 기본 검증표

| 확인 대상 | 정상 기준 |
|---|---|
| 크기 | 승용차 길이가 약 4~5m |
| 방향 | 차량의 앞뒤 방향이 올바름 |
| 바닥 | 차량이 바닥 위에 위치 |
| Up Axis | Z축이 위쪽 |
| 재질 | 흰색 기본 재질로 변하지 않음 |
| 텍스처 | 누락·검정·이상 색상이 없음 |
| 유리 | 완전히 불투명하거나 사라지지 않음 |
| Normal | 표면이 뒤집히지 않음 |
| 계층 구조 | 차체·바퀴·번호판 Prim이 유지됨 |

### 차량 크기로 Scale 확인

```text
약 450m   → 0.01 Scale 누락
약 4.5m   → 정상
약 0.045m → 0.01 Scale 중복 적용
```

---

## 11. 메모보드에서 확인된 주요 함정

### 11.1 Material Baking 중 멈춤

Commandlet 방식으로 Material Baking을 수행할 때 다음 옵션이 중요했습니다.

```text
-NoTextureStreaming
```

메모보드 #153에서는 이 옵션이 없으면 Material Baking이 첫 번째 재질에서 사실상 멈추는 현상이 발생했습니다.

### 11.2 Decal과 SplineMesh

실측 결과:

- Instanced Plane Mesh로 만들어진 주차선은 정상 Export되었습니다.
- Decal Component는 빈 Xform으로 Export될 수 있습니다.
- SplineMesh Component는 빈 Xform으로 Export될 수 있습니다.

중요한 도로 표시나 주차선은 다음과 같이 실제 Mesh로 변환하는 것이 안전합니다.

```text
Decal
  ↓
Plane Static Mesh
  ↓
Material 적용
  ↓
USD Export
```

### 11.3 런타임 Spawn 오브젝트

Blueprint 또는 게임 실행 중 Spawn되는 차량은 레벨 원본에 없으므로 전체 레벨을 Export해도 포함되지 않습니다.

권장 구성:

```text
ParkingLevel.usda
Vehicles/
  ├─ Sonata.usda
  ├─ Avante.usda
  └─ Motorcycle.usda
```

Omniverse 앱에서 차량 USD를 Reference 또는 Payload로 동적으로 생성합니다.

### 11.4 검은 렌더 화면

메모보드 #153에서 검은 화면의 실제 원인은 재질이 아니라 다음 요소들이었습니다.

- Unreal의 `UDS Sky_Sphere`가 조명을 가림
- CCTV 시야각 표시용 원뿔 Mesh가 주차장을 덮음
- CineCamera가 카메라 하우징 Mesh 안에 위치
- Omniverse Stage에 적절한 DomeLight가 없음

검은 화면이 나오면 다음 Prim을 먼저 비활성화해 봅니다.

```text
Sky_Sphere
CameraPyramid
CameraFrustum
CameraHousing
DebugVolume
```

### 11.5 카메라 변환

실검증 레벨에서는 고정식 카메라와 PTZ 카메라의 위치 및 CineCamera 데이터가 USD에 포함되었습니다. 그러나 다음 항목은 별도로 확인해야 합니다.

- 카메라가 바라보는 방향
- Focal Length
- Horizontal/Vertical Aperture
- FOV
- Near/Far Clipping
- 카메라가 하우징 Mesh 내부에 묻혔는지

---

## 12. 자동 일괄 변환

차량 1~2대는 Unreal GUI로 내보내도 충분하지만, 차량 23대처럼 많으면 Unreal Commandlet와 Python을 사용하는 것이 좋습니다.

PowerShell 실행 구조 예시:

```powershell
UnrealEditor-Cmd.exe `
  "D:\프로젝트\MyProject.uproject" `
  -run=pythonscript `
  -script="D:\scripts\export_usd.py" `
  -AllowCommandletRendering `
  -EnablePlugins=PythonScriptPlugin,USDImporter `
  -NoTextureStreaming `
  -unattended `
  -nop4
```

| 옵션 | 의미 |
|---|---|
| `-run=pythonscript` | Unreal Python 스크립트 실행 |
| `-AllowCommandletRendering` | 재질 굽기와 렌더 관련 기능 허용 |
| `-EnablePlugins=PythonScriptPlugin,USDImporter` | 프로젝트 설정을 직접 수정하지 않고 필요한 플러그인 활성화 |
| `-NoTextureStreaming` | Material Baking 멈춤 방지 |
| `-unattended` | 사용자 팝업 없이 자동 실행 |
| `-nop4` | Perforce 연결 방지 |

### 자동화 시 주의점

- Commandlet 종료 코드만으로 성공 여부를 판단하지 않습니다.
- 출력 USD 파일의 실제 존재 여부를 확인합니다.
- JSON 결과 보고서를 별도로 저장합니다.
- 한글 에셋 이름이 Commandlet Python에서 깨질 수 있습니다.
- `print()` 출력이 사라질 수 있으므로 파일 로그를 사용하는 것이 안전합니다.

결과 보고서 예시:

```json
{
  "success": true,
  "source": "/Game/Vehicles/현대_쏘나타",
  "output": "D:/USD/Vehicles/Sonata.usda",
  "materialsBaked": true
}
```

---

## 13. 권장 작업 순서

### 1단계 — 차량 한 대 시험

```text
현대 쏘나타 Static Mesh 선택
→ Asset Actions
→ Export
→ Sonata.usda
```

### 2단계 — Wrapper 작성

```text
Sonata_Omniverse.usda
→ Sonata.usda Reference
→ Scale 0.01
```

### 3단계 — Omniverse에서 확인

```text
크기
방향
재질
텍스처
유리
Normal
```

### 4단계 — 차량 일괄 변환

```text
23개 차량
→ UnrealEditor-Cmd
→ Python 자동 Export
→ 차량별 Wrapper USD 생성
```

### 5단계 — 주차장 레벨 변환

```text
Material Baking OFF로 1차 Export
→ 구조와 크기 확인
→ 512×512 Baking으로 2차 Export
```

### 6단계 — Omniverse에서 동적 조립

```text
ParkingLevel.usda
  + Vehicle USD References
  + Camera Prims
  + Omniverse Lighting
```

---

## 14. 최종 체크리스트

- [ ] Unreal에서 `USD Importer` 플러그인을 활성화했습니다.
- [ ] 단일 에셋은 `Asset Actions → Export`로 시험했습니다.
- [ ] 레벨 배치 Actor는 `File → Export Selected`로 시험했습니다.
- [ ] 전체 레벨은 Baking OFF로 먼저 시험했습니다.
- [ ] Omniverse용 Wrapper USD에 `Scale 0.01`을 적용했습니다.
- [ ] 차량 길이가 약 4~5m인지 확인했습니다.
- [ ] 텍스처가 상대 경로로 연결되어 있습니다.
- [ ] Base Color, Normal, Roughness, Metallic을 확인했습니다.
- [ ] Decal과 SplineMesh 누락 여부를 확인했습니다.
- [ ] 런타임 Spawn 차량을 별도 USD 에셋으로 분리했습니다.
- [ ] 카메라 방향과 FOV를 확인했습니다.
- [ ] 일괄 Baking 명령에 `-NoTextureStreaming`을 넣었습니다.
- [ ] Commandlet 결과를 JSON 파일과 출력 USD 존재 여부로 확인했습니다.

---

## 15. 핵심 요약

```text
1. Unreal에서 USD Importer 플러그인을 활성화합니다.
2. 단일 에셋은 Asset Actions → Export를 사용합니다.
3. 배치 Actor는 File → Export Selected를 사용합니다.
4. 전체 맵은 File → Export All을 사용합니다.
5. Unreal Material은 512×512부터 Baking합니다.
6. Omniverse용 Wrapper USD에서 Scale 0.01을 적용합니다.
7. 텍스처는 상대 경로로 보관합니다.
8. Decal·SplineMesh·런타임 Spawn 차량은 별도로 처리합니다.
9. 대량 변환은 UnrealEditor-Cmd + Python을 사용합니다.
10. Material Baking Commandlet에는 -NoTextureStreaming을 넣습니다.
```

가장 먼저 할 작업은 차량 한 대를 `.usda`로 내보내고 Wrapper에서 `0.01` Scale을 적용한 뒤, Omniverse에서 차량 길이가 약 4~5m인지 확인하는 것입니다.

---

## 참고 자료

- NVIDIA Omniverse Connect — Unreal Engine: <https://docs.omniverse.nvidia.com/connect/latest/ue4.html>
- NVIDIA Omniverse Unreal Connector User Manual: <https://docs.omniverse.nvidia.com/connect/latest/ue4/manual.html>
- 내부 메모보드 관련 기록: #124, #153, #143
