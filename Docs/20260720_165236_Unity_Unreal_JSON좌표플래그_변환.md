# Unity–Unreal JSON 좌표 플래그 변환 Goal/Loop 결과

- 작성일시: 2026-07-20 16:52:36 (Asia/Seoul)
- 작성자: Codex doc-writer (Luna)
- 참고: [`json_coordinate_flag_goal_loop_design.md`](../_workspace/json_coordinate_flag_goal_loop_design.md), [`json_coordinate_flag_impact_report.md`](../_workspace/json_coordinate_flag_impact_report.md), [`json_coordinate_flag_implementer_changes.md`](../_workspace/json_coordinate_flag_implementer_changes.md), [`json_coordinate_flag_loop_iteration_1.md`](../_workspace/json_coordinate_flag_loop_iteration_1.md), [`json_coordinate_flag_loop_iteration_2.md`](../_workspace/json_coordinate_flag_loop_iteration_2.md), [`json_coordinate_flag_qa_report.md`](../_workspace/json_coordinate_flag_qa_report.md)

---

## 1. 목표와 최종 상태

프리셋 메이커, 차량 배치, 카메라 컨트롤 JSON의 좌표를 하나의 Unreal 미터 좌표계로 정규화했다. Unity에서 생성된 기존 파일은 루트 `isUnreal` 키가 없거나 `false`인 경우 한 번만 변환하고, 이미 `true`인 파일은 수치를 다시 변환하지 않는다. 저장 시에는 내부 Unreal 미터 좌표와 `isUnreal: true`를 함께 기록한다.

최종 파이프라인은 다음과 같다.

```text
JSON 역직렬화 → 루트 플래그 판정 → (legacy만 공통 변환)
→ 내부 Unreal 미터 → UI 또는 월드 경계 변환(cm) → isUnreal:true로 저장
```

## 2. 변경 파일

| 영역 | 파일 | 변경 내용 |
|---|---|---|
| 공통 변환 | [`UnityUnrealCoordinateConverter.h`](../Park3D/Source/Park3D/UnityUnrealCoordinateConverter.h), [`UnityUnrealCoordinateConverter.cpp`](../Park3D/Source/Park3D/UnityUnrealCoordinateConverter.cpp) | Unity/Unreal 미터 간 축 변환과 Unreal 미터→월드 센티미터 변환을 단일 소스로 제공 |
| DTO | [`ParkingPresetTypes.h`](../Park3D/Source/Park3D/ParkingPresetTypes.h), [`ParkingCarTypes.h`](../Park3D/Source/Park3D/ParkingCarTypes.h), [`CameraControlTypes.h`](../Park3D/Source/Park3D/CameraControlTypes.h) | 각 JSON 루트 DTO에 `isUnreal` 추가 |
| 프리셋 | [`PresetMakerWidget.h`](../Park3D/Source/Park3D/PresetMakerWidget.h), [`PresetMakerWidget.cpp`](../Park3D/Source/Park3D/PresetMakerWidget.cpp) | `offsetPos`의 legacy 경계 정규화와 UE 좌표 저장 적용 |
| 차량 | [`CarPlacementLibrary.h`](../Park3D/Source/Park3D/CarPlacementLibrary.h), [`CarPlacementLibrary.cpp`](../Park3D/Source/Park3D/CarPlacementLibrary.cpp), [`CarActor.cpp`](../Park3D/Source/Park3D/CarActor.cpp), [`CarPlacementWidget.cpp`](../Park3D/Source/Park3D/CarPlacementWidget.cpp), [`CarPlacementManager.cpp`](../Park3D/Source/Park3D/CarPlacementManager.cpp) | `pos` 로드, 액터/주차면 월드 적용, 피킹 및 재생성 경계를 UE 미터 기준으로 통일 |
| 카메라 | [`CameraControlLibrary.cpp`](../Park3D/Source/Park3D/CameraControlLibrary.cpp), [`CameraControlManager.cpp`](../Park3D/Source/Park3D/CameraControlManager.cpp), [`CameraControlWidget.cpp`](../Park3D/Source/Park3D/CameraControlWidget.cpp) | `FCamDir.pos` 정규화, 월드 적용, UI 표시·수집 축 순서 변환 |
| 테스트 | [`PresetMakerJsonTest.cpp`](../Park3D/Source/Park3D/Tests/PresetMakerJsonTest.cpp), [`CarPlacementLibraryTest.cpp`](../Park3D/Source/Park3D/Tests/CarPlacementLibraryTest.cpp), [`CarPlacementManagerTest.cpp`](../Park3D/Source/Park3D/Tests/CarPlacementManagerTest.cpp), [`CameraControlLibraryTest.cpp`](../Park3D/Source/Park3D/Tests/CameraControlLibraryTest.cpp) | legacy 변환, UE 플래그 무변환, 저장-재로드, 새 축 기대값과 월드 반영 검증 |

## 3. 좌표·단위 규약

Unity 위치 `(x, y, z)` 단위 미터를 Unreal 내부 위치 `(z, x, y)` 단위 미터로 변환한다. 즉 Unity의 바닥 평면 오른쪽/전방 의미를 Unreal의 해당 축에 보존한다. 월드 액터나 주차면에 적용하는 시점에만 Unreal 미터에 `100`을 곱해 센티미터로 바꾼다. 변환기에는 다음 세 경계를 둔다.

- Unity 미터 → Unreal 미터: `(x, y, z) → (z, x, y)`
- Unreal 미터 → Unreal 미터: `isUnreal=true`이면 항등(무변환)
- Unreal 미터 → Unreal 월드: 각 성분 `×100`

회전 `rotY`, 카메라 pan/yaw의 부호, 프리셋 `faceRot`/`groupRot`, 메시 전방 오프셋, 차량 후면 180도 보정, 카메라 tilt 부호 규칙은 유지했다.

카메라 UI의 기존 라벨 순서 `X/높이/Z`는 내부 Unreal 축과 다르므로 다음처럼 매핑한다.

| UI 필드 | 내부 Unreal 미터 |
|---|---|
| X | X |
| 높이 | Z |
| Z | Y |

## 4. 플래그 및 JSON 처리 로직

프리셋 루트의 `offsetPos`, 차량 루트의 `pos`, 카메라 루트 내부 모든 `FCamDir.pos`가 대상이다.

1. 역직렬화 직후 루트 `isUnreal`을 읽는다.
2. 키가 없거나 `false`이면 legacy Unity 좌표로 간주해 공통 변환기를 통해 한 번만 `(z,x,y)`로 정규화하고 내부 플래그를 `true`로 만든다.
3. `true`이면 수치 변환 없이 값을 내부 Unreal 미터로 사용한다.
4. 저장 함수는 복사본에 `isUnreal=true`를 강제하고 내부 Unreal 미터 값을 그대로 직렬화한다.

따라서 legacy 파일의 `load → save → second load`에서 두 번째 로드는 이미 Unreal 형식이므로 이중 변환이 발생하지 않는다. 기존 파일을 자동으로 덮어쓰지 않으며, 사용자가 저장할 때만 새 형식으로 승격된다.

## 5. 영향도

- **JSON 호환성:** 신규 루트 키를 추가했지만 누락을 `false`로 처리해 기존 Unity JSON을 계속 읽을 수 있다. 신규 저장 파일은 UE 좌표와 `isUnreal:true`를 갖는다.
- **프리셋/차량/카메라 연계:** 세 도메인의 좌표 경계를 동일 변환기로 통일했다. 주차면·액터·카메라 적용 시 미터→센티미터 변환 위치가 일관된다.
- **Blueprint/UMG:** 루트 DTO `UPROPERTY`가 추가되었으나 기존 BindWidget 이름과 함수 시그니처는 보존했다. 카메라 UI는 표시 순서만 내부 축 규약에 맞게 재매핑한다.
- **회전/에셋:** 회전 부호와 메시·카메라 오프셋 규칙을 변경하지 않아 기존 에셋 배치 계약을 유지한다.
- **문서 규약 충돌:** `AGENTS.md`의 오래된 `(x,z,y)` 설명과 이번 요구사항이 충돌한다. 이 Goal에서는 물리 방향 보존을 검증한 새 공통 규약 `(z,x,y)`를 단일 기준으로 삼았다.
- **환경 위험:** 최초 Live Coding 링크는 코드가 아닌 stale VS18 `14.50.35717` MSVC 응답 파일 경로 때문에 실패했다. 중간 산출물이나 설치 파일을 삭제·수정하지 않고 에디터 종료 후 전체 빌드로 우회했다.

## 6. 검증 결과

### 빌드 및 실행

- UE5.8, VS2022 MSVC `14.44.35207`로 `Park3DEditor Win64 Development` 전체 빌드를 수행했다.
- SharedPCH/C++ 컴파일과 `UnrealEditor-Park3D.lib` 및 `.dll` 링크를 포함한 6개 액션이 성공했고 UBT 결과는 `Succeeded`였다.
- 전체 빌드 DLL을 로드하도록 에디터를 재시작한 뒤 `StartPIE → IsPIERunning(true) → StopPIE`가 모두 통과했다.

### Unreal Automation

전체 10건, `passed=10`, `failed=0`.

| 검증 범위 | 결과 |
|---|---|
| 공통 변환기 빌드/UHT/링크 | 통과 |
| 프리셋·차량·카메라 legacy Unity 로드 | 통과 |
| `isUnreal:true` 무변환 재로드 | 통과 |
| legacy load→save→second load 무이중변환 | 통과 |
| `(z,x,y)` 좌표 라운드트립 및 차량/카메라 좌표 | 통과 |
| 액터 트랜스폼·매니저 재생성·주차면 코너 계산 | 통과 |
| PIE 시작·실행 상태·중지 | 통과 |

## 7. 실패·미검증·제약

- 첫 번째 Loop에서는 에디터 Live Coding 잠금(`Unable to build while Live Coding is active`)으로 외부 UBT가 C++ 컴파일 전에 중단되어 사전 검증이 미완료였다.
- 이후 수동 Live Coding은 C++ 컴파일 자체는 성공했지만, VS2022 `14.44.35207` 링크가 이전 VS18 `14.50.35717`의 삭제된 `MSVCRT.lib` 경로를 참조하는 `LNK1181`을 냈다. 이는 소스/반사/Blueprint 문제가 아닌 환경의 stale 응답 파일 문제로 분리했다.
- 에디터 종료 후 전체 빌드가 새 DLL 링크까지 성공해 해당 Goal의 빌드·Automation·PIE 검증을 완료했다. Live Coding 캐시 자체를 수동 복구한 것은 아니므로 동일 환경에서 Live Coding만 재시도할 때 같은 문제가 재발할 가능성은 남아 있다.
- UMG 파일 대화상자에 대한 합성 마우스 클릭 입력은 도구 제약으로 실행하지 못했다. 대신 로드·저장 핵심 경로는 Automation으로, 월드 적용은 액터/매니저/주차면 테스트와 PIE 기동으로 검증했다.

코드 및 테스트 파일은 이번 문서화 단계에서 수정하지 않았다. 중간 산출물은 `_workspace/`에 보존되어 있으며, 본 문서는 Goal/Loop 결과를 종합한 최종 기록이다.
