# [daylight] 사후 영향도 분석

확정 변경: `DirectionalLight` pitch −16.29→**−55.0**, Intensity 6.0→**20.0** / `SkyLight` Intensity 1.0→**1.5**

## 1. 실제 변경된 파일

```
Park3D/Content/__ExternalActors__/Maps/PresetMaker1/4/AC/NISAE1O44NDS0B9BGVRHMD.uasset   (DirectionalLight)
Park3D/Content/__ExternalActors__/Maps/PresetMaker1/**/*.uasset                          (SkyLight, 2차 적용 시 추가)
```

`.umap`·`Config/`·`Source/` 변경 **0건**. 코드·빌드 산출물에 영향 없음.

> 주의: `Park3D/Content/`는 `.gitignore:27`로 git 추적 대상이 아니다. 따라서 이 변경은 **커밋되지 않으며**, 버전 관리 밖에서 관리된다. 되돌리려면 원본 값(pitch −16.285559 / Intensity 6.0 / SkyLight 1.0)을 다시 적용해야 한다 — 값은 `_workspace/daylight_lightscan_before.log`에 보존돼 있다.

## 2. 사전 예측 대비 실제 결과

| 사전 영향도 예측 | 실제 | 일치 |
|-----------------|------|------|
| C++/빌드 영향 없음 | 소스 변경 0건, 테스트 60/60 통과 | ✅ |
| 라이팅 리빌드 불필요(Movable + `r.AllowStaticLighting=0`) | 값 적용 후 재기동만으로 즉시 반영 | ✅ |
| 카메라 뷰어가 함께 밝아짐(`SCS_FinalColorLDR`) | Cam1/Cam2 캡처 모두 밝아짐 | ✅ |
| RPC 79개 메서드 영향 없음 | 주변 동작 점검 전 항목 통과 | ✅ |
| `PP_FixedExposure` 보존 | 4개 값 변경 0건 | ✅ |

사전 분석과 실제 결과가 어긋난 항목 **없음**.

## 3. 회귀 검증 결과

| 대상 | 결과 |
|------|------|
| Automation `Park3D` 전체 | **60/60 성공, 0 실패** (`daylight_automation.log`) |
| 프리셋 생성·목록·데칼 리빌드 | 통과 (`preset.create`/`list`/`rebuildAll` 정상) |
| 차량 저장/로드 | 통과 (`car.save` → `car.load` 23대 왕복) |
| 측정 계열 | 통과 (`setTargetPoint`→`angles` 22.86°/`distance` 12.87m) |
| 캡처 계열 | 통과 (`cam.captureJPG`/`capturePNG` 정상 반환) |
| 랜덤 배치 | 통과 (`car.setRandomColor`, `random.recreateCars` 23대 `seedHonored`) |

## 4. 의도된 부수 변화 (설계 R-2)

태양 고도를 16.29°→55°로 올렸으므로 **대기 산란 결과가 함께 바뀐다.**

| 요소 | 변경 전 | 변경 후 |
|------|--------|--------|
| 하늘 색 | 일몰 붉은 기 (하늘 평균 루마 66.6~79.2) | 주간 밝은 회백색 (201.7~202.4) |
| `ExponentialHeightFog` | 저각도 산란으로 지평선이 붉게 뜸 | 흰 헤이즈로 전환 |
| `VolumetricCloud` | 어둡게 깔림 | 밝게 표현 |
| 그림자 방향 | yaw 43.73 | **동일** (yaw 미변경) |

"한낮"이라는 요구의 필연적 귀결이며, 그림자 방향은 보존해 기존 배치의 시각적 인상을 유지했다.

## 5. 잔여 위험

| # | 위험 | 심각도 | 대응 |
|---|------|-------|------|
| 1 | 변경이 git 추적 밖(`Park3D/Content/` 무시)이라 되돌림·이력 추적이 수동 | 중 | 원본 값을 `daylight_lightscan_before.log`와 본 문서에 명시 보존 |
| 2 | 실행 중인 패키지에는 미반영 — 재패키징 필요 | 중 | 사용자 확인 후 재패키징 (프로세스 종료 필요) |
| 3 | 야간·저조도 시나리오를 전제한 기능이 있다면 전제가 깨짐 | 낮 | 소스에 조명 의존 코드 없음 확인. 조명 프리셋/시간대 전환 기능 부재 |
| 4 | 카메라 노출이 고정이라, 앞으로 조명을 더 바꾸면 화면 밝기가 그대로 따라 움직임 | 낮 | 의도된 설계. 밝기 재조정 시 본 문서의 측정 절차 재사용 |
