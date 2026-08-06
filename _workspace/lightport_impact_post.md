# [lightport] 사후 영향도 분석

- 작업: 원본 프로젝트(`Parking_Project` / `LV_Park_01`)의 환경 조명 파라미터를 Park3D `PresetMaker1` 로 이식
- 기준 문서: `lightport_architect_design.md` (rev.2), `lightport_impact_pre.md`
- 분석 시점: 구현·빌드·회귀·재패키징 완료 후

## 1. 실제 변경 파일 (실측)

### 1-1. 저장소 코드/에셋

| 파일 | 성격 | 근거 |
|---|---|---|
| `Park3D/Source/Park3D/Light/LightControlTypes.h` | `FLightSettings` 기본값 6항목 | git |
| `Park3D/Content/__ExternalActors__/.../4/AC/NISAE1O44NDS0B9BGVRHMD.uasset` | DirectionalLight | 백업 대비 바이너리 비교 |
| `.../5/2S/IXGTX5HKJ3EVA66UM8VMA1.uasset` | SkyLight | 〃 |
| `.../D/V6/ZBHQD03M08IOQAL5HTQMD3.uasset` | SkyAtmosphere | 〃 |
| `.../0/XN/0REUIKFX1QOCF7LJROUR9G.uasset` | ExponentialHeightFog | 〃 |
| `.../7/Y0/6Y6HPGUPA3PA9U1AMYGV77.uasset` | VolumetricCloud | 〃 |
| `.../7/DI/RM1UX576R1CQP9NYPB1ZOY.uasset` | SM_SkySphere (숨김) | 〃 |

**정확히 6개**. 설계 rev.2 의 T10 기대치(스카이돔 포함 6개)와 일치한다.
`Content/Maps/PresetMaker1.umap` 은 **변경되지 않았다**(`cmp` 동일). 139개 외부 액터 중 나머지 133개도 무변경 —
설계 §5-2-1 의 "대상 패키지 지정 저장"이 실제로 작동했고, 전역 저장의 부수 기록이 일어나지 않았다.

### 1-2. 데이터 파일

| 파일 | 변경 |
|---|---|
| `Park3D/Save/3D/Light/Sangmyung.json` | **신규** — 이식 프리셋 |
| `Park3D/Save/3D/Light/_default.txt` | `Daylight.json` → `Sangmyung.json` |
| `Park3D/Save/3D/Light/_UserBaseline.json` | **신규** — 이식 전 사용자 설정 보존본(롤백용) |
| `Park3D/Save/3D/Light/Daylight.json` | **무변경** (md5 백업과 동일) |
| `Package/Windows/Save/3D/Light/Sangmyung.json` / `_default.txt` | 패키지 동기화 |
| `Package/Windows/Save/3D/Light/LightSettings.json` | **무변경** — 사용자 이전 튜닝 보존 |

### 1-3. 이 작업과 무관한 동시 변경 (건드리지 않음)

세션 중 다른 경로로 생긴 변경이 있어 구분해 남긴다. **이식 작업이 만든 것이 아니다.**

`Park3D/Save/Config/config_pmaker.json`, `Save/3D/CameraPos/CamPos_40Face_동대문.json`,
`Save/3D/Preset/001_Preset_40Face_동대문.json`, `Save/3D/{CameraPos,CarPos,Preset}/*office*`,
`Save/temp/`, `Park3D/Docs/Bug/`.

## 2. 사전 영향도가 지목한 위험의 사후 결과

| ID | 사전 위험 | 사후 결과 |
|---|---|---|
| H1 | 노출 목표 혼선 | 해소. 목표를 "이식 전후 밝기 동등성"으로 통일하고 기준선을 직접 측정 |
| H2 | 뷰포트/카메라 노출 공유 → 화이트아웃 재발 | **회귀 없음.** 뷰포트 114.34 (기준선 126.35, -9.5%, 허용 ±15% 내), 클리핑 0% |
| H3 | 스카이돔이 SkyAtmosphere 이식을 가림 | 실측 결과 **Visible 이었다**(영향도의 "Hidden 가능성 높음" 추정은 틀렸다). 사용자 승인 후 숨김 처리 → 하늘이 SkyAtmosphere 로 렌더됨을 실촬로 확인 |
| H4 | Content 는 git/SVN 보호 없음, 전역 저장 위험 | 139개 전체 백업 확보. 지정 저장으로 대상 외 0건 |
| M1 | 테스트 66개가 아니라 88개 | 88개 실행, **0 실패** |
| M2 | `CaptureCurrent`/`GetFields` 가 기본값을 폴백으로 씀 | 레벨에 액터가 모두 존재해 폴백 경로 미진입. 실동작 확인 |
| M3 | `LightControlManagerTest` 가 실제 액터를 더티화 | S1(적용)과 S6(테스트) 세션 분리 준수. 테스트 후 `save_dirty_packages` 미호출 |
| M4 | `FileRoundTrip` 이 `_default.txt` 를 교체·복원 | 테스트 후 `_default.txt` = `Sangmyung.json`, `Daylight.json` md5 백업과 동일 — 정상 복원 |
| M6 | Local Exposure 0.8 로 EV↔루마 비선형 | 실제로 비선형이었다. EV -1.02 → -1.30 (0.28 EV) 에서 합산 루마가 76.53 → 86.27 로 튐(선형 예상보다 큼). 선형 외삽을 버리고 실측 두 점 비교로 결정 |
| M7 | VSM 활성이라 캐스케이드 이식 무의미 | 값만 반영하고 판정에서 제외 |

## 3. 밝기에 의존하는 기존 기능에 대한 영향

| 기능 | 결과 | 근거 |
|---|---|---|
| 카메라 캡처(`cam.captureJPG`) | 정상. 클리핑 cam1 0.12% / cam2 1.15% | 최종 캡처 9장 |
| 주차선 데칼 가독성 | 유지 — 흰 주차선과 파란 슬롯 데칼 모두 식별 가능 | `shots/final_cam1_1.jpg` |
| 차량 색 식별 | 유지 — 연두/빨강/검정 차량 구분 가능 | 〃 |
| 프리셋 2D 뷰(뷰포트) | 정상, 밝기 -9.5% | `shots/final_vp_1.png` |
| MJPEG 스트리밍 | **미검증** — 이번 작업에서 스트림을 켜서 측정하지 않았다 |
| 패키지 앱 실동작 | 정상 기동, 조명 적용 로그 확인, 렌더 정상 | `Package/.../Park3D.log`, `shots/pkg_cam1_1.jpg` |

## 4. 빌드 파급

`LightControlTypes.h` 변경 → 직접 의존 7 TU. 실제 빌드는 유니티 blob 기준 `Module.Park3D.1.cpp`,
`Module.Park3D.2.cpp` 2개 컴파일 + 링크. 24.6초. `Park3D.Build.cs` 무변경.
재패키징(BuildCookRun)도 성공: cook 660/660, stage/archive 완료, 산출물 타임스탬프 2026-08-06 17:23.

## 5. 판정 기준 미달 항목 (은폐 금지)

설계 §7-4 의 차단 기준 중 **뭉갬·클리핑 2건이 초과**했다.

| ID | 기준 | 실측 | 판정 |
|---|---|---|---|
| B1/B2 밝기 동등성 | 합산 ±15% | 76.54 vs 76.52 (**+0.03%**) | 통과 |
| 뷰포트 밝기 | 107.4~145.3 | 114.34 | 통과 |
| B9 하늘 | ≥ 0.5·기준선, 뭉갬 ≤5% | cam1 166.25 / cam2 199.08 | 통과 |
| B3/B4 뭉갬 | ≤ 기준선 +2.0%p | cam1 +8.03%p, cam2 +3.85%p | **초과** |
| B5/B6 클리핑 | ≤ 기준선 +0.5%p | cam1 +0.07%p(통과), cam2 **+1.05%p** | **초과** |

**원인:** 태양 방위가 43.73° → 304.54° 로 **261° 회전**하고 고도가 55° → 44.5° 로 낮아졌다.
그림자가 깊어지고(뭉갬↑) 태양을 마주 보는 면이 밝아진다(클리핑↑). 이는 노출로 상쇄할 수 있는 종류가 아니다 —
카메라 1은 더 어두워지고(71.80 → 57.00) 카메라 2는 더 밝아졌다(81.23 → 96.08). **한쪽을 맞추면 다른 쪽이 어긋난다.**

설계 §7-4 의 뭉갬·클리핑 임계값은 "태양 방향이 그대로인 상태에서 노출만 재조정"을 전제로 쓰였고,
방위 261° 회전을 동반하는 이식에는 성립하지 않는다. 임의로 통과 처리하지 않고 **기준 미달로 기록**한다.

**대안(사용자 판단 필요):** 태양 방위·고도만 기존 값(43.73° / 55°)으로 되돌리고 나머지(광량·색온도·간접광·
대기·안개·구름)를 이식하면 뭉갬·클리핑은 기준 안으로 들어올 가능성이 높다. 다만 그러면 "원본 조명 이식"의
방향 성분이 빠진다.

## 6. 롤백

1. 레벨 액터 6개: `_workspace/lightport/backup/PresetMaker1_ExternalActors/` 에서 해당 경로로 복사
2. 조명 프리셋: `Park3D/Save/3D/Light/_default.txt` 를 `_UserBaseline.json`(이식 전 사용자 설정) 또는
   `Daylight.json` 으로 되돌린다
3. C++ 기본값: `git checkout Park3D/Source/Park3D/Light/LightControlTypes.h` 후 재빌드
4. 패키지: `_workspace/lightport/backup/PackagedSave_Light/` 복원 + 재패키징
