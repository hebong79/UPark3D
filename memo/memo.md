# 작업 메모 — 2026-07-29

## 오늘 한 일 (4건, 전부 확인 완료)

### 1. 카메라 뷰어 부팅 시 상시 출력
- 증상: 앱 시작 시 흰 사각박스만 보이고, 카메라 컨트롤 UI를 열어야 카메라뷰가 나옴.
- 원인 2중
  - `ACameraControlManager`가 컨트롤 위젯에서만 lazy 스폰 → 패널 미개방 시 매니저·카메라·렌더타겟이 아예 없음.
  - 그 상태에서 `NativeTick`의 `RT != AppliedRT.Get()`이 `nullptr != nullptr` = false →
    `SetRenderTarget(nullptr)`이 한 번도 안 불려 Collapsed 처리가 누락 → UImage 기본 흰 브러시 잔상.
- 수정: `ACameraControlManager::GetOrSpawn()` + `EnsureDefaultCamera()` 추가,
  `APark3DGameMode::BeginPlay`에서 `ShowCameraViewer()` **직전에** 호출. 뷰어 이미지는 Collapsed로 시작.
- 초기 자세는 컨트롤 패널 슬라이더 기본값과 동일(높이 5m / X·Z 0 / pan·tilt 0 / zoom 1) → 패널을 열어도 화면이 안 바뀜.
- 문서: `Docs/20260729_162653_카메라뷰어_부팅시_렌더타겟_상시출력.md`

### 2. 뷰어 외곽 프레임 사각형 위치 어긋남 (패키지/독립실행 전용)
- 원인: `NativePaint`에서 `GetCachedGeometry()`(tick 공간 = **데스크톱 좌표**)를 그리기(**윈도우 좌표**)에 사용.
  PIE는 두 공간이 거의 일치해 정상으로 보였고, 패키지에서 약 (283,157) 밀려 창 밖으로 잘림.
- 수정: `GetPaintSpaceGeometry()`로 교체.
- 주의: 같은 파일의 마우스·커서 핸들러는 `GetScreenSpacePosition()`(데스크톱)과 비교하므로
  `GetCachedGeometry()`가 **맞다**. 건드리지 말 것.

### 3. 차량 배치 "열기" 시 차량 1개만 나옴 (패키지 전용)
- 결정적 증거는 **패키지 앱 로그**(`Package/Windows/Park3D/Saved/Logs/Park3D*.log`)에 있었다.
  사용자가 연 파일이 `Save/3D/Preset/001_Preset_Cam1.json` — 주차면 **프리셋** 파일(항목 1개)이었다.
- 원인 A(직접): 차량/프리셋 JSON의 **루트 키가 똑같이 `datas` 배열**이라
  `FJsonObjectConverter`가 조용히 성공하고 기본값 차량을 만든다.
- 원인 B(유발): `GetDefaultCarFilePath()`가 `ProjectDir()/Save/3D/CarPos`를 쓰는데
  패키지는 `Save/`가 **스테이지 루트**(`<Stage>/Save`)에 있어 그 경로가 없음 → 대화상자가 엉뚱한 곳에서 열림.
- 수정: `LooksLikeCarDatas()` 스키마 판별 추가(`prefabId`/`prefabName`/`rotY`/`isFront`),
  신규 `Park3DDataPaths.h`로 `Save/` 실제 위치 해석(차량·카메라·프리셋 3개 위젯 공통).
- 문서: `Docs/20260729_174846_패키지빌드_3건_불일치_원인분석_수정.md`

### 4. 카메라 뷰 기본 크기 = 화면 가로 40%
- 슬롯 폭(로컬 단위) → 화면 픽셀 변환 계수를 **계산으로 못 구한다**. 두 번 빗나감:
  - `GetViewportSize` ÷ `GetViewportScale` → 화면 26%
  - 루트 위젯 로컬 폭 × 0.4 → 화면 49%
- 채택: 화면비를 **실측**해 `새 폭 = 현재 폭 × (목표비율 / 현재비율)`로 보정(렌더 폭은 슬롯 폭에 선형).
- **1회 보정으로는 안 됨** — 첫 틱 지오메트리가 미정착이라 화면비를 0.167로 오측정(실제 ~0.205).
  허용오차 0.5% 안에 들 때까지 매 틱 재보정 → 실제 2회면 수렴. 드래그 시작 시 즉시 중단.
- 결과: 로그 `화면비 0.4000`, 실측 509px / 1280px ≈ 40%.
- 비율은 `DefaultViewWidthRatio`(EditAnywhere, 기본 0.4)로 조정 가능.
- 문서: `Docs/20260729_184533_카메라뷰어_기본크기_화면가로40퍼센트.md`

---

## 변경 파일

| 파일 | 구분 |
|------|------|
| `Park3D/Source/Park3D/CameraControlManager.h` / `.cpp` | 수정 |
| `Park3D/Source/Park3D/Park3DGameMode.cpp` | 수정 |
| `Park3D/Source/Park3D/CameraViewerWidget.h` / `.cpp` | 수정 |
| `Park3D/Source/Park3D/CarPlacementLibrary.cpp` | 수정 |
| `Park3D/Source/Park3D/Park3DDataPaths.h` | **신규** |
| `Park3D/Source/Park3D/CarPlacementWidget.cpp` | 수정 |
| `Park3D/Source/Park3D/CameraControlWidget.cpp` | 수정 |
| `Park3D/Source/Park3D/PresetMakerWidget.cpp` | 수정 |
| `Park3D/Source/Park3D/Tests/CameraControlManagerTest.cpp` | **신규** |
| `Park3D/Source/Park3D/Tests/CameraViewerWidgetTest.cpp` | 테스트 추가 |
| `Park3D/Source/Park3D/Tests/CarPlacementLibraryTest.cpp` | 테스트 추가 |
| `.claude/settings.json` | 승인 규칙 일반화 |
| `.claude/skills/park3d-auto-approve/SKILL.md` | **신규** |
| `CLAUDE.md` | 변경 이력 1행 추가 |

**신규 테스트**: `Park3D.CameraControl.EnsureDefaultCamera`,
`Park3D.CarPlacement.LoadRejectsNonCarJson`, `Park3D.CameraViewer.DefaultSize`

**검증**: 빌드 성공 / Automation **48건 전부 통과, 실패 0** / 재패키징 BUILD SUCCESSFUL.

---

## 상태

- **소스 변경은 아직 커밋하지 않음.**
- 패키지 최신 산출물: `Package/Windows/Park3D/Binaries/Win64/Park3D.exe` (2026-07-29 18:xx).
- 저장된 뷰어 크기 파일 2개를 **삭제함**(기본값 40% 확인용).
  `Park3D/Saved/CameraViewer/ViewerSize.json`(폭 814.21), 패키지 쪽(807.33).
  이전 크기가 필요하면 뷰어를 한 번 리사이즈하면 다시 저장된다.

## 남은 확인 항목

1. 차량 열기 **대화상자의 시작 폴더**가 실제로 `<Stage>/Save/3D/CarPos`로 열리는지 육안 확인 필요.
   (전제 조건은 확인했으나 네이티브 대화상자를 자동 조작하지 못해 미검증)
2. **차량 배치 패널이 1280×720 창에서 좌측 화면 밖으로 벗어남**. 이번 신고 대상이 아니라 미수정.
   더 큰 창에서는 재현되지 않을 수 있음.
3. 프리셋·카메라 파일 열기에는 아직 스키마 검증이 없다. 차량과 같은 혼동이 가능.
4. 실행 중 창 크기를 바꿔도 뷰어 기본 크기는 재보정하지 않는다(최초 1회만).

## 재발 방지 메모

- **패키징은 항상 `-build -cook -stage -pak`을 함께 돌린다.**
  콘텐츠만 재쿠킹하면 exe가 낡은 채 남아 C++ 변경이 통째로 빠진다(이번 1번 신고의 정체: exe 7/27 + pak 7/28).
- 패키지 전용 이상 신고를 받으면 **먼저 산출물 타임스탬프**와 소스 변경 시점을 비교한다.
- 패키지 런타임 문제는 `Package/Windows/Park3D/Saved/Logs/Park3D*.log`에 사용자 재현 흔적이 남아 있다. 먼저 볼 것.
- 에디터가 켜져 있으면 Live Coding이 디스크 빌드를 막는다. 신규 `.cpp` 추가는 핫컴파일로 반영되지 않는다.
- 패키징 중 ZenServer(`[::1]:8558`) 연결 거부로 `BUILD FAILED`가 날 수 있다(쿠킹은 성공, 스테이징에서 실패). 재실행하면 된다.
