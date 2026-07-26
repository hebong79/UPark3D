# 사후 영향도 분석 (rpcserver Phase 3 — cam)

작성일: 2026-07-25

## 변경/신규 파일
| 파일 | 변경 |
|---|---|
| `Rpc/Modules/CamRpcModule.h/.cpp` | 신규(cam.* 18: 13 실동작 + 5 미구현) |
| `Rpc/RpcModuleSupport.h/.cpp` | GetCameraManager + CamToDto 추가 |
| `Rpc/RpcServerSubsystem.h/.cpp` | cam 모듈 생성·등록·해제 |
| `Tests/RpcServerTest.cpp` | CamModule 테스트 1건 추가 |

## 의존성/회귀
- 전부 추가형. 기존 public API 무변경. **매니저/액터 코드 수정 없음**(기존 `ACameraControlManager`/`APTZCameraActor` API를 그대로 호출만 함) → car/preset보다 회귀면이 더 좁다.
- catalog 56 → **74**(cam 18). system.catalog 동적 반영.

## 데이터 권위
- cam은 `ACameraControlManager`(`Cameras` 배열, 기존 존재)를 그대로 권위로 사용. 위젯(카메라 컨트롤)과 **동일 매니저 공유** → preset처럼 목록 분기 문제 없음(car/cam은 단일 월드 권위, preset만 위젯 별도 목록).

## 미구현/제약
- 미구현 5(-32000): captureJPG/PNG(캡처 인코딩 파이프라인), savePreset/loadPreset/applyPreset(per-camera 프리셋 메모리).
- pan/tilt는 `Capture->GetRelativeRotation()` → RotatorToPanTilt 로 추출(정규화 [-180,180] 계열). Unity localEuler [0,360)와 표현 차이 가능(값 의미는 동일).
- DTO name은 `Camera-{camId}` 계산값(액터에 name 필드 없음).

## 검증
- 빌드 성공(경고 없음, 첫 시도) + 자동화 7개(신규 CamModule 포함) 통과 + HTTP 스모크(catalog 74, cam create/setPTZ/getPTZ 왕복, captureJPG -32000).
