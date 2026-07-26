# 사후 영향도 분석 (rpcserver Phase 4 — measure)

작성일: 2026-07-25

## 변경/신규 파일
| 파일 | 변경 |
|---|---|
| `Rpc/Modules/MeasureRpcModule.h/.cpp` | 신규(measure.* 5: 전부 실동작) |
| `Rpc/RpcServerSubsystem.h/.cpp` | measure 모듈 include·멤버·생성·등록·해제(5줄) |
| `Tests/RpcServerTest.cpp` | MeasureModule 테스트 1건 + include 1줄 |

## 의존성/회귀
- 전부 추가형. 기존 public API·매니저·액터·라이브러리 **무변경**(cam Phase와 동급 회귀면, 상태가 모듈 로컬이라 더 좁음).
- catalog 74 → **79**(measure 5). `system.catalog` 동적 반영, 클라이언트 무수정.
- 신규 엔진 의존 없음(HTTPServer는 Phase 1에 이미 추가). Build.cs 무변경.

## 데이터 권위
- 카메라 = `ACameraControlManager`(`Cameras`) 기존 권위 그대로 사용(위젯과 공유).
- 타겟점/0°기준점(`VerticalPosWithCam`)은 **MeasureRpcModule 세션 멤버**(Unity CPCamDistDlg 인스턴스 상태 대응). 월드 액터 스폰 없음 → 씬 오염 0.

## 좌표 규약 (중요 — 교차 관측)
- 이 코드베이스의 pos 규약은 **JSON x·y = 지면(UE X·Y), JSON z = 높이(UE Z)**. `cam.setPosition`/`car.create`/`random`/`preset offset` 등 Phase 1~3 전 위치 메서드와 **동일**. measure도 동일 변환(`RequirePosXZ`+`UnrealMetersToWorld`)을 써서 카메라↔타겟이 같은 UE 공간에 놓임 → 측정 일관.
- **Unity 레퍼런스 원 규약(x·z 지면, y 높이)과는 축 대응이 다르다.** 이는 measure가 만든 문제가 아니라 Phase 1에서 정해진 프로젝트 전역 규약이다. measure만 다르게 매핑하면 sibling 메서드와 불일치가 생기므로 **동일 규약 유지가 올바른 선택**. Unity 클라이언트 원본 좌표(y=높이)를 그대로 쓰려면 전역 좌표 어댑터를 별도 과제로 도입해야 한다(측정 모듈 국소 수정 대상 아님).

## 미구현/제약
- measure 5개 전부 실동작(미구현 0). 도메인 최초로 -32000 항목 없음.
- `cameraHeight`: `AMapFloorActor`가 NoCollision이라 하방 `ECC_Visibility` 트레이스는 Landscape(Z=0)를 히트. 미검출(헤드리스/랜드스케이프 없음) → 카메라 Z 폴백. 바닥 Z=0이면 두 경로 동일값.
- `angles`의 수평각 0° 기준은 직전 `measure.targetLine` 이 설정(`VerticalPosWithCam`). targetLine 미호출 시 원점(0,0,0) 기준(Unity 기본값 동작과 동일). 수직각·거리는 기준 무관하게 항상 정확.

## 검증
- UBT 정식 빌드 성공(경고 0, 첫 시도).
- 자동화 `Park3D.Rpc.*` **8개** 전부 Success(신규 MeasureModule 포함, 0 Fail, 건너뜀 0).
- HTTP 스모크(실서버, method 79): distance `{horizontal:9, distance3d:15}`, angles `vertical:53.130°`(=atan2(12,9) 정확), cameraHeight `12`, targetLine 각도 반환, 동일점 targetLine → -32000.
