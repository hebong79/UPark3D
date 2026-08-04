# 설계서 — 카메라 뷰어(렌더타겟) 상시 표시

- phase: `camviewer_always`
- 작성일: 2026-07-28
- 규칙: CLAUDE.md 0번(설계 선행)

---

## 1. 요구사항

카메라 뷰(렌더타겟 프리뷰)가 **카메라 컨트롤 대화상자가 열려 있을 때만** 보인다. 이를 **항상 보이도록** 바꾼다.

## 2. 현재 구조와 원인

```
APark3DGameMode::BeginPlay
   └ ShowMenu()  → WBP_MainMenu (ZOrder 100, 상시)
        └ TogglePanel → UCameraControlWidget (ZOrder 10)
             ├ NativeConstruct → EnsureViewer()  ← 뷰어 최초 생성 + AddToViewport(5)
             │                   RefreshViewerBrush()
             └ NativeDestruct  → ViewerInstance->RemoveFromParent()   ★ 여기서 사라짐
```

원인 두 가지:

| # | 원인 | 위치 |
|---|---|---|
| A | 패널이 닫힐 때 뷰어를 뷰포트에서 제거 | `CameraControlWidget.cpp:237-242` |
| B | 뷰어의 **생성자가 컨트롤 패널**이라, 패널을 한 번도 열지 않으면 뷰어가 아예 존재하지 않음 | `CameraControlWidget.cpp:1211-1227` (`EnsureViewer`) |

A만 고치면 "한 번 열었다 닫으면 계속 보임"이 되어 요구를 절반만 만족한다. **B까지 고쳐야 진짜 상시**가 된다.

추가로, 패널이 닫힌 상태에서도 선택 카메라는 바뀔 수 있다(RPC `cam.select` / `cam.create` / `cam.delete`). 현재 브러시 갱신(`RefreshViewerBrush`)은 컨트롤 패널만 호출하므로, 패널이 없으면 **뷰어가 낡은 RT를 계속 잡고 있거나 아예 비어 있다.** 이 경로도 함께 해결해야 기능이 성립한다.

## 3. 대안 비교

| 안 | 소유자 | 장점 | 단점 | 채택 |
|---|---|---|---|---|
| 1 | `UCameraControlWidget` 유지, `NativeDestruct` 제거만 | 변경 최소(4줄) | 원인 B 미해결 — 패널을 한 번도 안 열면 안 보임 | ✕ |
| 2 | **`APark3DGameMode`가 생성·보유** | `MenuWidget`·`MapFloorActor`와 **동일한 기존 선례**("패널을 한 번도 열지 않아도 존재") / 수명이 레벨과 일치 | GameMode에 필드 2개 추가 | **○** |
| 3 | `ACameraControlManager`(액터)가 생성 | 카메라와 수명 일치 | 매니저는 뷰어를 열지 않아도 스폰될 수 있고, 액터가 UI를 소유하는 역전 구조 | ✕ |
| 4 | 뷰어에 static 싱글턴 포인터 | 조달 간단 | PIE 세션 간 잔존 위험, 소유자 불명확 | ✕ |

**채택: 안 2.** `Park3DGameMode.cpp:44-45`가 이미 "패널을 한 번도 열지 않아도 바닥은 존재한다"며 `MapFloorActor`를 BeginPlay에서 보장하는 선례가 있다. 상시 UI(`ShowMenu`)도 같은 자리에 있다. 뷰어를 여기 두는 것이 프로젝트 관례와 일치한다.

## 4. 변경 설계

### 4-1. `APark3DGameMode` — 뷰어 소유

```cpp
// Park3DGameMode.h
/** 상시 표시할 카메라 뷰어 위젯 클래스. 기본값 /Game/UI/WBP_CameraViewer. */
UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Park3D|UI")
TSubclassOf<UCameraViewerWidget> ViewerWidgetClass;

/** 상시 뷰어 인스턴스. 컨트롤 패널이 공유해서 쓴다(중복 생성 방지). */
UFUNCTION(BlueprintCallable, Category = "Park3D|UI")
UCameraViewerWidget* GetCameraViewer() const;

private:
void ShowCameraViewer();                       // BeginPlay에서 1회
UPROPERTY(Transient) TObjectPtr<UCameraViewerWidget> ViewerWidget = nullptr;
```

- 생성자에서 `ConstructorHelpers::FClassFinder<UCameraViewerWidget>(TEXT("/Game/UI/WBP_CameraViewer"))` — `MenuWidgetClass`와 동일 패턴.
- `BeginPlay()`에서 `ShowMenu()` 다음에 `ShowCameraViewer()` 호출. `AddToViewport(5)` — 기존 ZOrder 규약(뷰어 5 < 패널 10 < 메뉴 100) 유지.

### 4-2. `UCameraControlWidget` — 소유에서 사용으로

- `EnsureViewer()`: **먼저 GameMode의 뷰어를 조회**해 `ViewerInstance`에 대입한다. 없을 때만 기존 경로(자체 생성)로 폴백 — BP 게임모드가 `APark3DGameMode`가 아닌 경우 하위 호환.
- `NativeDestruct()`: 뷰어 `RemoveFromParent()` **삭제**. (거리 대화상자 제거는 그대로 둔다 — 요청 범위 밖)

### 4-3. `UCameraViewerWidget` — 자립적 RT 추종

패널 없이도 선택 카메라를 따라가야 하므로 뷰어가 스스로 폴링한다.

```cpp
virtual void NativeTick(const FGeometry&, float) override;

TWeakObjectPtr<ACameraControlManager> CachedManager;  // 매 프레임 GetActorOfClass 방지
TWeakObjectPtr<UTextureRenderTarget2D> AppliedRT;     // 바뀔 때만 브러시 재지정
```

- 매니저 조달은 `GetActorOfClass`만 하고 **스폰하지 않는다**(뷰어가 매니저를 만드는 역전 방지). 없으면 RT는 null.
- `GetSelectedRenderTarget()` 결과가 `AppliedRT`와 다를 때만 `SetRenderTarget` 호출 → 매 프레임 브러시 재생성 없음.

### 4-4. RT가 없을 때의 표시

카메라 0대(부팅 직후)면 RT가 null이다. 현재 `SetRenderTarget`은 null이면 그냥 return이라 **빈 프레임 사각형만 남는다.**

→ `SetRenderTarget(nullptr)`에서 `Img_View`를 `Collapsed`로, 유효 RT면 `Visible`로 전환한다. 외곽 프레임은 `NativePaint`가 `Img_View`의 지오메트리 크기로 그리므로 Collapsed면 크기 0 → 자동으로 안 그려진다(추가 분기 불필요).

**가정**: "항상 보인다"는 *컨트롤 패널과 무관하게 보인다*는 뜻으로 해석했다. 보여줄 카메라가 하나도 없을 때 빈 사각형을 띄우는 것은 의도가 아니라고 판단해 숨긴다. 카메라가 생기면 즉시 나타난다.

## 5. 처리 흐름 (변경 후)

```
APark3DGameMode::BeginPlay
   ├ ShowMenu()            → WBP_MainMenu (100)
   └ ShowCameraViewer()    → WBP_CameraViewer (5)  ★ 상시
                                └ NativeTick: 매니저 폴링 → 선택 RT 변경 시에만 브러시 갱신
                                              RT 없으면 Img_View Collapsed

UCameraControlWidget (10)  … 열고 닫아도 뷰어에 영향 없음
   └ EnsureViewer(): GameMode 인스턴스를 받아서 씀(생성하지 않음)
```

## 6. 좌표/단위 규약

이번 변경은 위젯 수명·표시 상태만 다룬다. 좌표·단위 변환 없음. 뷰어의 표시 크기(16:9)·이동·리사이즈·크기 영속화(`ViewerSize.json`)는 기존 로직을 그대로 둔다.

## 7. 테스트 포인트

| # | 대상 | 방법 |
|---|---|---|
| 1 | `SetRenderTarget(nullptr)` → `Img_View` Collapsed | Automation 유닛테스트(신규) |
| 2 | `SetRenderTarget(유효 RT)` → Visible + 브러시 리소스 일치 | Automation 유닛테스트(신규) |
| 3 | 기존 크기 저장/복원 라운드트립 회귀 | 기존 `CameraViewerWidgetTest` 재실행 |
| 4 | 부팅 직후 패널을 한 번도 안 열어도 뷰어 존재 | PIE/패키지 실행 + 화면 캡처 |
| 5 | 컨트롤 패널 열었다 닫아도 뷰어 유지 | 동일 |
| 6 | 패널 닫힌 채 RPC `cam.create`/`cam.select` → 뷰어가 해당 카메라를 표시 | RPC + 캡처 |

## 8. 영향 범위(사전)

| 대상 | 영향 |
|---|---|
| `Park3DGameMode.h/.cpp` | 필드·함수 추가. 기존 메뉴/카메라 시작 로직 불변 |
| `CameraControlWidget.cpp` | `EnsureViewer` 조달 경로 변경, `NativeDestruct` 4줄 삭제 |
| `CameraViewerWidget.h/.cpp` | `NativeTick` 추가, `SetRenderTarget` null 처리 확장 |
| `Park3D.Build.cs` | 변경 없음(UMG·Engine 기존 의존) |
| WBP 에셋 | **변경 없음**. `WBP_CameraControl`의 `ViewerWidgetClass` 기본값은 폴백 경로로 계속 유효 |
| RPC | 변경 없음. `cam.*`는 매니저만 다루고 뷰어를 모름 |

**위험**: BP 게임모드(`BP_PresetGameMode`)가 `APark3DGameMode`를 상속하지 않는 경우 뷰어가 안 뜬다 → `EnsureViewer` 폴백으로 기존 동작 유지(패널 열 때만 표시)되므로 회귀는 아니다. 레벨의 GameMode 클래스를 구현 단계에서 확인한다.
