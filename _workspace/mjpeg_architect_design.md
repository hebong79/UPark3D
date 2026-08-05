# 설계서 — Park3D MJPEG 스트리밍 (`GET /stream`)

- phase: `mjpeg`
- 작성: 2026-08-04
- 대상: `Park3D/Source/Park3D/Rpc/` (C++ 전용, 블루프린트 배제)
- 근거 원본: Unity `CRpcServer.cs` MJPEG 스트림 (`unity/20260724_224837_RPC_전체_API_레퍼런스.md` §1)
- 엔진: UE 5.8 (`Park3D.uproject: EngineAssociation 5.8`)

---

## 1. 요구사항

### 1.1 기능 요구 (FR)

| ID | 내용 |
|----|------|
| FR-1 | `GET /stream` 이 `multipart/x-mixed-replace; boundary=...` 로 JPEG 프레임을 연속 전송한다 |
| FR-2 | `<img src="http://host:13510/stream?camId=1">` 로 브라우저에서 바로 표시된다 |
| FR-3 | 쿼리 파라미터로 카메라·fps·품질을 지정한다 (Unity 파라미터 의미 계승) |
| FR-4 | 동시 스트림 수를 제한하고, 초과 시 503 으로 거부한다 (Unity `MaxConcurrentStreams=4`) |
| FR-5 | exe 단독 실행만으로 동작한다 — 외부 프로세스(파이썬 브리지) 의존 없음 |
| FR-6 | 기존 79개 RPC 메서드와 `/rpc`·`/health`·`/rpc/catalog` 동작에 영향이 없다 |

### 1.2 비기능 요구 (NFR)

| ID | 내용 |
|----|------|
| NFR-1 | 스트림이 장시간(시간 단위) 지속돼도 메모리가 단조 증가하지 않는다 |
| NFR-2 | 클라이언트가 느리거나 멈춰도 서버가 크래시하지 않는다 |
| NFR-3 | 클라이언트 연결이 끊기면 프레임 생산을 중단한다 (좀비 스트림 금지) |
| NFR-4 | 프레이밍·파라미터 파싱·세그먼트 정책은 RHI 비의존 순수 함수로 분리해 `-nullrhi` 유닛테스트 대상으로 만든다 (`RpcImageUtil`·`RpcAuth` 선례 계승) |
| NFR-5 | 인증 정책이 기존 `/rpc` 대비 **약화되지 않는다** |

---

## 2. 엔진 조사 결과 — 설계를 결정한 사실들

UE 5.8 `HTTPServer` 모듈 소스를 직접 읽어 확인했다. 5.6 이전에는 없던 기능이다.

### 2.1 스트리밍 응답은 두 가지 모드가 있다

`HttpServerResponse.h:19-31, 69-76`

| 모드 | 트리거 | 동작 |
|------|--------|------|
| **A. 큐 기반** | `StreamingBodyQueue` 설정 | 프로듀서가 청크를 enqueue, 게임 스레드 `WriteStream` 이 dequeue |
| **B. 콜백 재호출** | `MultipleWriteStream \| HasAdditionalWrites` | 응답을 다 쓴 뒤 연결이 `AwaitingProcessing` 으로 돌아가고, 핸들러가 `OnComplete` 를 다시 호출 |

### 2.2 함정 ① — 큐 모드 단독은 메모리가 무한 증가한다

`HttpConnectionResponseWriteContext.cpp:78, 86, 146`

```cpp
Response->Body.Append(Chunk);        // ← 계속 append 만 한다
...
BodyBytesWritten >= Response->Body.Num()   // 이미 보낸 바이트는 offset 으로만 추적
```

**보낸 바이트가 `Response->Body` 에서 제거되지 않는다.** 하나의 응답 객체로 계속 스트리밍하면 전송한 모든 프레임이 메모리에 누적된다.

> 실측 추정: 640×360 JPEG q70 ≈ 25KB × 10fps = 250KB/s → **1시간에 약 900MB**. NFR-1 위반.

### 2.3 함정 ② — 콜백 모드 단독은 크래시 위험이 있다

`HttpConnection.cpp:202-212`, `HttpResultCallback.h:11-15`

```cpp
if (EHttpConnectionState::Writing == SharedThisPtr->GetState()) {
    checkf(!SharedThisPtr->PendingResponse,
        TEXT("...received a third synchronous OnComplete while a response was already queued."));
```

대기 슬롯이 **1개뿐**이다. 이전 응답이 아직 소켓에 쓰이는 중인데 `OnComplete` 를 두 번 더 부르면 `checkf` 로 죽는다(Development 빌드에서 활성).

**그리고 엔진은 "쓰기가 끝났다"는 신호를 핸들러에게 주지 않는다.** 따라서 매 프레임 `OnComplete` 를 호출하는 순진한 구현은, 클라이언트가 느려지는 순간(예: 브라우저 탭을 백그라운드로 내림) 크래시한다. NFR-2 위반.

### 2.4 해법 — 두 모드를 결합한 "세그먼트 롤오버"

**세그먼트 내부는 큐 모드(A), 세그먼트 경계에서만 콜백 모드(B)로 응답 객체를 교체한다.**

```
[세그먼트 1]  Queue enqueue × N프레임  →  Complete=true
                                              ↓  CompleteWrite → HasAdditionalWrites
                                              ↓  → 상태: AwaitingProcessing
[세그먼트 2]  OnComplete(새 응답, SkipHeaderWrite, 새 Queue)   ← 여기서 Body 메모리 해제
              Queue enqueue × N프레임  →  ...
```

| 함정 | 해소 방식 |
|------|-----------|
| ① 메모리 | 세그먼트마다 응답 객체가 통째로 교체돼 `Body` 가 버려진다 → 상한 = 세그먼트 1개 크기 |
| ② 크래시 | `OnComplete` 호출은 **세그먼트당 1회**(약 6초에 1회). 대기 슬롯이 2개 이상 쌓일 수 없다 |

세그먼트가 바뀌어도 **같은 TCP 연결·같은 HTTP 응답 스트림**이라 클라이언트는 경계를 인지하지 못한다. 재연결이 필요 없다.

### 2.5 추가 확인 사항

| 확인 | 근거 | 결론 |
|------|------|------|
| 세그먼트 사이 대기 중 타임아웃? | `HttpConnection.cpp:65-66` — `case AwaitingProcessing: break;` | 타임아웃 없음. 무기한 대기 가능 |
| 프레임 사이 대기 중 타임아웃? | `HttpConnectionResponseWriteContext.cpp:108-118` — 스트리밍 진행 중이면 `ElapsedIdleTime=0` 리셋 | 안전 |
| 헤더에 `Content-Length` 가 붙나? | `ResetContext` 의 큐 분기는 `Content-Length` 를 추가하지 않음 (`:39-44` vs `:51-57`) | multipart 에 필수인 "길이 없음" 충족 |
| 연결 사망 후 `OnComplete` 호출은? | `HttpConnection.cpp:183-190` — Destroyed 상태면 조용히 폐기 | 크래시 안 함 |

---

## 3. 연결 종료 감지 (NFR-3)

엔진은 "클라이언트가 끊겼다"는 콜백을 주지 않는다. 대신 **소유권으로 추론한다.**

- 응답 객체는 `StreamingBodyQueue`(`TSharedPtr`)를 붙들고 있다.
- 연결이 파괴되면 `WriteContext` → `Response` 가 소멸하며 그 참조가 풀린다.
- 따라서 매니저가 들고 있는 `TSharedPtr` 이 `IsUnique() == true` 가 되면 **엔진이 응답을 버렸다 = 연결이 죽었다.**

```cpp
if (Session.Queue.IsUnique()) { /* 클라이언트 종료 → 세션 제거 */ }
```

> ⚠ 이 판정은 엔진 내부 수명 규약에 의존한다. 엔진 업그레이드 시 재확인 대상이며, 코드에 그 취지를 주석으로 남긴다. 백스톱으로 `maxSec` 상한(기본 0=무제한, 지정 시 강제 종료)을 함께 둔다.

---

## 4. 인터페이스

### 4.1 HTTP

```
GET /stream?camId=1&fps=5&quality=70&token=<TOKEN>
→ 200 OK
   Content-Type: multipart/x-mixed-replace; boundary=park3dframe
   Cache-Control: no-cache, private
```

| 파라미터 | 타입 | 기본 | 범위 | 의미 |
|----------|------|------|------|------|
| `camId` | int | 0 | ≥0 | 카메라 ID(1-based). 0 = 현재 선택 카메라 |
| `fps` | float | 5 | 1~30 | 목표 프레임률. 기본값은 QA 계측(1프레임 ≈ 48ms) 후 10→5 로 확정 |
| `quality` | int | 70 | 1~100 | JPEG 품질 |
| `maxSec` | int | 0 | 0~3600 | 0=무제한. 좀비 스트림 백스톱 |
| `token` | string | — | `[A-Za-z0-9_-]+` | 인증 토큰(§4.3) |

Unity 의 `preset_idx`/`pan`/`tilt`/`zoom` 은 **채택하지 않는다.** Park3D 는 PTZ 를 `cam.setPTZ`/`cam.setPan`/`cam.setTilt`/`cam.setZoom` RPC 로 이미 제어하며, 스트림 URL 이 카메라 상태를 바꾸면 여러 시청자가 서로의 화면을 조작하게 된다(§7 대안 F 참조).

| 응답 | 조건 |
|------|------|
| 200 + 스트림 | 정상 |
| 401 | 인증 실패 |
| 404 | `camId` 없음 / 월드·카메라 매니저 없음 |
| 503 | 동시 스트림 4개 초과 |

### 4.2 multipart 프레이밍

```
\r\n--park3dframe\r\n
Content-Type: image/jpeg\r\n
Content-Length: <N>\r\n
\r\n
<JPEG N바이트>
```

### 4.3 인증 — `?token=` 쿼리 허용 (설계 판단)

`<img src>` 는 **커스텀 헤더를 붙일 수 없다.** 기존 `X-Park3D-Token` 헤더 방식만 고수하면 FR-2 가 불가능하다.

판정 순서는 기존 `Park3DRpcAuth::Authorize` 를 **그대로 재사용**하되, `PresentedToken` 을 다음 순서로 뽑는다.

```
1. X-Park3D-Token 헤더 (있으면 우선 — 프로그램 클라이언트)
2. ?token= 쿼리        (없으면 폴백 — <img> 용)
```

즉 **판정 로직·루프백 폴백·Origin 거부는 전혀 바뀌지 않고, 토큰의 운반 경로만 하나 늘어난다.** NFR-5 충족.

- `Origin` 헤더 거부(D11)는 유지된다. `<img>` 로드는 `Origin` 을 보내지 않으므로 정상 통과하고, `fetch()` 경유 크로스오리진 시도는 지금처럼 거부된다.
- **알려진 트레이드오프**: URL 에 실린 토큰은 브라우저 히스토리·`Referer`·프록시 로그에 남을 수 있다. 서버는 쿼리스트링을 로깅하지 않는다. 사설망 + 방화벽 출발지 한정이라는 기존 전제(`RpcAuth.h:39`) 안에서 수용한다.

---

## 5. 클래스 / 데이터 구조

```
Park3D/Source/Park3D/Rpc/
├─ MjpegStream.h/.cpp          [신규] 순수 로직 — 프레이밍·파라미터·세그먼트 정책 (RHI 비의존)
├─ MjpegStreamManager.h/.cpp   [신규] 세션 수명·티커·프레임 생산 (게임 스레드)
├─ RpcServerSubsystem.h/.cpp   [수정] /stream 라우트 바인드 + HandleStream
└─ RpcAuth.h/.cpp              [수정] 토큰 추출 순서 헬퍼 1개 추가(순수 함수)
```

### 5.1 `MjpegStream.h` (순수 — 유닛테스트 대상)

```cpp
namespace Park3DMjpeg
{
    extern const TCHAR* const BoundaryToken;          // "park3dframe"
    FString  ContentTypeValue();                      // multipart/x-mixed-replace; boundary=park3dframe

    /** JPEG 바이트 → multipart 파트 1개(헤더+본문). */
    void BuildPart(const TArray<uint8>& Jpeg, TArray<uint8>& OutPart);

    struct FStreamParams
    {
        int32 CamId   = 0;
        float Fps     = 10.f;
        int32 Quality = 70;
        int32 MaxSec  = 0;
    };
    /** 쿼리 → 파라미터. 범위 밖은 clamp(거부하지 않음 — 뷰어 URL 오타로 스트림이 죽지 않게). */
    FStreamParams ParseParams(const TMap<FString, FString>& Query);

    /** 토큰 운반 경로 선택: 헤더 우선, 없으면 쿼리. */
    FString PickToken(const FString& HeaderToken, const FString& QueryToken);

    /** 세그먼트 롤오버 판정. 둘 중 하나라도 넘으면 true. */
    struct FSegmentPolicy
    {
        int32 MaxBytes  = 4 * 1024 * 1024;   // 4MB
        int32 MaxFrames = 60;                // 10fps 기준 6초
        bool ShouldRollover(int32 Bytes, int32 Frames) const;
    };
}
```

### 5.2 `MjpegStreamManager.h` (게임 스레드)

```cpp
class FMjpegStreamManager
{
public:
    explicit FMjpegStreamManager(TFunction<UWorld*()> InWorldGetter);
    ~FMjpegStreamManager();                       // 모든 세션 종료 + 티커 해제

    static constexpr int32 MaxConcurrentStreams = 4;

    /** 세션 개설. false 면 OutStatus 코드로 거부 응답(404/503). */
    bool BeginStream(const Park3DMjpeg::FStreamParams& Params,
                     const FHttpResultCallback& OnComplete,
                     int32& OutHttpCode, FString& OutError);

    int32 NumActive() const { return Sessions.Num(); }

private:
    struct FSession
    {
        Park3DMjpeg::FStreamParams Params;
        FHttpResultCallback OnComplete;                                  // 복사본 보관(수명 = 세션)
        TSharedPtr<TQueue<TArray<uint8>, EQueueMode::Spsc>> Queue;
        TSharedPtr<TAtomic<bool>> Complete;
        double NextFrameTime = 0.0;
        double StartTime     = 0.0;
        int32  SegBytes  = 0;
        int32  SegFrames = 0;
    };

    bool Tick(float DeltaTime);                    // FTSTicker (게임 스레드)
    void OpenSegment(FSession& S, bool bFirst);    // 응답 객체 생성 + OnComplete 호출
    bool ProduceFrame(FSession& S, TArray<uint8>& OutPart);   // CaptureOnce → ReadPixels → JPEG → 파트

    TFunction<UWorld*()> WorldGetter;
    TArray<TUniquePtr<FSession>> Sessions;
    FTSTicker::FDelegateHandle TickerHandle;
};
```

---

## 6. 처리 흐름

### 6.1 스트림 개설

```
GET /stream  (게임 스레드)
 └ HandleStream
    ├ 인증: PickToken(헤더, 쿼리) → Park3DRpcAuth::Authorize → 실패 시 401 완결
    ├ ParseParams(QueryParams)
    ├ Manager->BeginStream(...)
    │   ├ 슬롯 초과 → 503
    │   ├ 카메라 해석 실패 → 404
    │   └ 세션 생성 → OpenSegment(bFirst=true)
    │        └ 응답 = {200, multipart 헤더, Flags = MultipleWriteStream|HasAdditionalWrites,
    │                  StreamingBodyQueue = Queue, StreamingBodyComplete = Complete}
    │           → OnComplete(MoveTemp(응답))
    └ return true          // 라우트 핸들러는 즉시 반환. 이후는 티커가 이어간다
```

### 6.2 티커 (매 프레임)

```
for each Session:
    if Queue.IsUnique():                     → 세션 제거 (클라이언트 종료)
    if MaxSec 초과:      Complete=true       → 세션 제거 (백스톱)
    if now < NextFrameTime:                  → skip (fps 페이싱)
    if !Queue->IsEmpty():                    → skip (백프레셔: 느린 클라이언트면 프레임 드롭)

    ProduceFrame → 실패 시 세션 제거
    if Policy.ShouldRollover(SegBytes, SegFrames):
        Complete->Store(true)                // 현 세그먼트 종료 지시
        OpenSegment(bFirst=false)            // 새 응답(SkipHeaderWrite) + 새 Queue → Body 메모리 해제
        SegBytes = SegFrames = 0
    Queue->Enqueue(Part)
    NextFrameTime += 1/fps
```

**백프레셔 설계**: 큐가 비었을 때만 다음 프레임을 만든다. 느린 클라이언트에는 프레임이 드롭될 뿐 큐도 메모리도 늘지 않는다.

### 6.3 프레임 생산 (`ProduceFrame`)

```
Cam = Manager(카메라매니저)->GetCamera(camId-1)     // 선택 상태를 바꾸지 않는다
Cam->CaptureOnce()                                  // = Capture->CaptureScene()
RT = Cam->RenderTarget                              // 없으면 실패
Res = RT->GameThread_GetRenderTargetResource()      // 없으면 실패(-nullrhi)
Res->ReadPixels(Bitmap, Flags{RCM_UNorm, LinearToGamma=false})   // cam.captureJPG 와 동일 규약
RpcImage::EncodeColors(Bitmap, W, H, bPng=false, Quality, Jpeg)  // 기존 함수 재사용
Park3DMjpeg::BuildPart(Jpeg, OutPart)
```

`cam.captureJPG` 의 `DoCapture` 와 픽셀 규약이 **동일**하다. base64 를 거치지 않는 점만 다르다.

> 카메라 선택(`SelectCamera`)을 건드리지 않는 것이 중요하다. `cam.captureJPG` 는 `camId` 지정 시 선택을 전환하지만, 스트림이 그러면 시청 행위가 에디터 UI 상태를 바꾼다(§7 대안 C).

---

## 7. 대안 비교

| 대안 | 내용 | 채택 | 사유 |
|------|------|------|------|
| **A. 큐 모드 단독** | 응답 1개로 계속 enqueue | ✕ | §2.2 메모리 무한 증가 |
| **B. 콜백 모드 단독** | 매 프레임 `OnComplete` | ✕ | §2.3 느린 클라이언트에서 `checkf` 크래시 |
| **C. 세그먼트 롤오버(A+B)** | 세그먼트 내부 큐 + 경계에서 응답 교체 | **✓** | 메모리 상한 + 크래시 회피 + 재연결 불필요 |
| D. `/snapshot` + JS 폴링 | REST 단발 + 클라이언트 반복 | ✕ | MJPEG 아님. 다만 C 실패 시 폴백으로 남김 |
| E. 파이썬 브리지 중계 | UE 는 스냅샷만, 파이썬이 MJPEG | ✕ | FR-5(exe 단독) 위반 |
| F. Unity 처럼 URL 로 PTZ 제어 | `?pan=&tilt=&zoom=` | ✕ | 시청이 카메라 상태를 변경 → 다중 시청자 간섭. PTZ 는 RPC 로 |

---

## 8. 테스트 포인트

### 8.1 유닛 테스트 (`-nullrhi` 가능, RHI 비의존)

| ID | 대상 | 검증 |
|----|------|------|
| U-1 | `BuildPart` | 바운더리·`Content-Type`·`Content-Length`·CRLF 배치가 정확한가 |
| U-2 | `BuildPart` | 본문 바이트가 무손상으로 실려 있는가(길이·앞뒤 바이트) |
| U-3 | `ParseParams` | 기본값 / clamp(fps 0→1, 999→30; quality 0→1, 200→100) |
| U-4 | `ParseParams` | 잘못된 문자열은 기본값으로 폴백 |
| U-5 | `PickToken` | 헤더 우선, 헤더 없으면 쿼리, 둘 다 없으면 빈 문자열 |
| U-6 | `FSegmentPolicy` | 바이트·프레임 경계 각각에서 롤오버 판정 |
| U-7 | `ContentTypeValue` | 바운더리 문자열이 파트 구분자와 일치 |

### 8.2 통합/실동작 (실 RHI 필요)

| ID | 검증 | 방법 |
|----|------|------|
| I-1 | `curl -N http://localhost:13510/stream` 이 프레임을 연속 반환 | 60초 수신, 바운더리 개수 ≈ fps×60 |
| I-2 | 세그먼트 롤오버가 스트림을 끊지 않음 | 60프레임(1세그먼트) 초과 수신 확인 |
| I-3 | **메모리 상한** | 5분 스트리밍 중 프로세스 working set 증가가 세그먼트 크기 수준 |
| I-4 | 느린 클라이언트에서 크래시 없음 | 수신 후 읽기 중단 → 30초 유지 → 크래시/어서션 없음 |
| I-5 | 연결 종료 감지 | curl 중단 후 `NumActive()==0`(로그로 확인) |
| I-6 | 동시 4개 초과 시 503 | 5개 동시 요청 |
| I-7 | 인증 | 토큰 설정 시 무토큰 401, `?token=` 200 |
| I-8 | 회귀 | 기존 Automation 54개 + `system.catalog` 79개 |

---

## 9. 영향 범위 (사전)

| 대상 | 영향 |
|------|------|
| `RpcServerSubsystem` | 라우트 1개 추가, 멤버 1개 추가. 기존 4개 라우트 로직 불변 |
| `RpcAuth` | 순수 함수 추가만. 기존 `Authorize` 시그니처·판정 불변 |
| `RpcImage::EncodeColors` | 호출자만 추가. 함수 불변 |
| `PTZCameraActor` | **변경 없음**. 기존 `CaptureOnce`/`RenderTarget` 만 읽는다 |
| `CameraControlManager` | **변경 없음**. `GetCamera()` 조회만. 선택 상태 미변경 |
| `Park3D.Build.cs` | **변경 없음** — `HTTPServer`·`ImageWrapper` 이미 의존 |
| 게임 스레드 부하 | 스트림당 `CaptureScene`+`ReadPixels`+JPEG 인코딩. 10fps×4스트림이 상한 |
| 패키지 exe | 재패키징 필요(C++ 변경) |

**최대 위험**: 게임 스레드 프레임 시간. `ReadPixels` 는 렌더 스레드 플러시를 유발한다. 4스트림 × 10fps = 초당 40회 플러시는 과하다 → **I-3/I-4 계측 결과에 따라 기본 fps 를 낮추거나 동시 스트림 수를 줄인다.** 이 값은 설계에서 확정하지 않고 계측 후 결정한다.

---

## 10. 미해결 / 확인 필요

| ID | 항목 | 처리 |
|----|------|------|
| O-1 | `Queue.IsUnique()` 종료 감지가 엔진 수명 규약 의존 | 코드 주석 + `maxSec` 백스톱. 엔진 업그레이드 시 재확인 |
| O-2 | 게임 스레드 부하 실측치 미확보 | QA I-3/I-4 에서 계측 후 기본값 확정 |
| O-3 | 브라우저별 `multipart/x-mixed-replace` 지원 | Chrome/Edge 확인 대상. Safari 는 지원이 제한적으로 알려져 있음(미검증) |
| O-4 | 토큰이 URL 에 노출되는 트레이드오프 | §4.3 에 명시. 사용자 승인 필요 사항으로 보고 |
