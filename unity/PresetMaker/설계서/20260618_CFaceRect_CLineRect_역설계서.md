# CFaceRect / CLineRect 역설계 설계서 (Unreal 포팅용)

> 대상: `CFaceRect` (상속 부모 `CLineRect` 포함)
> 경로: `Assets/Scripts/00_Base/Common/CFaceRect.cs`, `Assets/Scripts/00_Base/CLineRect.cs`
> [PresetMakerDlg 역설계서](PresetMakerDlg_역설계서.md)의 **가장 깊은 레이어(§6-3) 전용 상세판**.
> 이 두 클래스가 "주차면 라인 1개"와 "3D 큐브의 한 면"을 실제로 **LineRenderer 점으로 그리는 최하단 모듈**이다. 언리얼 포팅에서 라인/사각형 렌더링을 어떻게 대체할지 결정하는 핵심 문서.

---

## 0. 한눈에 보기

- **`CLineRect`** : LineRenderer로 **닫힌 사각형(5점)** 을 그리는 범용 기반 클래스. 4꼭짓점을 `m_Points`에 보관.
- **`CFaceRect : CLineRect`** : 주차면 전용 확장. (1) **바닥 주차면 1칸**, (2) **3D 큐브의 한 면** 두 용도로 쓰이며, 각각 다른 정적 팩토리로 생성된다. 회전 폭 계산 유틸 포함.

```
MonoBehaviour
└─ CLineRect          (m_Points, LineRenderer, MakeRect/SetPosition/SetLineColor)
   └─ CFaceRect       (m_Id, m_faceRot, 색/두께, 2개 정적 팩토리, SetPositionWorld, CalculateRotatedWidth)
```

호출처:
- `CPMakerParkSpaceUI.MakeFaceRect()` → `CFaceRect.CreateFaceRect()` (바닥 주차면)
- `CLineQubeBox.Initialize/DrawQubeLines()` → `CFaceRect.CreateLineRect()` (큐브 4면)

---

## 1. CLineRect — 닫힌 사각형 라인 기반 클래스

`Assets/Scripts/00_Base/CLineRect.cs`

### 1-1. 필드

| 필드 | 타입 | 의미 |
|------|------|------|
| `m_idx` | int | 사각형 인덱스(기본 -1) |
| `m_Points` | List\<Vector3\> | **사각형 4꼭짓점(로컬 기준)** — 이후 모든 계산의 원천 |
| `m_LineRenderer` | LineRenderer (protected) | 실제 렌더러 |

`Start()`에서 `GetComponent<LineRenderer>()`로 캐시. `GetLineRenderer()` 접근자 제공.

### 1-2. `MakeRect` 오버로드 3종

**① `MakeRect(lr, posA, posB, posC, posD, lineWidth=0.1)`** — 4점 직접 지정
```
lr.useWorldSpace = false       // 로컬좌표 → 부모 이동/회전에 종속
lr.positionCount = 5
lr.loop = true
lr.startWidth = lr.endWidth = lineWidth
lr.startColor = lr.endColor = Color.red   // 기본 빨강(이후 SetLineColor로 덮음)
SetPosition 0..3 = A,B,C,D / 4 = A         // 5번째로 닫음
```

**② `MakeRect(lr, List<Vector3> points, lineWidth=0.05)`** — 4점 리스트
- `Debug.Assert(points.Count == 4)`. 위 ①과 동일하게 5점(마지막=points[0])으로 닫음.
- ⚠️ 코드에 `if (points.Count != points.Count) return;` 라는 **항상 false인 무의미 가드**가 있다(원래 의도는 `points.Count != 4`로 추정). 이식 시 정상 검증으로 교체.

**③ `MakeRect(lr, xSize, zSize, y=0.02, lineWidth=0.05)`** — 크기로 생성 ⭐
```
m_LineRenderer = lr
A = (-xSize/2, y, -zSize/2)
B = (-xSize/2, y,  zSize/2)
C = ( xSize/2, y,  zSize/2)
D = ( xSize/2, y, -zSize/2)
m_Points = [A, B, C, D]        // 원점 중심, XZ 평면, 위에서 볼 때 CCW
MakeRect(lr, m_Points, lineWidth)   // ②로 위임
```
→ **주차면 사각형의 표준 생성 경로.** 중심이 원점이므로 `transform.position`으로 배치하고 `transform.localRotation`으로 회전한다.

### 1-3. 기타 메서드

| 메서드 | 역할 |
|--------|------|
| `SetPosition(Vector3 worldPos)` | `transform.position = worldPos` (이름과 달리 라인 점이 아닌 **오브젝트 위치** 설정) |
| `static CreateLineRect(parent, xSize, zSize, y=0.05)` | "LineRect" GO + LineRenderer + CLineRect 생성, `MakeRect(③)` 호출. ⚠️ 머티리얼 경로 **`"Matreials/RectLine"` (오타)** — CFaceRect는 `"Materials/RectLine"` 사용. 둘 다 `Resources.Load` |
| `SetLineColor(Color)` | `GetComponent<LineRenderer>().material.color = color` |

> ⚠️ **머티리얼 경로 오타**: `CLineRect.CreateLineRect`만 `Matreials`(오타), 나머지는 `Materials`. `CFaceRect`가 부모의 이 정적 메서드를 쓰지 않으므로 실제 영향은 없으나, 이식 시 혼동 주의.

---

## 2. CFaceRect — 주차면/큐브면 전용 확장

`Assets/Scripts/00_Base/Common/CFaceRect.cs`

### 2-1. 동봉 enum

```csharp
enum EFaceDirType { Default, Dir }   // 면 배치 진행 방향 (Default=월드축, Dir=면 로컬축)
enum EGroupPosType { eNone=-1, eDown=0, eUp, eCenterDown, eCenterUp, eLeft, eRight }
```
- `EFaceDirType` : `MakeFaceRect`/`MoveByFaceDirType`의 분기 키 (PresetMakerDlg 문서 §6 참조).
- `EGroupPosType` : 1번 카메라 기준 주차라인 위치 분류. **CFaceRect.m_ePosType 필드로만 존재, 현재 코드에서 거의 미사용** — 이식 우선순위 낮음.

### 2-2. 필드

| 필드 | 기본값 | 의미 |
|------|--------|------|
| `DParkFaceLineWidth` (const) | 0.1 | 주차면 라인 두께 표준값 |
| `m_Id` | 0 | 면/프리셋 식별 인덱스 |
| `m_faceRot` | 0 | 개별 주차면 회전(Y deg) |
| `m_ePosType` | eNone | 그룹 위치 타입(미사용 수준) |
| `m_LineColor` | (0,240,130,255) 녹색 | 라인 기본색 |
| `m_LineWidth` | 0.05 | 라인 두께 |

### 2-3. ⭐ 두 가지 생성 경로 (용도 분리 핵심)

CFaceRect는 **서로 다른 두 정적 팩토리**로 만들어지며 용도가 완전히 다르다. 이름이 헷갈리니 주의.

#### ① `CreateFaceRect(parent, xSize, zSize, yPos=0.05, lineWidth=0.05)` — **바닥 주차면 1칸**
```
GO "LineRect" 생성, localRotation = identity
go.tag = "ParkFace"
LineRenderer + CFaceRect 부착
MakeRect(lr, xSize, zSize, yPos, lineWidth)   // 부모 ③ → m_Points 4점 생성
material = Resources.Load("Materials/RectLine"), color = white
go.layer = "ParkFace" (없으면 0)
```
- 크기 기반(xSize×zSize) 사각형. **태그·레이어 `ParkFace`** → 마우스 피킹/충돌 분류에 사용.
- 호출처: `CPMakerParkSpaceUI.MakeFaceRect`.

#### ② `CreateLineRect(parent, index, lineWidth=0.02)` — **3D 큐브의 한 면(라인)**
```
GO "FaceRect" 생성
LineRenderer + CFaceRect 부착
Initialize(lr, index, lineWidth)
```
- 크기 없이 빈 라인으로 생성 → 이후 `SetPositionWorld(점리스트)`로 실제 점을 채움.
- 호출처: `CLineQubeBox`(아래/위/앞/뒤 4면).

> ⚠️ **이름 충돌 주의**: `CLineRect.CreateLineRect(parent, xSize, zSize, y)`(크기 기반)와 `CFaceRect.CreateLineRect(parent, index, lineWidth)`(인덱스 기반)는 **시그니처가 다른 별개 메서드**. 언리얼 이식 시 명확히 다른 이름으로 분리 권장(예: `CreateFloorFace` / `CreateEdgeLine`).

### 2-4. `Initialize(lr, index=0, lineWidth=0.02)` — 빈 라인 셋업
```
m_Id = index
m_LineRenderer = lr (없으면 GetComponent)
positionCount = 0           // 비워둠
startWidth = endWidth = lineWidth
useWorldSpace = false       // Canvas/로컬 좌표 추종 (주석: "Canvas UI 좌표계")
material = new Material("Universal Render Pipeline/Unlit"), color = m_LineColor
```

### 2-5. `SetPositionWorld(List<Vector3> list)` — 점 직접 채우기(큐브면용)
```
m_Points.Clear()
for i in list: m_Points.Add(list[i]); positionCount=i+1; SetPosition(i, list[i])
positionCount = m_Points.Count + 1
SetPosition(4, m_Points[0])   // 닫음 (4점 가정 → 5번째로 첫 점 반복)
```
- ⚠️ 마지막 줄이 **인덱스 4 하드코딩** → 점이 정확히 4개일 때만 올바르게 닫힌다(큐브 1면=4점 전제). 가변 점수에는 안전하지 않음.

### 2-6. ⭐ `CalculateRotatedWidth(width, height, fAngle)` — 사선 폭 보정
```
angle = fAngle * Deg2Rad
return width / cos(angle)
```
- 사각형을 `fAngle`로 사선 회전 배치할 때 **이웃 면과 겹치지 않도록 늘려야 할 폭**.
- ⚠️ **`height` 인자는 실제로 사용되지 않는다**(시그니처에만 존재). 호출부는 `CalculateRotatedWidth(xSize, zSize, rot)` 형태로 넘기지만 zSize는 무시됨.
- ⚠️ `cos(angle)≈0`(±90°)이면 0-나눗셈/발산 → 호출부(`MakeFaceRect`)에서 `|cos|>0.001` 가드 후 호출.
- 이 식은 `CParkingGeometry`의 `stepW = width/cos`와 **동일** → 이식 시 한 함수로 통일 권장.

---

## 3. 좌표·렌더링 규약 정리 (포팅 체크리스트)

1. **로컬 4점, 원점 중심**: `A(-x/2,y,-z/2) B(-x/2,y,z/2) C(x/2,y,z/2) D(x/2,y,-z/2)`. XZ 평면, Y는 살짝 띄움(주차면 0.05). 위에서 볼 때 CCW.
2. **닫힌 사각형 = 5점** (마지막에 A 반복). LineRenderer는 `loop=true`도 쓰지만, 명시적으로 5번째 점을 찍는 경로도 있음.
3. **`useWorldSpace=false`**: 라인 점은 로컬좌표. 배치/회전은 `transform.position`/`localRotation`으로. → 부모(`preset_line`) 회전·이동에 자연히 종속.
4. **두 용도**: 바닥면(크기 기반, 태그/레이어 ParkFace) vs 큐브면(점 직접 주입). 동일 클래스지만 생성·채움 방식이 다름.
5. **머티리얼**: URP Unlit. 바닥면은 `Resources/Materials/RectLine`, 큐브/Initialize는 런타임 `new Material(URP Unlit)`.
6. **회전 폭 보정**: `width/cos(angle)` (height 무시, ±90° 가드 필요).

---

## 4. 언리얼 포팅 매핑

| Unity 요소 | Unreal 권장 대응 |
|------------|------------------|
| `LineRenderer` (닫힌 사각형) | ① 바닥면: **Decal** 또는 평면 ProceduralMesh + 라인 / ② 와이어: `ULineBatchComponent`·`DrawDebugLine` 또는 Spline+SplineMesh |
| `m_Points` (로컬 4점) | `TArray<FVector>` 로컬 꼭짓점 |
| `MakeRect(xSize,zSize,y)` | 4꼭짓점 생성 함수 (원점 중심, XZ↔UE에선 XY/평면축 매핑 주의) |
| `CreateFaceRect` (바닥, 태그 ParkFace) | 주차면 액터/컴포넌트 + Collision Channel "ParkFace" |
| `CreateLineRect`(index) + `SetPositionWorld` | 큐브 엣지 라인 컴포넌트 + 동적 점 주입 |
| `SetLineColor` | 동적 머티리얼 인스턴스 `SetVectorParameterValue` |
| `useWorldSpace=false` | 컴포넌트 로컬 트랜스폼에 라인 부착(부모 종속) |
| `CalculateRotatedWidth` | `UBlueprintFunctionLibrary` static (width/cos, ±90° 가드) |
| URP Unlit / `Materials/RectLine` | Unlit Material 에셋 |
| `EFaceDirType` / `EGroupPosType` | `UENUM` (EGroupPosType는 미사용 — 생략 가능) |

> **렌더링 방식 결정 포인트**: Unity는 바닥 주차면도 LineRenderer(선)로 그린다. 언리얼에서는 (a) 동일하게 선으로 갈지, (b) Decal/메시로 면을 그릴지 초기에 결정해야 한다. 차량/카메라 인식 정확도가 중요한 프로젝트 성격상 **실측 라인 폭이 보이는 방식** 유지를 권장.

---

## 5. 이식 시 정리해야 할 코드 이슈 (역설계 중 발견)

| # | 위치 | 이슈 | 권장 |
|---|------|------|------|
| 1 | `CLineRect.MakeRect(②)` | `if (points.Count != points.Count) return;` — 항상 false 가드(버그) | `!= 4` 검증으로 교체 |
| 2 | `CLineRect.CreateLineRect` | 머티리얼 경로 오타 `"Matreials/RectLine"` | `"Materials/RectLine"`로 통일 |
| 3 | `CFaceRect.CalculateRotatedWidth` | `height` 인자 미사용 | 시그니처에서 제거 또는 실제 사용 |
| 4 | `CFaceRect.SetPositionWorld` | `SetPosition(4, ...)` 인덱스 하드코딩(4점 전제) | `m_Points.Count` 사용 |
| 5 | `CreateLineRect` 이름 충돌 | 부모/자식 동명이종 메서드 | 용도별 명확한 이름 분리 |
| 6 | `EGroupPosType` | 정의만 있고 사실상 미사용 | 이식 보류 |

> 위는 **역설계 관찰 결과**이며, 요청 범위(설계서)이므로 코드 수정은 하지 않았다. 필요 시 별도 작업으로 처리 가능.

---

### 부록. 파일 인덱스
| 클래스 | 경로 |
|--------|------|
| CLineRect | `Assets/Scripts/00_Base/CLineRect.cs` |
| CFaceRect / EFaceDirType / EGroupPosType | `Assets/Scripts/00_Base/Common/CFaceRect.cs` |
| (호출처) CPMakerParkSpaceUI | `Assets/Scripts/01_PresetMaker/CPMakerParkSpaceUI.cs` |
| (호출처) CLineQubeBox | `Assets/Scripts/01_PresetMaker/CLineQubeBox.cs` |
