# RPC Phase 4 설계서 — measure.* (거리/각도 측정)

작성일: 2026-07-25
대상: `unity/20260724_224837_RPC_전체_API_레퍼런스.md` §12 measure.* (5)
전제: Phase 1~3 완성(74 노출). measure는 미등록 도메인 1순위(순수함수 존재 → 결선).

---

## 1. 요구사항

Unity `CPCamDistDlg`(카메라 거리/각도 대화상자)의 RPC 기능 5개를 Unreal에 결선한다.

| method | params | 응답 | 의미 |
|---|---|---|---|
| `measure.setTargetPoint` | `pos`{x,z 필수, y?=0} | `{ok:true}` | 타겟점 이동·활성화(이후 distance/angles 기준) |
| `measure.distance` | `camId`(필수) | `{horizontal, distance3d}` | 카메라↔타겟 수평거리/3D거리(m) |
| `measure.angles` | `camId`(필수) | `{vertical, horizontal}` | 수직각(양수=내려다봄)/수평각(양수=오른쪽) |
| `measure.cameraHeight` | `camId`(필수) | `{height}` | 하방 레이캐스트로 바닥까지 거리, 미검출 시 카메라 Z(m) |
| `measure.targetLine` | `start`{x,z}, `end`{x,z} | `{angleStart, angleEnd}` | 선택 카메라 기준 수선의 발(0°) 대비 시작/끝 방향각 |

**선후관계(Unity 예외 이식):**
- `distance`·`angles`는 `setTargetPoint` 선행 필수. 타겟 비활성 → `-32000`(Unity InvalidOperationException).
- `targetLine`은 `camId` 없이 **현재 선택 카메라**(`Mgr->SelectedIndex`) 사용. 카메라 0대 → `-32000`.
- `targetLine` `start≈end`(수평거리² < 1e-4 m) → `-32000`(Unity ArgumentException).
- `angles`의 수평각 기준방향은 Unity `m_vVerticalPosWithCam`(타겟라인 수선의 발). RPC도 **직전 `targetLine` 호출이 설정**하며, 미설정 시 원점(0,0,0) 기준(Unity 기본값과 동일 동작). → 문서에 명시.

## 2. 클래스/데이터 구조

신규 `FMeasureRpcModule : FRpcModuleBase`(경량 C++, 서브시스템 소유). **매니저/액터/라이브러리 코드 무수정** — 기존 순수함수 호출만.

세션 상태(모듈 멤버, Unity CPCamDistDlg 인스턴스 상태 대응):
```cpp
FVector TargetWorld = FVector::ZeroVector;   // 타겟점 UE 월드(cm)
bool    bTargetActive = false;               // setTargetPoint 호출 여부
FVector VerticalPosWithCam = FVector::ZeroVector; // 수평각 0° 기준점(targetLine이 설정)
```
> 모듈은 GameInstance 수명 동안 영속(car/cam과 동일) → UI 세션 상태를 그대로 보존.

## 3. 백엔드 매핑 (전부 기존 자산)

| 요소 | 사용 API |
|---|---|
| 카메라 조회 | `ACameraControlManager::GetCamera(camId-1)`, `GetCameraCount()`, `SelectedIndex`, `MetersToUU` |
| 카메라 위치 | `APTZCameraActor::GetActorLocation()`(=Root 본체=카메라 헤드, UE 월드 cm) |
| 좌표 변환 | `UCameraControlLibrary::UnrealMetersToWorld`(pos→월드), `WorldCentimetersToMeters`(거리 cm→m) |
| 거리 | `DistanceXZ`(UE Z 제거 수평), `Distance3D` |
| 각도 | `VertHorzAngleToTarget(Cam, Target, RefDirBase, vert, horz)` |
| 타겟라인 | `TargetLineAngles(Cam, Start, End, OutRefPoint, startDeg, endDeg)` |
| 바닥 트레이스 | `UWorld::LineTraceSingleByChannel(ECC_Visibility)` 하방(GetWorldPtr) |

**좌표 규약 확인**: 내부 "Unreal 미터"는 축 스왑 없이 UE 월드에 스케일만(×MetersToUU). 카메라(`cam.setPosition`)와 타겟점(`setTargetPoint`)이 동일 변환 → 동일 UE 월드 공간에서 비교. UE Z=높이 → `DistanceXZ`(Z 제거)=수평거리, `Cam.Z-Target.Z`=높이차. 일관.

## 4. cameraHeight 이식 방침

Unity: `m_SelectableLayer` 하방 Raycast → "Floor" 태그 히트 시 hit.distance, 미검출 시 camPos.y.
Unreal 현실: `AMapFloorActor`는 **NoCollision**(순수 시각), 지표면 콜리전은 Landscape(Z=0, ECC_Visibility). 따라서:
- 카메라 위치에서 하방(-Z)으로 `ECC_Visibility` 트레이스(길이 1e6cm).
- 히트 → `hit.Distance`(cm)→m. 미히트(헤드리스/랜드스케이프 없음) → `camWorld.Z`(cm)→m.
- 바닥이 Z=0이면 hit.Distance ≈ camZ → 두 경로 동일값(의미 보존). "Floor 태그" 필터는 하방 Visibility 트레이스로 대체(문서화).

## 5. 처리 흐름 (핸들러별)

```
setTargetPoint: RequirePosXZ(pos) → TargetWorld=UnrealMetersToWorld(pos,MetersToUU); bTargetActive=true → {ok:true}
distance:  RequireInt(camId); bTargetActive? else -32000; Cam=GetCamById; cw=Cam.Loc
           → {horizontal:m(DistanceXZ(cw,TargetWorld)), distance3d:m(Distance3D(cw,TargetWorld))}
angles:    RequireInt(camId); bTargetActive? else -32000; cw=Cam.Loc
           → VertHorzAngleToTarget(cw,TargetWorld,VerticalPosWithCam,v,h) → {vertical:v, horizontal:h}
cameraHeight: RequireInt(camId); cw=Cam.Loc; TraceDown → {height:m(dist|cw.Z)}
targetLine: RequirePosXZ(start,end); cnt=GetCameraCount()>0? else -32000;
            sw/ew=UnrealMetersToWorld; XZ거리²<1e-4? else -32000;
            Cam=GetCamera(SelectedIndex).Loc; TargetLineAngles(cw,sw,ew,ref,s,e);
            VerticalPosWithCam=ref → {angleStart:s, angleEnd:e}
```

MetersToUU = `Mgr->MetersToUU`(기본 100). camId → GetCamById 헬퍼(CamRpcModule과 동형, 범위 밖 -32000).

## 6. 대안 비교

| 안 | 내용 | 채택 |
|---|---|---|
| A. 신규 측정 서브시스템/액터 | 타겟점을 월드 액터로 스폰 | ✗ 과설계. RPC 측정은 순간 계산이라 세계 상태 불필요 |
| **B. 모듈 세션 상태 + 순수함수 (채택)** | 타겟점/기준점을 모듈 멤버로 보관, 계산은 CameraControlLibrary | ✅ Unity CPCamDistDlg 인스턴스 상태와 1:1, 코드 무수정 |
| C. 매 호출 pos 파라미터로 전달 | 상태 없이 distance(camId,pos) | ✗ 레퍼런스 스키마 위반(distance는 camId만 받음) |

## 7. 테스트 포인트 (자동화 `Park3D.Rpc.MeasureModule`)

1. cam 1대 생성(setPosition으로 위치 지정) → setTargetPoint → distance: horizontal/distance3d 양수, distance3d ≥ horizontal.
2. angles: vertical/horizontal 반환(수치 범위 [-180,180]).
3. distance/angles를 setTargetPoint **전** 호출 → -32000.
4. cameraHeight: 카메라 Z=Hm 설정 → height ≈ H(헤드리스 폴백 경로).
5. targetLine(start≠end) → angleStart/angleEnd 반환. start==end → -32000.
6. 미등록이던 measure.*가 catalog에 5개 추가(74→79).

## 8. 영향도 (사전)

- 전부 추가형. 신규 파일 2개 + 서브시스템 include/생성/등록 3줄 + 테스트 1건.
- 기존 public API·매니저·라이브러리 무변경 → 회귀면 극소(cam Phase와 동급, 오히려 상태가 모듈 로컬이라 더 좁음).
- catalog 74→79. `system.catalog` 동적 반영, 클라이언트 무수정.
- 리스크: (a) 각도 부호 규약(§11 가정) — VertHorzAngle/TargetLineAngles가 이미 유닛테스트로 검증된 함수라 재사용만. (b) cameraHeight 헤드리스에서 트레이스 미스 → camZ 폴백(설계상 정상).
