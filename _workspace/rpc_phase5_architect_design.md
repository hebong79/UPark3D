# RPC Phase 5 설계서 — cam.captureJPG / cam.capturePNG

작성일: 2026-07-25
대상: `unity/20260724_224837_RPC_전체_API_레퍼런스.md` §9 캡처(2)
전제: Phase 1~4 완성(79 노출). cam.captureJPG/PNG는 현재 등록/미구현(-32000) → 실동작 전환.

---

## 1. 요구사항

| method | params | 응답 |
|---|---|---|
| `cam.captureJPG` | `camId`?(생략=선택), `quality`?=85 | `{img_bytes(base64), width, height, format:"jpg", camId}` |
| `cam.capturePNG` | `camId`?(생략=선택) | `{img_bytes(base64), width, height, format:"png", camId}` |

- `img_bytes`는 base64 문자열(파일 저장 안 함).
- `camId` 지정 시 해당 카메라로 **선택 전환 후** 캡처. 생략 시 현재 선택 카메라.

## 2. 백엔드 (기존 자산 재사용)

- `APTZCameraActor::Capture`(USceneCaptureComponent2D, `SCS_FinalColorLDR` 톤매핑) → `RenderTarget`(UTextureRenderTarget2D, `RTF_RGBA8`, 기본 1280×720).
- `CaptureOnce()`(=`Capture->CaptureScene()`) 로 프레시 캡처.
- `ACameraControlManager::SelectCamera(idx)`(선택 전환 + 캡처 활성 + 1회 캡처).
- 신규 엔진 의존: **ImageWrapper**(JPEG 품질 인코딩). PNG도 동일 경로 통일.

## 3. 클래스/데이터 구조

**신규 유틸** `Rpc/RpcImageUtil.h/.cpp` — RHI 비의존 순수 인코딩(유닛테스트 대상):
```cpp
namespace RpcImage
{
    // FColor(BGRA) 픽셀 → JPEG/PNG 바이트. bPng=false면 JPEG(Quality 0~100).
    bool EncodeColors(const TArray<FColor>& Pixels, int32 W, int32 H, bool bPng, int32 Quality, TArray<uint8>& OutBytes);
    // 바이트 → base64 문자열.
    FString ToBase64(const TArray<uint8>& Bytes);
}
```
> 인코딩을 모듈에서 분리하는 이유: ReadPixels는 GPU(RHI) 필요라 헤드리스 `-nullrhi`에서 검증 불가. **인코딩 순수함수는 합성 픽셀로 유닛테스트** 가능(실 픽셀 캡처는 실RHI `-game` 스모크로 검증).

**CamRpcModule**: 기존 captureJPG/PNG의 `NotImplemented` 2건을 실구현으로 교체.

## 4. 처리 흐름 (핸들러)

```
captureJPG/PNG:
  Mgr = GetCameraManager
  camId = Has("camId") ? RequireInt : SelectedIndex+1
  Cam = GetCamById(camId)  // 범위 밖 -32000
  camId 지정됐으면 Mgr->SelectCamera(camId-1)   // 선택 전환(캡처 활성)
  Cam->CaptureOnce()                            // 프레시 프레임
  RT = Cam->RenderTarget; RTRes = RT->GameThread_GetRenderTargetResource()
  RTRes 없음(-nullrhi) → -32000("렌더 리소스 없음 — 실RHI 필요")
  RTRes->ReadPixels(Bitmap, FReadSurfaceDataFlags(RCM_UNorm))
  Bitmap 비었으면 -32000
  RpcImage::EncodeColors(Bitmap, RT->SizeX, RT->SizeY, bPng, quality, Bytes)
  img = RpcImage::ToBase64(Bytes)
  → {img_bytes:img, width:SizeX, height:SizeY, format:"jpg"|"png", camId}
```

## 5. 인코딩 구현

- `IImageWrapperModule::CreateImageWrapper(EImageFormat::JPEG|PNG)` → `SetRaw(pixels, len, W, H, ERGBFormat::BGRA, 8)` → `GetCompressed(Quality)`.
  - FColor 메모리 배치는 BGRA → `ERGBFormat::BGRA`.
  - JPEG: Quality(1~100, 기본 85). PNG: Quality 무시(무손실).
- base64: `FBase64::Encode(Bytes.GetData(), Bytes.Num())`.

## 6. 대안 비교

| 안 | 내용 | 채택 |
|---|---|---|
| A. FImageUtils(PNG만) | Engine 내장, JPEG 품질 불가 | ✗ JPEG quality 요구 미충족 |
| **B. ImageWrapper(JPEG+PNG 통일) (채택)** | 한 경로로 두 포맷+품질 | ✅ 스펙 충족, 코드 단순 |
| C. 파일 저장 후 경로 반환 | 과거 문서 방식 | ✗ 레퍼런스가 base64 명시(파일 저장 안 함) |

## 7. 테스트 포인트

**자동화 `Park3D.Rpc.ImageUtil`**(신규, RHI 비의존):
1. 합성 픽셀(4×4 빨강 FColor) → EncodeColors PNG → 바이트 non-empty, PNG 시그니처(89 50 4E 47) 확인.
2. 동일 픽셀 → EncodeColors JPEG(q=85) → 바이트 non-empty, JPEG SOI(FF D8) 확인.
3. ToBase64 → non-empty, 디코드 왕복 길이 일치.

**CamModule 자동화 보강**: `-nullrhi` 환경에서 captureJPG는 렌더 리소스 없음 → -32000(정직 실패)까지 확인(크래시 없음).

**실RHI HTTP 스모크**: `-game`(nullrhi 제외, 윈도우드)로 실제 캡처 → img_bytes 길이>0, width=1280/height=720, format 일치.

## 8. 영향도 (사전)

- 추가형 + 기존 -32000 2건 교체. 신규 파일 2개(RpcImageUtil) + CamRpcModule 2핸들러 수정 + Build.cs ImageWrapper 1개.
- 매니저/액터 코드 무수정(SelectCamera/CaptureOnce/RenderTarget 기존 API 호출만).
- catalog 수 불변(79) — 이미 등록된 method의 동작만 -32000→실동작. 미구현 12→10.
- 리스크: (a) 헤드리스 nullrhi에서 실캡처 불가 → 인코딩은 유닛, 실캡처는 실RHI 스모크로 분리 검증. (b) base64 페이로드 큼(1280×720 JPEG ≈ 수십~수백 KB) — HTTP 응답 크기 주의(정상 범위).
