# 사후 영향도 분석 (rpcserver Phase 5 — cam 캡처)

작성일: 2026-07-25

## 변경/신규 파일
| 파일 | 변경 |
|---|---|
| `Rpc/RpcImageUtil.h/.cpp` | 신규(FColor→JPEG/PNG 인코딩 + base64, RHI 비의존) |
| `Rpc/Modules/CamRpcModule.cpp` | captureJPG/PNG의 -32000 → 실구현 교체(DoCapture/ResolveCaptureCam 헬퍼 추가) |
| `Park3D.Build.cs` | PrivateDependency에 `ImageWrapper` 추가 |
| `Tests/RpcServerTest.cpp` | ImageUtil 테스트 1건 + CamModule captureJPG 라벨 갱신 + Base64 include |

## 의존성/회귀
- 추가형 + 기존 -32000 2건 교체. 매니저/액터 코드 **무수정**(SelectCamera/CaptureOnce/RenderTarget 기존 API 호출만).
- 신규 엔진 의존 `ImageWrapper`(엔진 표준 모듈, 위험 낮음). Build.cs 1개 추가.
- catalog **79 불변**(captureJPG/PNG는 이미 등록돼 있었음 — 동작만 실구현화). **미구현 12 → 10**.

## 데이터 권위/스레딩
- 캡처는 카메라 자체 `RenderTarget`(APTZCameraActor) 읽기 — 신규 상태 없음.
- HTTP 콜백은 게임 스레드 → `GameThread_GetRenderTargetResource()`/`ReadPixels` 안전. 인코딩(ImageWrapper)도 동기.

## 검증 (RHI 분리 전략)
- **인코딩 순수함수**(RpcImage): 합성 픽셀로 유닛테스트(RHI 불필요). PNG 시그니처/JPEG SOI/base64 왕복 확인.
- **실 캡처**(ReadPixels): GPU 필요 → 실RHI `-game`(windowed) HTTP 스모크로 검증.
- 헤드리스 `-nullrhi`: 렌더 리소스/픽셀 없음 → captureJPG **-32000**(크래시 없음). CamModule 자동화가 이를 확인.

## 결과
- UBT 빌드 성공(경고 0).
- 자동화 `Park3D.Rpc.*` **9개** 전부 Success(신규 ImageUtil 포함, 0 Fail).
- 실RHI HTTP 스모크: captureJPG(1280×720, 45,044B, SOI=0xFFD8), capturePNG(1280×720, 952,828B, sig=0x89504E47), 잘못된 camId→-32000, catalog=79.

## 제약
- base64 페이로드 큼(PNG 1280×720 ≈ 950KB, JPEG ≈ 45KB). 대량/고빈도 호출 시 HTTP 응답 크기·메모리 유의. `quality` 낮추면 JPEG 축소 가능.
- 캡처 색공간: `SCS_FinalColorLDR`(톤매핑) 기준. `SetLinearToGamma(false)`로 이중 감마 방지.
- `-nullrhi` 환경(순수 헤드리스 CI)에서는 실 캡처 불가(설계상 -32000). 실 픽셀은 GPU 환경 필요.
