---
name: parking-design
description: Park3D(언리얼 5 주차장) 프로젝트의 구현 전 설계서 작성 방법. 신규 기능·변경에 착수하기 전 요구사항·클래스/데이터 구조·인터페이스·처리 흐름·대안 비교를 설계서로 확정할 때 반드시 사용. CLAUDE.md 0번 규칙(설계 필수)을 수행. architect 에이전트 전용.
---

# Parking Design — 설계 스킬

CLAUDE.md **0번 규칙(설계 필수)**을 수행한다. 코드 작성 전에 설계서를 확정한다. 설계 없이 구현 착수 금지.

## 선조사 (설계 전)
- `Park3D/Source/`에서 관련 기존 클래스(`PresetMakerWidget`, `ParkingPresetManager`, `ParkingPresetTypes`) 구조·관례 파악.
- `Docs/`에서 선행 작업 문서 확인.
- `unity/PresetMaker/` 원본(C# `CPresetMakerDlg`, `CPMakerParkSpaceUI`, `CFaceRect`, `CLineQubeBox`)을 참조해 포팅 정합성 확보.

## 설계서 구성 (6개 필수)
| # | 항목 | 내용 |
|---|------|------|
| 1 | 요구사항 | 요청을 명확한 기능 요구사항·제약·완료조건으로 분해 |
| 2 | 클래스/데이터 구조 | 신규/변경 클래스·멤버, `FParkingPreset`/`FParkingPresetDatas` 영향, JSON 스키마 변경 |
| 3 | 인터페이스 | 함수/메서드 시그니처, 위젯↔매니저 호출 관계(`RefreshView`→`RebuildAll`) |
| 4 | 처리 흐름 | 단계별 알고리즘 + 좌표/단위 규약 적용 |
| 5 | 대안 비교 | 2안 이상이면 트레이드오프 비교 후 권장안 |
| 6 | 테스트 포인트 | qa-verifier가 검증할 핵심 케이스 예고 |

## 좌표/단위 규약 (설계에 반드시 반영)
- 미터→cm (×100). Unity `(x, y_up, z)` → UE `(x, z, y_up=Z)`.
- 개별 면 회전(faceRot, 면 로컬 Z) ↔ 그룹 회전(groupRot, Origin 기준 Z) 분리.
- 사선 보정: `Default + |cos|>0` → 간격 `폭/cos(faceRot)`. 방향 반전: 정규화 각도 > 180°.

## 테스트 가능 설계 원칙
좌표 변환·JSON 직렬화·생성 계산 등 핵심 로직은 UObject/Actor에서 분리한 순수 함수로 설계한다(유닛 테스트 용이성 = 설계 품질).

## 출력
`_workspace/{phase}_architect_design.md`에 6개 구성으로 작성. 이 설계서는 impact-analyst(사전 영향 검토)·unreal-implementer(구현 기준)·qa-verifier(테스트 기준)·doc-writer(문서화 소스) 모두의 입력이 된다. 모호한 요구사항은 추측하지 말고 "가정/미확정"으로 표기하거나 오케스트레이터에 질의.

## 코드 미작성
설계서는 "무엇을/왜/어떻게"까지. 실제 코드는 unreal-implementer가 이 설계서를 받아 작성한다.
