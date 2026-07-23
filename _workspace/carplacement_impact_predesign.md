# 차량배치 UI / Main Menu 설계서 — 사전 영향도 검토 (구현 전)

- 작성일: 2026-06-23
- 작성자: impact-analyst
- 검토 대상: `Docs/20260623_215100_차량배치UI_메뉴_설계서.md`
- 단계: 설계 게이트 (구현 착수 전 사전 분석)
- 근거 소스: `Park3D/Source/Park3D/*`, `unity/CarObject/*`, `Docs/20260618_115326_*`

---

## 0. 결론 (요약)

- **반려할 치명적 결함은 없다.** 설계는 기존 PresetMaker 패턴과 정합하며, Build.cs 의존성도 이미 충족된다(신규 모듈 0개).
- 다만 **구현 전 반드시 닫아야 할 차단(Blocker)급 선결 항목 2개**가 있다:
  - **B-1 (높음): JSON 스키마 미확정.** 권위 있는 Unity `SObjectPos`/`SVector3` 클래스 정의가 repo 스냅샷에 없음 → 필드 대소문자/`pos` 키(x/y/z vs X/Y/Z) 확정 불가. 설계의 Q1/Q2가 실제 미해결 위험으로 확인됨.
  - **B-2 (높음): 차량 메시 에셋 부재.** `Park3D/Content` 전체에 차량 메시(fbx/gltf/glb/Car*)가 1개도 없음 → §3.5 색상 파라미터(Q4), §6 정면축(Q5) 모두 검증 불가. 액터/색상/위젯 UI 단계(구현 3~5단계)는 메시 임포트 전 진행 불가.
- 나머지는 **구현 단계 주의 위험**으로 분류(아래 우선순위 표). 특히 Q8(GameMode 교체)은 회귀 위험이 명확하므로 **기본 동작 유지형 완화책**을 권고한다.

---

## 1. 빌드 모듈 / 의존성 영향 — **위험 낮음 (양호)**

근거: `Park3D/Source/Park3D/Park3D.Build.cs:11,17-21`

```
PublicDependencyModuleNames = { Core, CoreUObject, Engine, InputCore,
    EnhancedInput, UMG, Slate, SlateCore, Json, JsonUtilities }
+ (non-Shipping) DesktopPlatform, PARK3D_USE_FILE_DIALOG=1
```

- 설계 §9.6의 "신규 모듈 추가 없음" 주장은 **정확**하다.
  - UMG/Slate(위젯), Json/JsonUtilities(`FJsonObjectConverter`), DesktopPlatform(파일 대화상자), EnhancedInput(입력) 모두 이미 존재.
  - 차량 `UStaticMesh`/`UStaticMeshComponent`/`UMaterialInstanceDynamic`/`UTextRenderComponent`는 전부 기본 `Engine` 모듈 → 추가 불필요.
- **GLTF 관련 모듈은 불필요**: 설계가 런타임 GLB 로딩을 범위에서 제외(§8.2 A, 사전 임포트 채택)했으므로 `glTFRuntime` 등 플러그인 의존이 없다. 이 결정이 유지되는 한 Build.cs 변경 0.
- **헤더 의존 사이클 위험 — 낮음**: 신규 7개 클래스는 단방향 의존(`Widget → Manager → Actor → Component`, 공통 `ParkingCarTypes.h`)으로 설계됨. 기존 `ParkingPresetTypes.h`처럼 `ParkingCarTypes.h`를 순수 USTRUCT/UENUM 전용 헤더로 두면 사이클 없음. 단, **`UMainMenuWidget`이 `UPresetMakerWidget`을 직접 include하지 말 것** — 설계대로 `TSubclassOf<UUserWidget>`(§4.6)로 약결합 유지하면 PresetMaker 헤더 의존이 생기지 않는다(준수 시 위험 없음).

→ qa 전달: Build.cs는 변경 불필요. 신규 UCLASS 7종 추가이므로 **에디터 종료 후 풀 리빌드 1회 필수**(Live Coding으로는 신규 UCLASS 등록 실패 가능 — MEMORY `park3d-move-clean-rebuild`, Docs 20260618 §4 참조).

---

## 2. GameMode 영향 (설계서 Q8) — **위험 높음 (회귀 가능)**

근거:
- `Park3DGameMode.cpp:12-17` — 생성자가 `/Game/UI/WBP_PresetMaker`를 `PresetWidgetClass` 기본값으로 하드코딩.
- `Park3DGameMode.cpp:23-59` — `BeginPlay`가 그 위젯을 무조건 생성+`AddToViewport`+`FInputModeGameAndUI` 설정.
- `Docs/20260618_115326_*.md:50-55,100-101` — `DefaultEngine.ini`의 `GlobalDefaultGameMode=/Script/Park3D.Park3DGameMode`로 **전역 등록**. World Settings에서 override하지 않은 모든 맵에 적용.

설계 §9.4 / Q8은 "시작 위젯을 PresetMaker → MainMenu로 교체"를 제안한다. 회귀 위험:

| 회귀 시나리오 | 영향 |
|---|---|
| `PresetWidgetClass` 기본값 또는 `BeginPlay` 로직을 MainMenu로 **치환**하면, 기존 "실행 즉시 PresetMaker 자동표시"(20260618 검증 동작)가 **사라진다**. | 높음 — 기존 기능 파괴 |
| 전역 GameMode이므로 PresetMaker1 맵뿐 아니라 모든 맵의 시작 UI가 바뀐다. | 중간 — 광범위 |
| 입력 모드/커서 설정(`FInputModeGameAndUI`, `bShowMouseCursor`)이 MainMenu 기준으로 재설정되면, 이후 PresetMaker를 메뉴에서 열 때 포커스/커서 상태가 기존과 달라질 수 있다. | 중간 |

**완화책 (권장 우선순위)**:
1. **(권장) 기존 코드 무수정 + 데이터 교체**: `PresetWidgetClass`는 `EditDefaultsOnly`(`Park3DGameMode.h:25`)이므로, **C++ 변경 없이** BP 서브클래스 또는 기본값만 `WBP_MainMenu`로 바꾸면 된다(20260618 §7 명시). 이러면 `BeginPlay` 로직(커서/입력모드)을 그대로 재사용 → 외과적 변경(CLAUDE.md 3번) 충족. MainMenu가 PresetMaker를 자식 토글로 열도록 하면 기존 PresetMaker UI도 보존.
2. **롤백 용이성 확보**: 시작 위젯 전환은 **마지막 구현 단계(6단계)** 에서만, 그것도 기본값/BP 레벨에서 수행. C++ `BeginPlay`에 MainMenu 전용 분기를 넣지 말 것(넣으면 회귀 + 외과적 변경 위반).
3. 전환을 보류하고 MainMenu를 PresetMaker 안/별도 토글로 띄우는 안도 가능(설계 Q8을 "기획 확인"으로 유보).

→ unreal-implementer 경고: **`Park3DGameMode.cpp`의 `BeginPlay` 본문은 건드리지 말 것.** 시작 위젯 변경은 `PresetWidgetClass` 기본값/BP override로만.
→ qa 전달: 시작 위젯 전환 후 **회귀 검증** — (a) 기존 PresetMaker 자동표시 동작이 유지되거나 메뉴에서 1클릭으로 복원되는지, (b) 다른 맵(override 없는 맵) 시작 시 의도대로 뜨는지.

---

## 3. JSON 스키마 호환성 — **위험 높음 (B-1, 선결 필요)**

### 3.1 기존 PresetMaker JSON 경로 (참고 기준)
근거: `PresetMakerWidget.cpp:792-839`
- `FJsonObjectConverter::UStructToJsonObjectString` / `JsonObjectStringToUStruct`로 `FParkingPresetDatas{ TArray<FParkingPreset> Datas }`를 직렬화.
- `FParkingPreset.Offset`은 **`FVector`**(`ParkingPresetTypes.h:37`). 즉 **기존 프로젝트의 `preset.json`은 이미 UE `FVector` 직렬화 키 규약을 따르고 있고, 그 파일이 정상 로드된다.** → 차량도 같은 경로를 쓰면 UE↔UE 라운드트립은 안전(설계 TP-6 통과 예상).

### 3.2 Unity 상호 호환 위험 (핵심)
- 설계 NFR-01은 Unity `CarPos_SNum.json`과 **양방향 호환**을 요구. 그러나:
  - **권위 있는 `SObjectPos`/`SVector3` 클래스 정의가 repo에 없다.** `unity/CarObject` 전체에 `class SObjectPos` / `class SVector3` 정의 부재(검색 결과 0건). 활성 코드 `CCarPlacementDlg.cs`는 `using SCarPos = SObjectPos;`(line 9)로 외부 정의를 참조만 한다.
  - `CSaveCarPosData.cs:88-92`의 필드 목록(`id/slotId/prefabId/pos/rotY`)은 **주석 처리된 구버전**이라 권위가 없다. 여기엔 `presetId/type/isFront`가 **없다**.
  - 반면 **활성 코드** `CCarPlacementDlg.cs`는 `kCarPos.presetId`(line 624,649,751,778), `kCarPos.isFront`(626,769,779), `kCarPos.type`(629,752,780)을 실제 사용 → **현행 `SObjectPos`에는 이 3개 필드가 존재**함이 확정. 따라서 설계 §7 스키마(8필드)는 **방향이 맞다**(구버전 5필드 아님).
- **미확정 = 위험인 지점**:
  - (Q1) 각 필드의 **JSON 대소문자**(`presetId` 등은 camelCase 추정이나 미검증).
  - (Q2) **`pos`의 `SVector3` 키가 `x/y/z`(소문자)인지** — Newtonsoft 기본은 멤버명 그대로이나, `SVector3` 멤버명이 `x,y,z`인지 `X,Y,Z`인지 정의 부재로 확인 불가. **UE `FVector`의 `FJsonObjectConverter` 직렬화는 버전에 따라 `X/Y/Z`(대문자)** 가 될 수 있어, 소문자 키를 쓰는 Unity 파일과 **불일치 시 `pos`가 (0,0,0)으로 역직렬화되는 조용한 데이터 손실** 발생.

**완화책 (우선순위)**:
1. **(B-1, 차단) 구현 1~2단계 착수 전 실제 `CarPos_SNum.json` 샘플 1개 확보** → 8개 필드명·대소문자·`pos` 키를 눈으로 확정. 설계 §11 Q1/Q2가 권고한 그대로이며, 본 검토로 "선택"이 아니라 **필수**임을 확인.
2. **(권장 채택) 설계 §8.5 A안 — `pos`를 전용 `FCarVec3{ float x,y,z; }` USTRUCT로** 정의하여 키를 강제. `FVector` 직접 사용(§8.5 B)은 대소문자 리스크가 실제로 존재하므로 지양.
3. 필드 누락/추가 시 무손실 처리 확인: `FJsonObjectConverter`는 없는 필드는 기본값 유지하므로 부분 호환은 가능. 단 `pos` 키 불일치는 좌표 0화로 이어지므로 TP-7에서 **반드시 좌표 값까지 비교**.

→ qa 전달(중점):
- **TP-7 강화**: 손으로 만든 §7 샘플이 아니라 **실제 Unity 출력 파일**로 로드 검증, `pos.x/y/z` 비영 값이 보존되는지 수치 비교.
- **TP-6 라운드트립**에 `FCarVec3` 키가 `x/y/z`로 직렬화되는지 문자열 검사 추가.

→ 분석 한계: `SObjectPos`/`SVector3` 권위 정의 부재로, 본 검토는 "필드 존재"까지만 확정. **정확한 키 문자열은 샘플 파일 없이 검증 불가**(명시).

---

## 4. 위젯 ↔ 매니저 / PresetMaker 공존 — **위험 중간**

### 4.1 입력·커서·픽 충돌 (가장 구체적인 위험)
근거: `PresetMakerWidget.cpp:122-169` (NativeTick)
- PresetMaker는 이미 **Ctrl+좌클릭 → `GetHitResultUnderCursor`로 월드 픽 → 선택 프리셋 이동**을 구현(`:142-163`). 설계 §5.2의 차량 픽(Ctrl+LMB → `TraceFloor`)과 **입력 제스처가 완전히 동일**.
- 두 위젯이 **동시에 뷰포트에 떠 있으면(둘 다 키보드 포커스/픽 활성)**:
  - 같은 Ctrl+LMB가 양쪽에서 해석되어 **프리셋과 차량이 동시에 움직이거나**, 포커스 쟁탈로 한쪽이 먹통.
  - PresetMaker는 `bOffsetPickControl` 게이트(`:129`)와 RMB 릴리즈 시 포커스 복귀(`:135-139`)로 자기 입력을 제어. 차량 위젯도 **반드시 `bPlacing` 같은 게이트로 자기 입력을 가둬야** 함(설계 `bPlacing`(§3.2)이 이 역할 — 양호).
- RMB 보유 시에만 이동하는 `AParkFlyPawn`(`ParkFlyPawn.h`)과의 간섭: 차량 위젯의 WASD 이동(§5.5 NativeOnKeyDown)이 Pawn의 카메라 이동과 **같은 WASD를 공유**. PresetMaker도 동일 충돌을 겪고 RMB/포커스 엣지 로직(`:126-139`)으로 회피 중. 차량 위젯은 **이 포커스 복귀 로직을 그대로 포팅**해야 동일 문제를 피한다(설계 NativeTick 주석에 "픽 모드 입력 처리(선택)"로만 표기 — 포커스 복귀가 누락되면 회귀).

**완화책 (우선순위)**:
1. **(권장) 배타 표시**: MainMenu가 PresetMaker/CarPlacement를 **동시에 띄우지 않도록 배타 토글**(설계 §5.7 "형제 Dlg 닫기"). 이러면 입력/픽 충돌의 근본 원인 제거. → 설계대로 구현 시 위험 대부분 해소.
2. 차량 위젯의 픽/이동을 `bPlacing`/선택차량 존재 + 키보드 포커스 보유로 **이중 게이트**.
3. PresetMaker의 `NativeTick` 포커스-복귀(`:135-139`) 로직을 차량 위젯에도 동일 적용(복붙 허용 — 설계 §9.1 외과적 최소중복 방침과 정합).

### 4.2 매니저 탐색 패턴
근거: `PresetMakerWidget.h:204-205` (`TWeakObjectPtr<AParkingPresetManager> ViewManager; GetViewManager();`)
- 설계 `GetCarManager()`(§4.1)가 동일 패턴(`GetAllActorsOfClass`→없으면 Spawn). **별개 클래스(`ACarPlacementManager`)이므로 PresetMaker 매니저와 인스턴스 충돌 없음** → 위험 낮음. 단 두 매니저가 같은 레벨에 공존하므로, 차량 매니저가 PresetMaker가 그린 디버그 라인을 지우지 않도록 `ClearAll` 범위가 자기 액터/차량으로 한정되는지 확인(설계상 `Cars` 배열만 다룸 — 양호).

### 4.3 presetId 공간 공유
근거: 설계 §9.5 + `ParkingPresetTypes.h:28`(`FParkingPreset.PresetIdx`) ↔ Unity `CCarPlacementDlg.cs:751`(`primary.m_iPresetId`)
- 의미상 같은 그룹키지만 **두 모듈이 강제 동기화되지는 않음**(서로 다른 JSON, 서로 다른 데이터 보유). 1차에서는 "문서화만"이라는 설계 결정이 타당 → 위험 낮음. 단 사용자가 두 UI의 presetId를 불일치로 입력하면 시각적 그룹이 어긋날 수 있음(기능 결함 아님, UX 주의).

→ qa 전달(중점):
- 두 위젯 동시 표시 시 Ctrl+LMB 동작(배타 토글이 실제로 한쪽만 활성화하는지) — PIE/standalone 육안.
- 차량 위젯 활성 중 PresetMaker 디버그 라인이 보존되는지(매니저 ClearAll 격리).

---

## 5. 에셋 참조 — **위험 높음 (B-2, 선결 필요)**

근거: `Park3D/Content`에 차량 메시 0건.
- `*.fbx/*.gltf/*.glb` 검색 결과 0건, `Vehicles/Cars/Car/Meshes` 폴더 부재, `*Car*.uasset` 부재.
- 영향:
  - **Q4 (색상 파라미터)**: 차량 머티리얼이 노출하는 파라미터명(`BaseColor`/`Metallic`/`Roughness` 가정)을 `get_material_info`로 확인 불가 → `UCarColorComponent`(§3.5) 구현이 가정에 의존. 임포트 머티리얼이 GLB→UE 자동변환인지 마스터 머티리얼인지에 따라 파라미터명·번호판 슬롯 키워드(`licplate`)가 달라짐.
  - **Q5 (정면축)**: 메시 정면이 +X인지 확인 불가 → 회전 부호/오프셋(§6 Q3)과 결합해 **차량이 90°/180° 틀어져 배치되는** 시각 버그 가능.
- **단계적 차단**: 설계 §10.1 구현 단계 중 1단계(라이브러리)·2단계(JSON)는 **메시 없이 진행 가능**(순수 계산/직렬화). 3단계(액터/색상)·5단계(위젯 UI 시각 확인)·TP-8/9/14는 **메시 임포트 전 불가**.

**완화책 (우선순위)**:
1. **(B-2) 최소 1종 차량 메시 임포트 후** 3단계 착수. 임포트 직후 `get_material_info`로 Q4 파라미터명, 메시 바운드로 Q5 정면축 확정.
2. 메시 미확보 시에도 **1~2단계(라이브러리+JSON)는 선행 구현·유닛테스트 가능** → CLAUDE.md 1번(유닛테스트) 충족하며 진척 확보. 설계 §10.1 단계 순서가 이 분리를 이미 반영(양호).
3. `UCarColorComponent`의 키워드/파라미터명을 `EditAnywhere`(설계 §4.4)로 노출 → 메시별 실제 파라미터명에 맞춰 BP에서 조정 가능하게(가정 깨질 때 코드 수정 없이 대응).

→ qa 전달: TP-8(색상)·TP-14(회전 부호 육안)는 **메시 임포트 완료가 선행 조건**임을 명시. 메시 전 단계에서는 라이브러리/JSON 테스트만 게이트.

---

## 6. 우선순위 위험 목록 (구현 단계 입력)

| 우선순위 | ID | 위험 | 위험도 | 완화책 | 게이트 |
|---|---|---|---|---|---|
| 1 | B-1 | JSON `pos` 키 대소문자 불일치 → Unity 파일 좌표 0화(조용한 손실) | 높음 | 실제 샘플 확보 + `FCarVec3{x,y,z}` 전용 구조체(§8.5 A) | 구현 2단계 착수 전 |
| 2 | B-2 | 차량 메시 부재 → 색상/정면축 검증 불가 | 높음 | 최소 1종 임포트 후 3단계 착수, 1~2단계는 선행 | 구현 3단계 착수 전 |
| 3 | R-3 | GameMode 시작 위젯 교체 시 PresetMaker 자동표시 회귀 | 높음 | `BeginPlay` 무수정 + `PresetWidgetClass` 기본값/BP만 교체, 6단계로 연기 | 6단계 |
| 4 | R-4 | 두 위젯 동시 표시 시 Ctrl+LMB 픽/포커스 충돌 | 중간 | 배타 토글(§5.7) + `bPlacing` 게이트 + 포커스복귀 로직 포팅 | 5단계 |
| 5 | R-5 | 회전 부호/오프셋(Unity rotY ↔ UE Yaw) 미확정 | 중간 | TP-14 육안+스크린샷, 필요 시 `-rotY`/+180° 오프셋 | 5~6단계(메시 후) |
| 6 | R-6 | 신규 UCLASS 7종 → Live Coding 실패 가능 | 중간 | 에디터 종료 후 풀 리빌드 1회 | 각 신규 클래스 추가 시 |
| 7 | R-7 | `UMainMenuWidget`이 PresetMaker 헤더 직접 include → 결합/사이클 | 낮음 | `TSubclassOf<UUserWidget>` 약결합 유지(§4.6 준수) | 6단계 |

---

## 7. qa-verifier 중점 검증 항목 (전달)

1. **JSON**: 실제 Unity `CarPos_SNum.json` 로드 시 `pos.x/y/z` 비영 값 보존(TP-7 강화), `FCarVec3` 직렬화 키가 소문자 `x/y/z`인지 문자열 검사(TP-6 보강).
2. **GameMode 회귀**: 시작 위젯 전환 후 기존 PresetMaker 자동표시 동작이 보존/1클릭 복원되는지, override 없는 맵 시작 정상인지.
3. **공존**: PresetMaker + CarPlacement 동시 상황에서 배타 토글이 한쪽만 활성화하는지, Ctrl+LMB가 한 모듈에만 적용되는지, 차량 매니저 ClearAll이 PresetMaker 라인을 지우지 않는지.
4. **에셋 의존 테스트는 메시 임포트 후**: TP-8(색상)·TP-14(회전 부호 육안)는 메시 선행 필수. 메시 전에는 TP-1~7,9(라이브러리/JSON)만 게이트.
5. **리빌드**: 신규 UCLASS 추가 시 풀 리빌드로 등록 확인(Live Coding 의존 금지).

---

## 8. 분석 한계

- **`SObjectPos`/`SVector3` 권위 정의 부재**: `unity/CarObject` 스냅샷에 클래스 본체가 없어, JSON 8개 필드의 정확한 키 문자열·대소문자·`pos` 하위 키를 소스로 확정하지 못함. → 실제 샘플 파일로만 검증 가능(B-1).
- **콘텐츠(에셋) 그래프 미점검**: MCP 에디터 미연결 상태로 `find_references`/`get_asset_info`/`get_material_info`를 실행하지 않음. WBP_PresetMaker BP 위젯의 바인딩 무결성, 머티리얼 파라미터는 에디터 연결 후 재확인 필요(신규 클래스는 기존 BP를 변경하지 않으므로 기존 바인딩 깨짐 위험은 본질적으로 낮음).
