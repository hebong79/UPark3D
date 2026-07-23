---
name: unreal-implementation
description: Park3D(언리얼 5 주차장) 프로젝트의 C++/UMG/Blueprint/머티리얼 구현 및 Unreal MCP 에디터 제어 방법. 신규 기능 구현, 버그 수정, 리팩터링, 위젯/매니저 제작, JSON↔3D 생성 로직 작업 시 반드시 사용. unreal-implementer 에이전트 전용.
---

# Unreal Implementation — 구현 스킬

Park3D 프로젝트(Unity PresetMaker의 언리얼 5 C++ 포팅)에서 코드/에셋을 구현하는 방법.

## 프로젝트 좌표/단위 규약 (위반 시 3D가 깨진다)
- 단위: 미터 → cm (×100).
- 축 변환: Unity `(x, y_up, z)` → UE `(x, z, y_up=Z)`. 주차면은 UE XY평면, 높이는 Z축.
- 회전 분리: 개별 면 회전(faceRot, 면 로컬 Z축)과 그룹 회전(groupRot, Origin 기준 Z축)은 별개로 적용한다.
- 사선 보정: `Default + |cos|>0`이면 간격을 `폭/cos(faceRot)`로 확장(겹침 방지). 방향 반전은 정규화 각도 > 180°일 때.

## 구현 워크플로
0. **설계서 확인 (선행 필수)**: architect의 확정 설계서(`_workspace/*_architect_design.md`)를 먼저 읽고 그 인터페이스·데이터 구조·처리 흐름을 기준으로 구현한다. 설계서가 없으면(사소 변경 예외 외) 구현을 시작하지 말고 오케스트레이터에 설계 게이트 통과를 요청한다(CLAUDE.md 0번 규칙).
1. **기존 코드 확인**: `Park3D/Source/`에서 관련 클래스(`PresetMakerWidget`, `ParkingPresetManager`, `ParkingPresetTypes`)를 읽고 관례 파악. Unity 원본 클래스명을 참조 주석으로 남긴다.
2. **테스트 가능 설계**: 좌표 변환·JSON 직렬화·생성 계산 등 핵심 로직은 UObject/Actor에서 분리한 순수 함수/static 함수로 만든다(qa-verifier가 유닛 테스트하기 쉽도록).
3. **구현**: C++는 Edit/Write로, 에디터 조작은 Unreal MCP 도구로.
4. **빌드 검증**: 코드 변경 후 컴파일/빌드 통과 확인. C++ 클래스 추가/시그니처 변경 시 `Build.bat` 재빌드 필요.
5. **MCP 제약 우회**: OnClicked 등 일부 블루프린트 노드는 MCP로 생성 불가 → C++ 베이스 클래스에 핸들러를 만들고 위젯이 상속하게 한 뒤 재빌드. (메모리 `unreal-mcp-widget-control-limits` 참조)

## Unreal MCP 주요 도구
- 상태: `health_check`, `get_world_info`, `get_actors_in_level`, `find_actors_by_name`
- 블루프린트: `create_blueprint`, `add_component_to_blueprint`, `compile_blueprint`, `read_blueprint_content`
- 위젯: `add_widget`, `set_widget_properties`, `get_widget_tree`, `bind_widget_event`(가능 범위 내)
- 임의 실행: `execute_python` (MCP가 직접 지원 안 하는 작업의 만능 우회로)

## Build.bat 재빌드 워크플로
C++ 클래스/시그니처 변경 후:
```
Park3D/  에서 RunUAT 또는 프로젝트 Build.bat로 에디터 타겟 재빌드 → 에디터 Hot Reload/재시작 → MCP health_check 재확인
```

## 출력
변경 요약을 `_workspace/{phase}_implementer_changes.md`에 기록: 변경 파일 목록, 핵심 로직 요약, qa-verifier가 테스트할 함수/시나리오, impact-analyst가 볼 인터페이스 변경점.

## 완료 기준
빌드 통과 + 변경 요약 기록까지가 "구현 완료(미검증)". 실동작/테스트는 qa-verifier 검증을 거쳐야 최종 완료.
