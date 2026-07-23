# Park3D 데칼 주차면 결함 2건 수정 — 구현 로그 (미검증: 컴파일 게이트 대기)

작성: 2026-07-10 / 담당: unreal-implementer

## 요약
- 결함 원인: 선택 fill 데칼 기본 머티리얼 `/Game/M/Decal/바닥_이미지/MI_파란색`이 부모(M_Auto/M_Decal) 실종으로 파손 → placeholder 렌더.
- 라인 데칼(`MI_Decal_Line_Road_White_02`)은 정상이므로 무변경.

## A. 정상 fill 데칼 머티리얼 확보 (MCP MaterialTools) — 완료·검증됨
- 신규 마스터 머티리얼 생성: `/Game/M/Decal/M_ParkingSelectFill`
  - MaterialDomain = **MD_DeferredDecal**
  - BlendMode = **BLEND_Translucent**
  - VectorParameter `FillColor` 기본 (R=0, G=0.6, B=1, A=1) → **MP_EmissiveColor** 연결
  - ScalarParameter `Opacity` 기본 **0.3** → **MP_Opacity** 연결
  - recompile 성공(셰이더 에러 없음), save_assets=true 로 디스크 저장.
- 렌더 검증: `CaptureAssetImage` 썸네일 = 파란(시안) 반투명 구체. placeholder 체커 아님(반투명이라 배경 체커가 파랑 너머로 살짝 비침 = 정상).
  - 산출 이미지: `_workspace/parkingdecal_fix_matthumb.png`
- 비고: 마스터 머티리얼 방식 채택(차선 MI 방식 불필요) — Emissive 경로라 Albedo 텍스처 없이 solid color 출력.

## B. C++ 기본 경로 교체 (외과적) — 코드 완료, 재컴파일 필요
- 파일: `Park3D/Source/Park3D/ParkingPresetManager.cpp` 생성자
- 변경: `SelectFillDecalMaterial` FObjectFinder 기본 경로
  - 이전: `/Game/M/Decal/바닥_이미지/MI_파란색.MI_파란색`
  - 이후: `/Game/M/Decal/M_ParkingSelectFill.M_ParkingSelectFill`
  - Finder 실패 가드(Succeeded 체크) 유지, 경고 메시지 자산명만 갱신.
- 라인 머티리얼(`LineDecalMaterial`)·`PlaceFillDecal`·기타 로직 무변경.
- `PlaceFillDecal`의 CreateDynamicMaterialInstance 색 강제는 **생략**(머티리얼 기본색으로 충분, 단순함 우선).

## C. 헤드리스 Automation 테스트 추가 — 코드 완료, 재컴파일 필요
- 신규 파일: `Park3D/Source/Park3D/Tests/ParkingDecalTest.cpp`
- 테스트 1: `Park3D.ParkingDecal.ComputeSlotCorners` (정적 순수 함수 회귀)
  - Case1: 무회전 Offset(1,2)m Face0 → Bottom 손검산 좌표 일치
  - Case2: 같은 프리셋 Face1 → X로 Step(250cm) 이동 확인
  - Case3: 면회전 90° → (x,y)->(-y,x) 회전 반영
  - Case4: 그룹회전 90°(Origin 피벗) + 모든 코너 Z==FaceHeightZ(5) 보존
- 테스트 2: `Park3D.ParkingDecal.Rebuild` (에디터 월드 스폰)
  - 데칼 개수: 프리셋 면수 [2,3], 선택=1 → 가시 데칼 = (2+3)×4 + 3(선택면 fill) = 23 (fill 머티리얼 있을 때). bEnable=false → 0.
  - 두께: 선택 없음(라인만), T=10→DecalSize.Z=5, T=30→15 (half=T/2 비례).
  - 널가드: LineDecalMaterial/SelectFillDecalMaterial=null → 크래시 없이 가시 0.
  - 머티리얼 미로드 환경 대비 경고 후 스킵 가드 포함.

## 변경 파일 목록
1. `Park3D/Source/Park3D/ParkingPresetManager.cpp` (생성자 1곳)
2. `Park3D/Source/Park3D/Tests/ParkingDecalTest.cpp` (신규)
3. 에셋: `/Game/M/Decal/M_ParkingSelectFill` (신규, 저장 완료)

## 컴파일 게이트 (수동)
- B(생성자)·C(테스트)는 **C++ 재컴파일 필요** → 사용자가 에디터에서 **Ctrl+Alt+F11**(Live Coding) 실행.
- 나(구현자)는 컴파일을 직접 트리거하지 않음.

## 컴파일 후 QA 확인 항목 (qa-verifier)
- 두 Automation 테스트 실행: `Park3D.ParkingDecal.*` 모두 통과.
- PIE에서 매니저 RebuildDecals 실행 시 선택 프리셋 바닥에 **파란 반투명 fill 데칼**이 렌더되고 placeholder 아님을 스크린샷 확인(재컴파일 필요로 이번 라운드 미수행).

## 상태
구현 완료(미검증). A 머티리얼은 렌더까지 검증. B/C는 컴파일 게이트 대기.
