# streamlight — 사전 영향도 분석

- 날짜: 2026-08-06 / 브랜치: `fix/stream-indirect-light`
- 변경: `APTZCameraActor` 생성자 1줄 (`Capture->bAlwaysPersistRenderingState = true`) + 유닛 테스트 1파일

## 1. 변경 표면

| 파일 | 변경 | 성격 |
|---|---|---|
| `Park3D/Source/Park3D/PTZCameraActor.cpp` | 생성자에 `bAlwaysPersistRenderingState = true` (+주석 3줄) | 컴포넌트 기본값 |
| `Park3D/Source/Park3D/Tests/PTZCameraCaptureStateTest.cpp` | 신규 Automation 테스트 | 테스트 전용 |

헤더 변경 없음 → 의존 모듈 재컴파일 파급 없음. `Park3D.Build.cs` 변경 없음(`Engine` 이미 의존).
공개 시그니처·UPROPERTY·직렬화 스키마 변경 없음 → 에셋/세이브 호환성 영향 없음.

## 2. 이 변경을 타는 호출 지점 (전수)

`APTZCameraActor::Capture` 를 캡처하는 곳은 4군데뿐이며 전부 같은 컴포넌트를 공유한다.

| 호출 지점 | 카메라 선택 상태 | 변경 전 | 변경 후 |
|---|---|---|---|
| `CamRpcModule.cpp:45` DoCapture (`cam.captureJPG`/`capturePNG`) | 항상 선택 전환 후 캡처 | 뷰 스테이트 있음(정상) | 동일(정상) — **회귀 없음** |
| `CamStreamSubsystem.cpp:353` ProduceJpeg (카메라별 `/stream`) | 선택 안 건드림 | 뷰 스테이트 없음(결함) | 유지됨 → **수정 대상** |
| `MjpegStreamManager.cpp:161` ProduceFrame (레거시 `/stream`) | 선택 안 건드림 | 뷰 스테이트 없음(결함) | 유지됨 → **부수 수정** |
| `CameraControlManager.cpp:139` SelectCamera 직후 1회 캡처 | 선택 직후 | 뷰 스테이트 있음 | 동일 |

즉 정상 동작하던 경로(`cam.captureJPG`, 뷰어 위젯)는 이미 뷰 스테이트를 갖고 있었으므로
동작이 바뀌지 않는다. 바뀌는 것은 결함이 있던 비선택 캡처뿐이다.

## 3. 위험

| 위험 | 근거 | 완화 |
|---|---|---|
| GPU 메모리 증가 | 뷰 스테이트는 카메라별 temporal 히스토리/Lumen 캐시를 잡는다(1280x720 기준 카메라당 수십 MB 추정) | 할당은 지연(lazy) — 한 번도 캡처되지 않은 카메라는 뷰 스테이트를 만들지 않는다. 동시 캡처는 슬롯(`ActiveSlots`/`HardMaxSlots`)으로 이미 제한됨. QA 에서 다수 카메라 시 메모리 관찰 |
| 첫 프레임 수렴 지연 | Lumen 히스토리가 비어 있는 첫 캡처는 여전히 어두울 수 있음 | 설계 5장 명시. 스트림은 연속 캡처라 0.5~1초 내 수렴. `cam.captureJPG` 는 선택 카메라라 기존과 동일 |
| 프레임 비용 증가 | 간접광이 실제로 계산되므로 캡처 1프레임 비용이 늘 수 있음 | 기존 측정 기준 캡처 1프레임 약 48ms. QA 에서 `cam.streamStatus` 의 `fps` 로 전후 비교 |
| 카메라 삭제 시 누수 | 액터 파괴 시 컴포넌트가 뷰 스테이트를 해제하는지 | 엔진 `USceneCaptureComponent::OnUnregister`/소멸자가 `ViewStates.Destroy()` 수행 — 별도 코드 불필요 |

## 4. 영향 없음이 확인된 영역

- 좌표/단위 규약, JSON 저장·로드 스키마, 프리셋/차량/맵 로직
- RPC 메서드 목록·시그니처 (`cam.*` 전부 그대로)
- 스트림 포트 대역/슬롯 스케줄링/`config_pmaker.json`
- UMG 위젯 레이아웃 및 `CameraViewerWidget` 표시 경로(선택 카메라 RT 사용 → 기존과 동일)

## 5. 빌드 영향

- 재컴파일 대상: `PTZCameraActor.cpp`, 신규 테스트 1파일. 헤더 무변경 → 전체 재빌드 불필요.
- Live Coding 핫컴파일 가능 범위(구현부 .cpp 변경).
