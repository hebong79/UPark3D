# camportrange — 주변 동작 사후점검 보고서

- 대상: 카메라 스트림 포트 대역을 `Save/Config/config_pmaker.json` 의 `cam_port_min`/`cam_port_max` 로 이관
- 입력: `Docs/20260805_233915_카메라_스트림_포트대역_config_이관_설계서.md`, `_workspace/camportrange_qa_report.md`, `_workspace/camportrange_impact_post.md`, `Docs/20260805_230638_시작시_설정파일_자동로딩_구현.md`, `git diff`(미커밋)
- 방법: 위 산출물을 직접 재읽고, `git diff` 로 실제 코드 변경을 대조해 "실제 확인"과 "코드 판독에 의한 추론"을 구분했다. 기능 코드는 수정하지 않았다.

---

## 종합

| 구분 | 건수 |
|---|---|
| 통과 | 6 |
| 실패 | 0 |
| 미검증 | 3 |

---

## 1. 기존 MJPEG 스트리밍 — 채널 개설/정지, `camId-1` 인덱스 규약, 슬롯 스케줄러

| 세부 항목 | 판정 | 근거 |
|---|---|---|
| `ResolvePort` 자체 | **통과** | `git diff -- CamStreamPolicy.cpp` 로 직접 확인 — 기존 함수 무수정, `PortRangeToBase`/`ExtendedMaxPort` 만 추가됨. 기존 유닛테스트 `Park3D.Rpc.CamStream.ResolvePort` 그대로 유지 |
| `DiffChannels` 인덱스 정합성(확장 10→12, 축소 12→2) | **통과(코드 판독 + 실측 혼합)** | 영향도 §3.3 이 `RemoveAt(CamId-1)` 내림차순 처리를 코드로 추적해 "안전"이라 판정했고, 이는 **실행 검증이 아니라 코드 판독**이다. 다만 QA A/B/C 항의 실제 기동 로그가 이 판정과 부합하는 결과를 냈다 — 10↔2대, 10↔11대 전환에서 예상한 채널 수(cam1~cam11 순차 또는 cam1~cam2만)가 정확히 로그에 찍혔다(`camportrange_restart.log:2301~2321`, `camportrange_regression.log:2292~2295`). 즉 **결과(채널 수·순서)는 실측, 내부 인덱스 연산의 정확성 자체는 코드 판독 근거** |
| 슬롯 스케줄러(`SelectSlots`/`ActiveSlots`/`HardMaxSlots`) | **통과(추론, 미실행)** | 영향도 §3.3 이 `MaxCameras` 와 무관하게 클램프됨을 코드로 확인했다. 이번 phase 의 QA·사후영향도 어느 쪽도 다수 시청자 부하 상태에서 슬롯 스케줄링을 **실행 검증하지 않았다**(11대 로딩 테스트는 시청자 없이 채널 개설만 확인). 구조적 안전 근거는 있으나 런타임 실측은 없다 |
| 히치 위험(대량 소켓 바인드) | **미검증** | 영향도 §3.3·M6 이 "채널당 스레드 2 + 소켓 1" 이라는 구조적 위험을 지적했을 뿐, 실제 스레드 수 증가나 Tick 프레임 시간은 **측정하지 않았다**(영향도 §9의 qa-verifier 전달 항목 M6 이 이번 QA 보고서에는 반영되지 않음) |

## 2. `cam.streamStatus` / `GetCameraStreamPort` RPC 응답

| 세부 항목 | 판정 | 근거 |
|---|---|---|
| 함수 자체 무수정 여부 | **통과** | `git diff -- CamStreamSubsystem.cpp` 로 확인 — `GetCameraStreamPort`(스캔 대상 라인대), `BuildStatusJson` 본문 모두 이번 diff 에 없음. 값의 **출처**(BasePort/MaxCameras)만 config 로 바뀌었을 뿐 스키마·호출 시그니처는 그대로 |
| 실제 RPC 응답값(`cam.list`/`cam.streamStatus`) 확인 | **미검증** | QA 보고서 서두에 "환경 주의사항 준수: … 검증은 로그 파일 + `netstat -ano` PID 대조로만 수행(**RPC 미사용**)" 이라고 명시되어 있다. 패키지 인스턴스(PID 27700, 포트 13510 점유)를 건드리지 않기 위한 의도적 선택이었으나, 그 결과 이번 phase 에서 `cam.streamStatus`/`cam.list` 를 **실제로 호출해 응답 JSON 을 본 적은 없다**. 설계서 §7 의 검증 계획("`cam.list`의 `streamPort`가 13601/13602 인지")도 이번 QA 문서에는 반영되지 않았다 |
| 1틱 지연 동안 신규 camId 의 `streamPort=0` | **미검증(추론만)** | 영향도 §3.2 가 "채널 개설은 Tick 담당이라 1틱 지연이 정상"이라고 코드로 추론했을 뿐, 그 순간의 RPC 응답을 실측하지 않았다 |

## 3. 카메라위치 파일 열기/저장 UI 경로 + 시작 자동 로딩 경로 (둘 다 `LoadFromJsonFile` 공유)

| 세부 항목 | 판정 | 근거 |
|---|---|---|
| 시작 자동 로딩 경로(`Park3DGameMode` → `LoadFromJsonFile`) | **통과** | QA C1/C2/D/E 가 전부 이 경로로 기동했다. 11대·한글파일명 파일이 `camerapos_file` 로 지정된 상태의 실제 게임 기동 로그로 "확장 → 다음 기동 유지 → UTF-8 유지 → 비활성 시 미기록"을 각각 실측 확인(`camportrange_c1_boot1.log`, `camportrange_c2_boot2.log`, `camportrange_d_boot.log`, `camportrange_e_boot.log`) |
| "파일 열기" UI 버튼 경로(`HandleOpen` → `LoadFromJsonFile`) | **통과(간접)** | `HandleOpen` 은 선행 기능 문서(`20260805_230638`)에서 이미 `LoadFromJsonFile` 로 추출되어 자동 로딩과 **완전히 같은 코드 경로**임이 확인되어 있다(`CameraControlWidget.cpp` 리팩터링 이력). 이번 phase 의 QA 는 파일 다이얼로그를 통한 실제 클릭이 아니라 config 의 `camerapos_file` 자동 로딩으로 같은 함수를 태웠으므로, **함수 자체의 동작은 실측, "다이얼로그로 연 경우"라는 입력 경로 자체는 간접 확인**이다 |
| "저장" UI 경로(`HandleSave` → `EnsureCamStreamPortRange`) | **미검증** | `git diff` 로 `HandleSave` 안에 `EnsureCamStreamPortRange(Mgr->GetCameraCount())` 호출이 추가된 것은 확인했다. 그러나 QA A/B/C/D/E 어느 항목도 "저장" 버튼을 눌러 11대 이상을 저장하는 시나리오를 재현하지 않았다 — 전부 로딩(파일 열기/자동로딩) 경로만 실행했다. 저장 시점의 대역 확장은 **코드 존재만 확인, 실행 검증 없음** |

## 4. 같은 config 파일을 공유하는 선행 기능(`rpc_port`, `max_zoom`, 파일 3종 자동 로딩)이 손상되지 않는가

| 세부 항목 | 판정 | 근거 |
|---|---|---|
| `UpdateCamPortRange` 가 다른 키를 보존하는가 | **통과** | 신규 유닛테스트 `Park3D.AppConfig.UpdateCamPortRange`(T10)가 `rpc_port`/`preset_file`/미지의 키(`unknown_key`) 보존을 직접 assert 하고, Automation 88건 전부 성공으로 실행됨. 또한 저장소 실물 `git diff -- Park3D/Save/Config/config_pmaker.json` 자체가 `cam_port_min`/`cam_port_max` **두 줄만 추가**되고 `rpc_port`/`preset_file`/`carpos_file`/`camerapos_file`/`max_zoom` 은 값 그대로임을 실측으로 보여준다 |
| 자동 로딩 적용 순서(로딩 중 재기록이 진행 중 로딩을 오염시키는가) | **통과(코드 판독)** | 영향도 §8 이 `ApplyStartupConfig` 가 `Load` 시점의 **값 복사본**만 참조함을 코드로 확인했다(`Park3DGameMode.cpp:110-111`). 카메라위치 로딩 중 config 가 재기록돼도 이후 차량배치 적용은 복사본을 쓰므로 오염되지 않는다. 이 경로를 별도로 실행 재현하지는 않았으나, 로딩 순서를 흔들 수 있는 유일한 쓰기(`EnsureCamStreamPortRange`)가 카메라위치 로딩 **다음**(design §4.2, `CameraControlWidget.cpp:874-876`)에 일어난다는 것도 diff 로 확인됨 |
| `rpc_port` 결정 체인과의 간섭 | **통과** | 영향도 §8 "`rpc_port` 와의 간섭 없음" 판정 + T10 이 `rpc_port` 보존을 실측. `RpcServerSubsystem.cpp` 는 이번 diff 에 없음(git status 목록에서도 미변경) |

## 5. 패키지 빌드 경로(스테이지 루트 `Save/Config/`)

| 세부 항목 | 판정 | 근거 |
|---|---|---|
| 실제 패키지 config 가 H1 시나리오(키 없음) 그대로인가 | **확인됨(사실)** | 이번 사후점검에서 `Package/Windows/Save/Config/config_pmaker.json` 을 직접 열람 — `cam_port_min`/`cam_port_max` **둘 다 없다**(rpc_port/preset_file/carpos_file/camerapos_file/max_zoom 만 존재). 영향도 §H1 이 "가설이 아니라 실재하는 파일 상태"라고 지적한 그대로 현재도 유효하다 |
| 이 상태에서 H1 수정이 실제로 방어하는가 | **통과(등가 시나리오로 확인)** | QA C 항이 "두 키를 모두 제거해 패키지 배포본과 동일한 미지정 상태"를 **재현**해 검증했다(QA report:50). `Park3D/Save/Config/` 사본으로 테스트했을 뿐 `Package/Windows/` 실물 경로는 아니지만, `GetSaveRootDir()` 의 탐색 로직상 두 경로는 폴더 구조만 다르고 파일 처리 로직은 동일하므로 **기능적으로 등가**. 다만 패키지 exe 를 실제로 기동해 이 정확한 파일을 대상으로 재현하지는 않았다 |
| 재패키징 없이도 기존 패키지가 안전한가 | **통과(설계 특성)** | H1 수정 이후에는 최초 확장 시점에 `cam_port_min`+`cam_port_max` 를 함께 기록하므로, 패키지 config 에 처음부터 두 키가 없어도 **첫 확장을 겪는 순간부터 자가치유**된다. 재패키징이나 수동 파일 교체가 필수는 아니다(영향도 §6 이 지적한 "`Save/`가 UAT 스테이징 대상이 아니다"라는 기존 결함은 이번 변경과 무관하게 여전히 남아 있음 — 새로 발견된 문제 아님) |
| 읽기 전용 설치 경로에서의 동작(M4 실패 경로) | **미검증** | QA "M4 관련 비고"가 명시한 그대로 — 성공 경로만 재현되었고, 쓰기 실패 시 화면 알림 문구가 실제로 "확장했지만 config 기록은 실패"로 뜨는지는 재현되지 않았다 |

---

## 실패/미검증 요약 (오케스트레이터 보고용)

**실패: 0건.**

**미검증 3건** (구현 결함으로 보지 않으며, 실패로 되돌릴 근거는 없음 — 다만 다음 검증 기회에 다뤄야 함):
1. `cam.streamStatus`/`cam.list` RPC 실응답 확인 — QA 가 PID 27700 보호를 위해 의도적으로 RPC 를 쓰지 않아 이번 phase 전체에서 미실행.
2. "저장" UI 경로(`HandleSave` → `EnsureCamStreamPortRange`)의 실행 검증 — 코드 존재는 확인했으나 QA 는 로딩 경로만 재현.
3. M4(config 기록 실패 시 알림 문구) 실패 분기 — QA 가 이미 미검증으로 명시한 항목, 이번 사후점검도 이를 뒤집을 근거를 찾지 못함.

이 중 실패나 고위험으로 판단되는 항목은 없어 구현/QA 단계로 되돌릴 필요는 없다고 판단한다. 다만 세 항목 모두 "통과로 단정할 근거가 없다"는 의미이므로 최종 문서에 미검증으로 명시한다.
