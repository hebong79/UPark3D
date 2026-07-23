---
name: impact-analysis
description: Park3D 프로젝트의 변경이 빌드 모듈·헤더 의존성·위젯↔매니저 연동·기존 기능·에셋 참조에 미치는 영향을 분석하는 방법. 구현 전 위험 사전 분석, 구현 후 회귀 영향 점검 시 반드시 사용. impact-analyst 에이전트 전용.
---

# Impact Analysis — 영향도 분석 스킬

CLAUDE.md 4번 규칙을 수행한다. 변경의 파급을 식별·보고한다.

## 분석 대상 (Park3D 의존성 면)
| 면 | 확인 방법 |
|----|-----------|
| 빌드 모듈 | `Park3D/Source/Park3D/Park3D.Build.cs`의 모듈 의존(Json, JsonUtilities, UMG 등) 추가/제거 영향 |
| 헤더 의존성 | 변경 헤더(`ParkingPresetTypes.h` 등)를 include하는 곳을 `find_references`/Grep으로 추적 |
| 위젯↔매니저 | `PresetMakerWidget` ↔ `ParkingPresetManager`(`RefreshView`→`RebuildAll`) 연동 시그니처 |
| 블루프린트→C++ | C++ 베이스 클래스를 상속한 BP 위젯/액터의 바인딩 깨짐 여부 |
| 에셋 참조 | 머티리얼·위젯 BP·데이터 에셋 레퍼런스 (`find_references`, `get_asset_info`) |
| JSON 호환성 | `FParkingPreset`/`FParkingPresetDatas` 필드 변경이 기존 `preset.json` 로드를 깨뜨리는지 |

## 분석 절차
1. **양방향**: (사전) 변경 대상의 참조처를 먼저 추적해 위험 경고 → unreal-implementer에 전달. (사후) 실제 변경된 인터페이스의 파급 점검.
2. **근거 제시**: 영향 주장마다 `파일:라인`·심볼 근거를 단다.
3. **위험도 분류**: 높음(기존 동작/스키마 파괴 가능) / 중간(재빌드·재컴파일 필요) / 낮음(국소 변경).
4. **회귀 시나리오**: 깨질 수 있는 기존 동작을 구체적으로 서술(예: "boxSizeZ 타입 변경 시 기존 preset.json 역직렬화 실패").

## 도구
- `find_references`, `find_assets`, `get_asset_info`, `search_class_paths`, `search_parent_classes`
- Grep (헤더 include/심볼 사용처)

## 출력
`_workspace/{phase}_impact_report.md`: 영향받는 파일/모듈 목록, 위험도, 회귀 시나리오, **qa-verifier에 전달할 중점 검증 항목**. 위험 발견 시 unreal-implementer에 `SendMessage`로 즉시 경고. 분석 한계 영역은 명시.
