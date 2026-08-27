// Copyright Epic Games, Inc. All Rights Reserved.
// PlateGlyphAtlas : 번호판 번호를 **거리장(SDF) 텍스처**로 합성한다.
//
// 왜 지오메트리(Text3D) 가 아니라 텍스처인가 — 실측 두 가지 때문이다(Docs/20260827_161319).
//   ① 압출 측벽이 화면에 2.5px 나와도 명암 대비가 0 이었다. 글자 알베도가 0.015 라 디퓨즈가
//      전부 0 으로 깎여 법선이 어디를 보든 같은 색이 나온다. 즉 두께 문제가 아니라 셰이딩 문제다.
//   ② 원거리에서 획 굵기가 0.7px 이 되면 지오메트리는 **그냥 사라진다**(밉이 없다).
//      실측: 판 폭 47px 에서 번호 오른쪽 절반이 통째로 판 흰색으로 나왔다.
// 텍스처는 밉·이방성 필터가 회색으로 수렴시켜 ②가 사라지고, 머티리얼이 노멀·거칠기·가림을
// 함께 내므로 ①도 사라진다.
//
// 글자 집합이 닫혀 있다는 점이 이 방식을 싸게 만든다 — 숫자 10 + 승용 한글 24 뿐이다
// (`ACarActor::MakeDeterministicPlateNumber`). 그래서 **차량마다가 아니라 글자마다 한 번** 굽고,
// 런타임은 아틀라스에서 어드밴스 박스만 잘라 이어 붙인다.
//
// ── 왜 GPU 렌더타깃이 아니라 CPU 합성인가 ─────────────────────────────────────
// 처음에는 `UTextureRenderTarget2D` + Canvas 로 합성하고 `bAutoGenerateMips` 로 밉을 맡겼다.
// **밉이 채워지지 않았다.** 밉 단수는 11 로 잡히는데 내용이 비어, 패키지 실행에서 줌 24 이상은
// 번호가 멀쩡하고 **줌 16 이하는 통째로 사라졌다**(줌 20 에서만 희미 — 트라이리니어가 밉0 과 빈 밉1
// 을 섞는 구간). `bCanCreateUAV` 를 켜도 같았다.
//
// SDF 축소 자체는 멀쩡하다는 것을 따로 확인했다 — 같은 데이터를 박스 필터로 내려 보니
// **밉5(32x8)까지 글자 안쪽 비율이 0.23 으로 유지**됐다. 그러니 원인은 축소가 아니라 엔진의
// 밉 생성이다. 8분짜리 재쿡으로 엔진 내부를 추측하는 대신 밉을 우리가 만든다 —
// 결정적이고, 축소 방식을 우리가 고르고, 아틀라스를 **에셋이 아니라 PNG** 로 둘 수 있다
// (`Content/` 는 gitignore 라 에셋은 fresh clone 에서 사라진다).

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "PlateGlyphAtlas.generated.h"

class UTexture2D;

/** 아틀라스 한 칸. `Tools/plate_sdf/bake_glyph_sdf.py` 가 내보내는 metrics JSON 과 1:1 이다. */
struct FPlateGlyphCell
{
	int32 Col = 0;
	int32 Row = 0;
	/** 어드밴스 폭(셀 픽셀). 숫자는 전부 같다 — 실물 번호판이 고정폭이다(피치 5.01cm 실측). */
	float Advance = 0.f;
	/** 셀 안에서 어드밴스 박스가 시작하는 x(셀 픽셀). 이 박스만 잘라 붙인다. */
	float BoxX0 = 0.f;
};

/**
 * 글리프 SDF 아틀라스를 한 번 읽어 두고, 번호 문자열을 텍스처로 합성해 준다.
 *
 * 게임 인스턴스 수명에 붙인다 — 차량 23대가 같은 아틀라스를 쓰고, 액터마다 로드하면
 * 같은 PNG·JSON 을 23번 읽게 된다.
 */
UCLASS()
class PARK3D_API UPlateGlyphAtlasSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * 아틀라스와 메트릭은 **둘 다 `Save/Config/`** 에 둔다.
	 * `Content/` 는 gitignore 라 에셋으로 두면 fresh clone 에서 사라지고, 쿠커도 참조가 없으면
	 * 안 굽는다(실제로 첫 쿡에서 `SkipPackage` 로 빠졌다). `Save/` 는 BuildPackage.bat 가
	 * robocopy 로 통째로 스테이징하므로 패키지에서도 그대로 읽힌다.
	 */
	static const TCHAR* GetAtlasFileName() { return TEXT("plate_glyph_sdf.png"); }
	static const TCHAR* GetMetricsFileName() { return TEXT("plate_glyph_metrics.json"); }

	/** Save/Config/<파일명> 절대 경로. */
	static FString GetConfigFilePath(const TCHAR* FileName);

	/** 아틀라스와 메트릭이 모두 준비됐는가. 아니면 번호를 못 그린다(폴백 위젯이 남는다). */
	bool IsReady();

	/**
	 * 번호 문자열을 SDF 텍스처(밉 포함)로 합성한다. 실패하면 nullptr.
	 *
	 * @param Outer         텍스처의 소유자(보통 차량 액터). 액터가 죽으면 같이 정리된다.
	 * @param DisplayText   "672우 3269" 처럼 공백이 든 표시용 문자열.
	 * @param PlateAspect   판 앞면의 가로/세로 비(폭cm / 높이cm). 판 메시에서 읽어 넘긴다 —
	 *                      여기서 상수로 박으면 판 메시를 갈아 끼웠을 때 글자만 늘어난다.
	 */
	UTexture2D* BuildNumberSdf(UObject* Outer, const FString& DisplayText, double PlateAspect);

	/** 합성 텍스처 크기(판 앞면 전체를 덮는다). 머티리얼의 SdfTexelU/V 와 같은 값이어야 한다. */
	static constexpr int32 TexWidth = 1024;
	static constexpr int32 TexHeight = 256;

private:
	void EnsureLoaded();

	/** 로드 시도를 한 번만 하기 위한 표식. 실패해도 매번 다시 읽지 않는다. */
	bool bLoadAttempted = false;

	/** 아틀라스 그레이스케일 픽셀(가로 AtlasW × 세로 AtlasH). GPU 를 거치지 않는다. */
	TArray<uint8> AtlasPixels;
	int32 AtlasW = 0;
	int32 AtlasH = 0;

	TMap<TCHAR, FPlateGlyphCell> Cells;
	float CellSize = 0.f;
	float Baseline = 0.f;
	float FontSize = 0.f;
	float SpaceAdvance = 0.f;
};
