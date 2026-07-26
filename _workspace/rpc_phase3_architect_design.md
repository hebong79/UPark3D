# Park3D RPC 서버 Phase 3 설계서 (cam.*)

작성일: 2026-07-25
참조: `unity/20260724_224837_RPC_전체_API_레퍼런스.md` §9(cam 18)
전제: Phase 1~2 서버 코어 재사용. 모듈 1개(cam) 추가 등록. catalog 56→69.

## 1. 범위
- cam.*(18) = **13 실동작** + **5 미구현(-32000)**.
- 미구현: `captureJPG`/`capturePNG`(렌더타깃 캡처 인코딩 파이프라인 부재), `savePreset`/`loadPreset`/`applyPreset`(per-camera 프리셋 메모리 + apply 의미 부재).

## 2. 백엔드 (기존 재사용, 신규 액터 없음)
- 권위: `ACameraControlManager`(`Cameras` 배열 = car/preset 패턴). AddCamera/RemoveCameraAt/GetCamera/GetCameraCount/SelectCamera/SelectedIndex.
- 액터: `APTZCameraActor`(SetCameraWorldLocation/SetPanTilt/SetZoom/GetZoom, `Capture` 공개).
- pan/tilt 추출: `UCameraControlLibrary::RotatorToPanTilt(Capture->GetRelativeRotation())`.
- 좌표: `UCameraControlLibrary::UnrealMetersToWorld/WorldToUnrealMeters`.

### camId 규약
Unity camId는 1-based, 매니저 Cameras는 0-based. **camId = index + 1**. 핸들러가 index = camId-1로 변환. 범위 밖이면 -32000.

## 3. DTO (CamDto)
`{camId, name, pos{x,y,z}, pan, tilt, zoom}`:
- camId = index+1, name = `Camera-{camId}`(Unity 자동명 규약; 액터에 name 필드 없음).
- pos = `WorldToUnrealMeters(Cam->GetActorLocation())`(Unreal 미터 월드).
- pan/tilt = RotatorToPanTilt(Capture 상대회전), zoom = Cam->GetZoom().

## 4. 핸들러 매핑
| method | 결선 | 상태 |
|---|---|---|
| create | AddCamera(name) → {camId=GetCameraCount()} | ✅ |
| delete | RemoveCameraAt(camId-1) → {ok}(≤1이면 false) | ✅ |
| select | SelectCamera(camId-1) → {ok} | ✅ |
| list | Cameras 순회 → {cameras:[CamDto]} | ✅ |
| get | CamDto | ✅ |
| setPosition | pos(미터)→UnrealMetersToWorld→SetCameraWorldLocation | ✅ |
| setHeight | height(m)→worldZ, XY 유지 | ✅ |
| setPTZ | SetPanTilt(pan,tilt)+SetZoom(zoom) | ✅ |
| getPTZ | {pan,tilt,zoom} | ✅ |
| setPan | 현재 tilt 유지 + SetPanTilt | ✅ |
| setTilt | 현재 pan 유지 + SetPanTilt | ✅ |
| setZoom | SetZoom | ✅ |
| setFOV | Capture->FOVAngle 직접 대입 | ✅ |
| captureJPG/capturePNG | 렌더타깃 픽셀 읽기+JPG/PNG 인코딩 파이프라인 부재 | ✗ -32000 |
| savePreset/loadPreset/applyPreset | per-camera 프리셋 메모리+apply 의미 부재 | ✗ -32000 |

## 5. 대안 비교
| 항목 | 채택 | 대안 | 사유 |
|---|---|---|---|
| 권위 | 기존 ACameraControlManager | 신규 | 이미 Cameras 배열 보유 |
| pos 좌표 | 월드→Unreal 미터 | Unity 로컬/월드 혼합 | 단순·일관(문서 명시) |
| 캡처/프리셋 | -32000 | 부분 근사 | 파이프라인 부재, 정직 |

## 6. 테스트 포인트
- create→list(1)→get(camId 1)→setPTZ(pan90,tilt10,zoom2)→getPTZ(근사 일치)→setZoom→delete(1대면 false).
- setPosition→get pos 반영. captureJPG/savePreset → -32000.
- HTTP: catalog 69, cam.create/list/getPTZ.

## 7. 좌표/단위
pos·height 입력은 Unreal 미터. pan/tilt deg, zoom 배율(1~36). setFOV는 수평 FOV(도) 직접.
