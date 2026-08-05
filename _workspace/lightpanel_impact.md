# [lightpanel] 영향도 분석 (사전 검토 + 사후 확인)

> 사전 위험 검토는 설계서 `lightpanel_architect_design.md` §9(잔여 리스크)에 통합해 수행했다.
> 본 문서는 그 예측과 실제 결과를 대조하고, 회귀 범위를 확인한다.

## 1. 변경 범위

### 신규 파일

```
Park3D/Source/Park3D/Light/LightControlTypes.h        FLightSettings (6항목)
Park3D/Source/Park3D/Light/LightControlLibrary.h/.cpp 순수 함수(직렬화·클램프·파일·기본값 포인터)
Park3D/Source/Park3D/Light/LightControlManager.h/.cpp AActor, 레벨 조명에 적용/캡처
Park3D/Source/Park3D/Light/LightControlWidget.h/.cpp  UUserWidget, C++ 위젯 트리
Park3D/Source/Park3D/Tests/LightControlLibraryTest.cpp
Park3D/Source/Park3D/Tests/LightControlManagerTest.cpp
```

### 수정 파일

| 파일 | 변경 |
|------|------|
| `MainMenuWidget.h/.cpp` | `LightControlWidgetClass`, `Btn_Light`, `HandleLight`, `InjectLightButton`, `NativeOnInitialized` 추가 |
| `Park3DGameMode.cpp` | BeginPlay 에 시작 조명 적용 블록 + include 2줄 |
| `Park3D.Build.cs` | **변경 없음** (UMG/Slate/Json/DesktopPlatform 이미 의존) |

### 데이터 파일 (신규)

```
Save/3D/Light/Daylight.json    기본 조명 설정
Save/3D/Light/_default.txt     기본값 포인터(상대 파일명 "Daylight.json")
```

에디터측(`Park3D/Save/`)과 패키지측(`Package/Windows/Save/`) 양쪽에 배치. 포인터를 **상대 파일명**으로 둬서 Save 루트가 달라도 각자 자기 `Light/` 폴더에서 해석된다(실행 확인 완료).

`.umap`·레벨 에셋 **변경 0건** — 조명은 이제 시작 시 파일에서 적용되므로 레벨 값을 다시 건드릴 필요가 없다.

## 2. 사전 예측 대비 실제

| 설계서 §9 리스크 | 실제 결과 |
|-----------------|----------|
| #1 안개가 원거리 헤이즈로 남을 수 있음 | **현실화** — 노출로 상쇄하는 구조가 되어 원경/근경 트레이드오프 발생(QA §4). 7번째 항목 추가를 제안 |
| #2 런타임 버튼 삽입이 `VBox_Menu` 이름에 의존 | 이름은 존재했으나 **`Btn_Exit` 가 직계 자식이 아니어서** 인덱스 탐색 실패 → 부모 거슬러 오르기로 보완. 추가로 `NativeConstruct` 가 Slate 빌드 이후라 순서가 반영되지 않아 `NativeOnInitialized` 로 이동 |
| #3 메인 뷰포트와 카메라 캡처의 밝기 차 | **원인 규명** — 두 경로의 노출 반응이 다른 게 아니라, EV 0 에서 뷰포트가 톤매퍼 상단에 포화돼 있었다. 카메라 경로는 EV 에 선형(110 → 38.8 = 1.5스톱) |
| #4 `SCS_FinalColorLDR` 이 PPV 노출을 따르는지 미확인 | **따른다** — 확인 완료 |

## 3. 회귀 확인

| 대상 | 결과 |
|------|------|
| Automation `Park3D` 전체 | **66/66 성공, 0 실패** (기존 60 전부 유지 + 신규 6) |
| 프리셋(생성·목록·데칼 리빌드) | 통과 |
| 차량(목록·저장·로드) | 통과 |
| 측정(타겟점·각도·거리·카메라 높이) | 통과 |
| 캡처(`cam.captureJPG`) | 통과 |
| 맵·랜덤(`map.get`·`setRandomColor`·`recreateCars`) | 통과 |
| **합계** | **14/14 통과, 실패 0** (패키지 빌드에서 실행) |

RPC 79개 메서드의 시그니처·동작은 변경하지 않았다(조명 RPC는 추가하지 않음 — 요청 범위 밖).

## 4. 기존 동작에 대한 영향

| 항목 | 영향 |
|------|------|
| **레벨 조명이 시작 시 덮어써진다** | 이전에는 맵에 저장된 값이 그대로 쓰였으나, 이제 `Save/3D/Light/` 의 기본값이 적용된다. 레벨에서 조명을 바꿔도 실행 시 파일 값으로 덮인다 — 요구사항 R4 의 의도된 결과이나 **동작 변화**이므로 명시한다 |
| Main Menu 항목 1개 증가 | 세로 목록이 한 칸 길어진다. 스타일·패딩은 기존 버튼에서 복사해 외형 일관성 유지 |
| 카메라 뷰어/MJPEG | 씬 조명을 그대로 받으므로 노출 변경이 그대로 반영된다(QA §4의 트레이드오프 대상) |
| 저장/로드·프리셋·측정 | 무관 |

## 5. 잔여 위험

| # | 위험 | 심각도 | 대응 |
|---|------|-------|------|
| 1 | 노출 하나로 원경·근경을 동시에 못 맞춘다(안개 원인) | **중** | 안개 농도를 7번째 항목으로 추가하면 해소. 사용자 판단 대기 |
| 2 | 패널 UI 가 WBP 가 아니라 C++ 구성이라 디자이너에서 재배치 불가 | 낮 | 이 클래스를 부모로 하는 WBP 를 만들면 해결. 기능 제약 없음 |
| 3 | `Save/3D/Light/` 가 없거나 포인터가 깨지면 내장 기본값으로 폴백 | 낮 | 의도된 설계. 폴백 경로 테스트(T6) 통과 |
| 4 | 런타임 버튼 삽입이 `VBox_Menu` 이름에 의존 | 낮 | 못 찾으면 경고 로그 + 패널은 `TogglePanel`(BlueprintCallable)로 접근 가능 |
