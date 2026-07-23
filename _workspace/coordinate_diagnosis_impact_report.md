# Unity JSON 좌표 불일치 영향도 보고서

- 조사일: 2026-07-20 15:51:42
- 상태: 구현 전 진단. 코드/에셋/JSON 변경 없음.

## 근본 원인

현재 Park3D는 Unity 위치를 아래의 **축 교환식**으로 변환한다.

```
현재: Unity(x, y, z) -> UE(x, z, y)
```

이는 Unity `+X(오른쪽)`를 UE `+X(전방)`으로, Unity `+Z(전방)`를 UE `+Y(오른쪽)`으로 보낸다. 즉, 높이 축만 올바르게 옮기고 바닥 평면의 전방/오른쪽 의미를 서로 뒤바꾼다.

동일한 공간 방향을 보존하는 변환은 다음이다.

```
목표: Unity(x, y, z) -> UE(z, x, y)
```

현재식의 바닥 2D 변환 `(x,z)->(x,z)`은 Unity 양의 Yaw 회전 행렬과 UE 양의 Yaw 회전 행렬의 방향이 반대가 되게 한다. 하지만 코드는 `rotY`/`faceRot`/`groupRot`을 그대로 UE Yaw로 사용한다. 따라서 0도만이 아니라 사선/회전 프리셋에서 위치ㆍ진행방향ㆍ회전까지 한 규약으로 맞지 않는다.

## 근거

| 영역 | 근거 | 판정 |
|---|---|---|
| Unity 프리셋 | `unity/PresetMaker/CPMakerParkSpaceUI.cs:184,205,239`에서 JSON `offsetPos`를 Unity 월드 `(x,z)` 위치로 직접 사용 | JSON은 Unity 좌표 의미를 보존 |
| Unity 차량 | `unity/CarObject/CCarObjListUI.cs:126-127,156-157`에서 JSON `pos`/`rotY`를 Unity Transform에 직접 적용 | 차량도 같은 Unity 좌표 의미 |
| UE 프리셋 | `Park3D/Source/Park3D/PresetMakerWidget.cpp:919-920`에서 `FVector(x,z,y)`로 로드 | 잘못된 평면 축 매핑 |
| UE 주차면 생성 | `ParkingPresetManager.cpp:84-91`이 `Offset.X`를 UE X, Unity Z에 대응한 `Offset.Y`를 UE Y로 취급 | 프리셋 전체가 같은 오류를 상속 |
| UE 차량 | `CarPlacementLibrary.cpp:12-15`가 `FVector(x,z,y)`를 반환하고 `CarActor.cpp:49-58`이 그대로 적용 | 차량도 동일한 오류를 상속 |
| UE 카메라 | `CameraControlLibrary.cpp:11-14`도 같은 `FVector(x,z,y)` 변환 | 향후 카메라 JSON도 동일한 공간 불일치 위험 |
| 프리셋 시점 값 | `001_Preset_Seo_1.json`에는 `camPos/camRot/fov`가 있으나 `ParkingPresetTypes.h:95-106` DTO에는 해당 필드가 없음 | 프리셋 로드만으로 Unity와 같은 카메라 시점이 재현되지 않음 |

## 예제 수치 (m -> cm)

| JSON 값 | 현재 UE `(x,z,y)` | 의미 보존 UE `(z,x,y)` |
|---|---:|---:|
| 프리셋 1 `(-7.367, 0, 19.176)` | `(-736.7, 1917.6, 0)` | `(1917.6, -736.7, 0)` |
| 차량 1 `(-7.291647, -0.000145, 18.634543)` | `(-729.2, 1863.5, -0.0145)` | `(1863.5, -729.2, -0.0145)` |

기본 Park3D 카메라는 `Yaw=0`이고 `+X`를 바라보므로 화면의 오른쪽은 대체로 `+Y`이다(`Park3DGameMode.h:46-50`). 현재 변환은 Unity의 오른쪽 `+X`를 화면 깊이인 UE `+X`로 보내고 Unity의 전방 `+Z`를 화면 수평인 UE `+Y`로 보낸다. 그래서 사용자에게는 좌/우가 바뀌거나 뒤틀린 형태로 보인다. 어느 쪽이 '왼쪽'으로 보이는지는 당시 카메라 Yaw/Pitch에 따라 달라지지만, 원인은 카메라가 아니라 공통 축 변환식이다.

추가로 예제 프리셋의 `camPos/camRot/fov`는 현재 프리셋 DTO에서 읽지 않는다. 따라서 화면의 어느 쪽에 보이는지까지 Unity와 일치시키려면, 축 변환 교정과 별도로 같은 카메라 시점을 적용해야 한다. 이 항목은 월드 위치가 틀어지는 1차 원인과는 구분된다.

## 영향도와 회귀 위험

| 면 | 위험도 | 영향 |
|---|---|---|
| 프리셋 JSON | 높음 | 기존 Unity 파일을 다시 로드하면 모든 `offsetPos`의 월드 위치가 교정되어 배치가 이동한다. |
| 차량 JSON | 높음 | 기존 차량의 위치가 프리셋과 함께 교정되어야 한다. 한쪽만 교정하면 차량과 면의 상대관계가 깨진다. |
| 회전 | 높음 | 사선 `faceRot`, `groupRot`, 차량 `rotY`의 실제 방향 검증이 필요하다. 목표식 `(z,x,y)`에서는 Unity 양의 Yaw와 UE 양의 Yaw를 같은 부호로 유지할 수 있다. |
| 카메라 JSON | 높음 | `CameraControlLibrary`도 같은 식이므로 위치/타깃을 함께 교정하지 않으면 화면 비교 기준이 계속 어긋난다. |
| Build/헤더/Blueprint/에셋 | 낮음 | 진단만 했으므로 현재 변경 없음. 실제 수정 시 C++ 함수의 계약은 유지 가능하지만 Automation 기대값과 기존 저장 파일의 표시 결과는 갱신되어야 한다. |

## 현재 테스트의 한계

`CarPlacementLibraryTest.cpp:41-45`와 `PresetMakerJsonTest.cpp:49-52`는 현재식 `(x,z,y)`을 정답으로 고정한다. 라운드트립은 같은 잘못된 정방향/역방향 함수를 왕복하므로 통과한다. 따라서 이 테스트들은 JSON 파싱과 수학적 역변환만 검증하며, Unity와 Unreal 사이의 실제 오른쪽/전방 방향 보존은 검증하지 못한다.

## QA 인계 항목 (수정 시)

1. Unity의 원점, `+X`, `+Z` 표식 3개를 같은 카메라 방향으로 UE에서 비교한다.
2. `001_Preset_Seo_1.json`과 `CarPos_1Preset_7Num.json`을 함께 로드해 각 차량이 원래 슬롯 중심에 놓이는지 확인한다.
3. `faceRot/groupRot/rotY`가 0°, 90°, 30°, 270°일 때 선/차량의 전면과 진행 방향을 비교한다.
4. 프리셋ㆍ차량ㆍ카메라 JSON을 Unity -> UE -> Unity로 저장했을 때 값과 화면 방향이 모두 유지되는지 확인한다.
