// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlateGlyphAtlas.h"

#include "../Park3DDataPaths.h"
#include "PlateKinds.h"
#include "PlateLayout.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	// ─── 실물 번호판 실측값 ────────────────────────────────────────────────
	// Wikimedia Commons "Republic of Korea euro license plate.jpg"(1474x322)를 판 폭 52.1cm 로
	// 환산해 쟀다. 이미지 종횡비 4.578 vs 실제 4.727 이라 세로는 3.3% 보정했다.
	//
	//   파란 KOR 띠 오른쪽 끝  5.62 cm
	//   번호 잉크 7.14 ~ 49.84 cm,  어드밴스 박스로는 6.79 ~ 50.19 cm
	//   글자 높이 7.08 cm,  숫자 피치 5.01 cm(균일),  세로 중심 = 판 중앙
	//
	// 이전 구현은 글자 높이가 5.78cm 로 실물보다 22% 작았다. 위젯 여백(80/520 등)에서
	// 유도한 값이었는데 그 여백 자체가 실물 기준이 아니었다.
	constexpr double AreaLeftFrac = 6.79 / 52.10;
	constexpr double AreaRightFrac = (52.10 - 50.19) / 52.10;

	// 2019 개정에서 "문자 폭과 자간 폭이 소폭 좁아졌다". 우리가 가진 수성돋움체는 넓은 쪽이라
	// 높이를 실물에 맞추면 글자가 판을 넘친다 → 가로만 좁힌다.
	//   실물 어드밴스합/글자높이 = 6.198,  우리 = 7.272  → 0.853
	constexpr double CondenseX = 0.853;

	// 캡 높이 / em. 수성돋움체 실측(숫자 bbox 높이 168 / 크기 200).
	constexpr double CapPerEm = 0.84;

	/** 그레이스케일 버퍼에서 바이리니어로 읽는다. 밖은 가장자리 값으로 클램프한다. */
	FORCEINLINE float SampleBilinear(const TArray<uint8>& Pix, int32 W, int32 H, double X, double Y)
	{
		const double Cx = FMath::Clamp(X, 0.0, static_cast<double>(W - 1));
		const double Cy = FMath::Clamp(Y, 0.0, static_cast<double>(H - 1));
		const int32 X0 = FMath::FloorToInt32(Cx);
		const int32 Y0 = FMath::FloorToInt32(Cy);
		const int32 X1 = FMath::Min(X0 + 1, W - 1);
		const int32 Y1 = FMath::Min(Y0 + 1, H - 1);
		const float Fx = static_cast<float>(Cx - X0);
		const float Fy = static_cast<float>(Cy - Y0);
		const float A = Pix[Y0 * W + X0], B = Pix[Y0 * W + X1];
		const float C = Pix[Y1 * W + X0], D = Pix[Y1 * W + X1];
		return FMath::Lerp(FMath::Lerp(A, B, Fx), FMath::Lerp(C, D, Fx), Fy);
	}
}

FString UPlateGlyphAtlasSubsystem::GetConfigFilePath(const TCHAR* FileName)
{
	return FPaths::Combine(Park3DDataPaths::GetSaveRootDir(), TEXT("Config"), FileName);
}

UPlateGlyphAtlasSubsystem* UPlateGlyphAtlasSubsystem::Resolve(const UWorld* World)
{
	if (World)
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			if (UPlateGlyphAtlasSubsystem* Atlas = GI->GetSubsystem<UPlateGlyphAtlasSubsystem>())
			{
				return Atlas;
			}
		}
	}
	// 독립 인스턴스(게임 인스턴스가 없을 때). 루트에 걸어 GC 에서 지킨다.
	// 서브시스템 클래스는 ClassWithin=UGameInstance 라 Package 를 outer 로 만들면 ensure 가 뜬다
	// (Automation 실측: "created in invalid Outer /Script/CoreUObject.Package") → 빈 UGameInstance 를 outer 로
	// 만들어 준다(Init 은 부르지 않는다 — 파일만 읽는다).
	static UPlateGlyphAtlasSubsystem* Standalone = nullptr;
	if (!Standalone)
	{
		UGameInstance* Outer = NewObject<UGameInstance>(GetTransientPackage());
		Outer->AddToRoot();
		Standalone = NewObject<UPlateGlyphAtlasSubsystem>(Outer);
		Standalone->AddToRoot();
	}
	return Standalone;
}

void UPlateGlyphAtlasSubsystem::EnsureLoaded()
{
	if (bLoadAttempted)
	{
		return;
	}
	bLoadAttempted = true;

	// ── 아틀라스 PNG ──────────────────────────────────────────────────────
	const FString AtlasPath = GetConfigFilePath(GetAtlasFileName());
	TArray<uint8> Encoded;
	if (!FFileHelper::LoadFileToArray(Encoded, *AtlasPath))
	{
		UE_LOG(LogTemp, Warning, TEXT("[PlateSDF] 아틀라스를 못 읽었다: %s"), *AtlasPath);
	}
	else
	{
		IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		const TSharedPtr<IImageWrapper> Png = Module.CreateImageWrapper(EImageFormat::PNG);
		TArray<uint8> Raw;
		if (Png.IsValid() && Png->SetCompressed(Encoded.GetData(), Encoded.Num())
			&& Png->GetRaw(ERGBFormat::Gray, 8, Raw))
		{
			AtlasW = Png->GetWidth();
			AtlasH = Png->GetHeight();
			AtlasPixels = MoveTemp(Raw);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[PlateSDF] 아틀라스 PNG 디코드 실패: %s"), *AtlasPath);
		}
	}

	// ── 메트릭 JSON ───────────────────────────────────────────────────────
	const FString MetricsPath = GetConfigFilePath(GetMetricsFileName());
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *MetricsPath))
	{
		UE_LOG(LogTemp, Warning, TEXT("[PlateSDF] 메트릭을 못 읽었다: %s"), *MetricsPath);
		return;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[PlateSDF] 메트릭 JSON 파싱 실패: %s"), *MetricsPath);
		return;
	}

	// GetNumberField 는 double 을 돌려준다 — 좁히기 경고를 피하려면 명시적으로 캐스팅한다.
	CellSize = static_cast<float>(Root->GetNumberField(TEXT("cell")));
	Baseline = static_cast<float>(Root->GetNumberField(TEXT("baseline")));
	FontSize = static_cast<float>(Root->GetNumberField(TEXT("fontSize")));
	SpaceAdvance = static_cast<float>(Root->GetNumberField(TEXT("spaceAdvance")));
	if (Root->HasTypedField<EJson::Number>(TEXT("spread")))
	{
		Spread = static_cast<float>(Root->GetNumberField(TEXT("spread")));
	}

	int32 InkFromMetrics = 0;
	const TArray<TSharedPtr<FJsonValue>>* Glyphs = nullptr;
	if (Root->TryGetArrayField(TEXT("glyphs"), Glyphs))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Glyphs)
		{
			const TSharedPtr<FJsonObject> Obj = Value->AsObject();
			if (!Obj.IsValid())
			{
				continue;
			}
			const FString Char = Obj->GetStringField(TEXT("char"));
			if (Char.IsEmpty())
			{
				continue;
			}
			FPlateGlyphCell Cell;
			Cell.Col = Obj->GetIntegerField(TEXT("col"));
			Cell.Row = Obj->GetIntegerField(TEXT("row"));
			Cell.Advance = static_cast<float>(Obj->GetNumberField(TEXT("advance")));
			Cell.BoxX0 = static_cast<float>(Obj->GetNumberField(TEXT("boxX0")));
			if (Obj->HasTypedField<EJson::Number>(TEXT("inkX0")))
			{
				Cell.InkX0 = static_cast<float>(Obj->GetNumberField(TEXT("inkX0")));
				Cell.InkY0 = static_cast<float>(Obj->GetNumberField(TEXT("inkY0")));
				Cell.InkX1 = static_cast<float>(Obj->GetNumberField(TEXT("inkX1")));
				Cell.InkY1 = static_cast<float>(Obj->GetNumberField(TEXT("inkY1")));
				++InkFromMetrics;
			}
			else
			{
				// 옛 메트릭 — 어드밴스 박스 × 캡 높이로 근사한다(숫자는 맞고 한글은 조금 작게 나온다).
				Cell.InkX0 = Cell.BoxX0;
				Cell.InkX1 = Cell.BoxX0 + Cell.Advance;
				Cell.InkY1 = Baseline;
				Cell.InkY0 = static_cast<float>(Baseline - FontSize * CapPerEm);
			}
			Cells.Add(Char[0], Cell);
		}
	}

	UE_LOG(LogTemp, Display, TEXT("[PlateSDF] 아틀라스 %dx%d 글리프=%d(잉크상자 %d) 셀=%.0f 폰트=%.0f (%s)"),
		AtlasW, AtlasH, Cells.Num(), InkFromMetrics, CellSize, FontSize, *MetricsPath);
}

bool UPlateGlyphAtlasSubsystem::IsReady()
{
	EnsureLoaded();
	return AtlasPixels.Num() > 0 && AtlasW > 0 && AtlasH > 0 && Cells.Num() > 0 && CellSize > 0.f;
}

UMaterialInterface* UPlateGlyphAtlasSubsystem::GetKindMaterial(const FString& Key)
{
	if (const TObjectPtr<UMaterialInterface>* Found = KindMaterials.Find(Key))
	{
		return Found->Get();
	}
	UMaterialInterface* Mat = nullptr;
	if (PlateKinds::FindKind(Key) != nullptr)
	{
		Mat = LoadObject<UMaterialInterface>(nullptr, *PlateKinds::KindMaterialPath(Key));
		if (Mat == nullptr)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PlateSDF] 종류 머티리얼 없음: %s — 이 종류는 옛 판(normal_film)으로 그린다"),
				*PlateKinds::KindMaterialPath(Key));
		}
	}
	KindMaterials.Add(Key, Mat);
	return Mat;
}

void UPlateGlyphAtlasSubsystem::BlitCell(TArray<uint8>& Mip0, int32 W, int32 H, const FPlateGlyphCell& Cell,
	double SrcX0, double SrcY0, double SrcX1, double SrcY1, double DstX0, double DstY0, double ScaleX, double ScaleY) const
{
	const double CellX = Cell.Col * CellSize;
	const double CellY = Cell.Row * CellSize;
	const int32 DstXBegin = FMath::Max(0, FMath::FloorToInt32(DstX0));
	const int32 DstXEnd = FMath::Min(W, FMath::CeilToInt32(DstX0 + (SrcX1 - SrcX0) * ScaleX));
	const int32 DstYBegin = FMath::Max(0, FMath::FloorToInt32(DstY0));
	const int32 DstYEnd = FMath::Min(H, FMath::CeilToInt32(DstY0 + (SrcY1 - SrcY0) * ScaleY));
	for (int32 Y = DstYBegin; Y < DstYEnd; ++Y)
	{
		const double Sy = CellY + SrcY0 + (Y + 0.5 - DstY0) / ScaleY - 0.5;
		// 셀 밖(이웃 글리프)을 읽지 않는다 — 큰 여백을 옮길 때 옆 셀의 글자가 묻어 들어온다.
		if (Sy < CellY || Sy > CellY + CellSize - 1.0) { continue; }
		for (int32 X = DstXBegin; X < DstXEnd; ++X)
		{
			const double Sx = CellX + SrcX0 + (X + 0.5 - DstX0) / ScaleX - 0.5;
			if (Sx < CellX || Sx > CellX + CellSize - 1.0) { continue; }
			const float V = SampleBilinear(AtlasPixels, AtlasW, AtlasH, Sx, Sy);
			uint8& Dst = Mip0[Y * W + X];
			// 글자끼리는 안 겹치지만, 반올림 경계에서 앞 글자를 지우지 않도록 최댓값을 남긴다.
			Dst = FMath::Max(Dst, static_cast<uint8>(FMath::RoundToInt(V)));
		}
	}
}

UTexture2D* UPlateGlyphAtlasSubsystem::MakeSdfTexture(UObject* Outer, TArray<uint8>&& Mip0, int32 W, int32 H, int32& OutMipCount)
{
	// ── 밉 체인 (박스 필터) ───────────────────────────────────────────────
	// 엔진의 `bAutoGenerateMips` 는 렌더타깃에서 밉을 채우지 못했다(헤더 머리말 참조).
	// 박스 필터로 내려도 SDF 는 잘 버틴다 — 오프라인 실측에서 32x8(밉5)까지 글자 안쪽 비율이
	// 0.23 으로 유지됐다. 그러니 내용만 채워 주면 된다.
	TArray<TArray<uint8>> Mips;
	Mips.Add(MoveTemp(Mip0));
	int32 MipW = W;
	int32 MipH = H;
	while (MipW > 1 || MipH > 1)
	{
		const int32 NextW = FMath::Max(1, MipW / 2);
		const int32 NextH = FMath::Max(1, MipH / 2);
		const TArray<uint8>& Src = Mips.Last();
		TArray<uint8> Next;
		Next.SetNumUninitialized(NextW * NextH);
		for (int32 Y = 0; Y < NextH; ++Y)
		{
			for (int32 X = 0; X < NextW; ++X)
			{
				const int32 X0 = FMath::Min(X * 2, MipW - 1);
				const int32 X1 = FMath::Min(X * 2 + 1, MipW - 1);
				const int32 Y0 = FMath::Min(Y * 2, MipH - 1);
				const int32 Y1 = FMath::Min(Y * 2 + 1, MipH - 1);
				const int32 Sum = Src[Y0 * MipW + X0] + Src[Y0 * MipW + X1]
					+ Src[Y1 * MipW + X0] + Src[Y1 * MipW + X1];
				Next[Y * NextW + X] = static_cast<uint8>((Sum + 2) / 4);
			}
		}
		Mips.Add(MoveTemp(Next));
		MipW = NextW;
		MipH = NextH;
	}

	// ── 텍스처 만들기 ─────────────────────────────────────────────────────
	UTexture2D* Tex = NewObject<UTexture2D>(Outer);
	FTexturePlatformData* Data = new FTexturePlatformData();
	Data->SizeX = W;
	Data->SizeY = H;
	Data->PixelFormat = PF_G8;
	MipW = W;
	MipH = H;
	for (const TArray<uint8>& Level : Mips)
	{
		FTexture2DMipMap* Mip = new FTexture2DMipMap();
		Mip->SizeX = MipW;
		Mip->SizeY = MipH;
		Mip->BulkData.Lock(LOCK_READ_WRITE);
		void* Dest = Mip->BulkData.Realloc(Level.Num());
		FMemory::Memcpy(Dest, Level.GetData(), Level.Num());
		Mip->BulkData.Unlock();
		Data->Mips.Add(Mip);
		MipW = FMath::Max(1, MipW / 2);
		MipH = FMath::Max(1, MipH / 2);
	}
	Tex->SetPlatformData(Data);
	Tex->SRGB = false;              // SDF 는 색이 아니라 거리값이다 — 감마가 끼면 임계 0.5 가 밀린다
	Tex->Filter = TF_Trilinear;
	Tex->AddressX = TA_Clamp;
	Tex->AddressY = TA_Clamp;
	Tex->NeverStream = true;        // 스트리밍이 물면 저해상 밉만 올라와 글자가 울렁거린다
	Tex->CompressionSettings = TC_Grayscale;
	Tex->UpdateResource();
	OutMipCount = Mips.Num();
	return Tex;
}

UTexture2D* UPlateGlyphAtlasSubsystem::BuildNumberSdf(UObject* Outer, const FString& DisplayText, double PlateAspect)
{
	if (!IsReady() || Outer == nullptr || DisplayText.IsEmpty() || PlateAspect <= 0.0)
	{
		return nullptr;
	}

	// 글자별 어드밴스를 먼저 모은다. 아틀라스에 없는 글자가 하나라도 있으면 통째로 포기한다 —
	// 반쪽짜리 번호를 그리는 것보다 폴백 위젯을 남기는 편이 낫다.
	struct FRun { const FPlateGlyphCell* Cell; float Advance; };
	TArray<FRun> Runs;
	double TotalAdvance = 0.0;
	for (const TCHAR Ch : DisplayText)
	{
		if (Ch == TEXT(' '))
		{
			Runs.Add({ nullptr, SpaceAdvance });
			TotalAdvance += SpaceAdvance;
			continue;
		}
		const FPlateGlyphCell* Cell = Cells.Find(Ch);
		if (Cell == nullptr)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PlateSDF] 아틀라스에 없는 글자 '%c' — 번호 합성을 건너뛴다(%s)"),
				Ch, *DisplayText);
			return nullptr;
		}
		Runs.Add({ Cell, Cell->Advance });
		TotalAdvance += Cell->Advance;
	}
	if (TotalAdvance <= 0.0)
	{
		return nullptr;
	}

	// 텍셀이 가로/세로로 다르다(판은 4.74:1 인데 텍스처는 4:1). 글자의 cm 종횡비를 지키려면
	// 그 비를 가로 배율에 곱해야 한다. Aspect = (텍스처 가로/세로비) / (판 가로/세로비).
	const double Aspect = (static_cast<double>(TexWidth) / TexHeight) / PlateAspect;

	const double AreaX0 = AreaLeftFrac * TexWidth;
	const double AreaX1 = (1.0 - AreaRightFrac) * TexWidth;
	const double AreaW = AreaX1 - AreaX0;

	// 폭을 영역에 맞추고 세로는 종횡비 + 압축비로 따라온다(compose_number.py 와 같은 순서).
	const double ScaleX = AreaW / TotalAdvance;
	const double ScaleY = ScaleX / (Aspect * CondenseX);

	// 글자 덩어리(캡 높이)의 중심을 판 한가운데에 놓는다.
	const double Cap = FontSize * CapPerEm;
	const double GlyphMidInCell = Baseline - Cap * 0.5;
	const double DstY0 = 0.5 * TexHeight - GlyphMidInCell * ScaleY;

	// ── 밉 0 을 CPU 에서 그린다 ────────────────────────────────────────────
	TArray<uint8> Mip0;
	Mip0.SetNumZeroed(TexWidth * TexHeight);   // 0 = 완전 바깥

	double Pen = AreaX0 + 0.5 * (AreaW - TotalAdvance * ScaleX);
	int32 Drawn = 0;
	for (const FRun& Run : Runs)
	{
		const double W = Run.Advance * ScaleX;
		if (Run.Cell != nullptr)
		{
			// 셀 전체가 아니라 **어드밴스 박스만** 옮긴다. 셀을 통째로 붙이면 폭이 넓어
			// 앞 글자의 오른쪽을 덮어써 지워 버린다.
			BlitCell(Mip0, TexWidth, TexHeight, *Run.Cell,
				Run.Cell->BoxX0, 0.0, Run.Cell->BoxX0 + Run.Advance, CellSize, Pen, DstY0, ScaleX, ScaleY);
			++Drawn;
		}
		Pen += W;
	}

	int32 MipCount = 0;
	UTexture2D* Tex = MakeSdfTexture(Outer, MoveTemp(Mip0), TexWidth, TexHeight, MipCount);

	UE_LOG(LogTemp, Display,
		TEXT("[PlateSDF] '%s' 합성: 글자 %d개, 배율 가로%.4f 세로%.4f, 캡 %.1ftexel(≈%.2fcm), 밉 %d단"),
		*DisplayText, Drawn, ScaleX, ScaleY, Cap * ScaleY,
		Cap * ScaleY * 11.0 / TexHeight, MipCount);

	return Tex;
}

UTexture2D* UPlateGlyphAtlasSubsystem::BuildKindSdf(UObject* Outer, const FPlateKindDef& Kind, const FString& Shown)
{
	if (!IsReady() || Outer == nullptr)
	{
		return nullptr;
	}
	const TArray<FPlateCell> Layout = PlateLayout::Cells(Kind, Shown);
	if (Layout.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PlateSDF] 조판 실패(번호 문법 아님): kind=%s shown=%s"), Kind.Key, *Shown);
		return nullptr;
	}
	for (const FPlateCell& C : Layout)
	{
		if (!Cells.Contains(C.Ch))
		{
			UE_LOG(LogTemp, Warning, TEXT("[PlateSDF] 아틀라스에 없는 글자 '%c' — 종류 합성을 건너뛴다(kind=%s %s)"),
				C.Ch, Kind.Key, *Shown);
			return nullptr;
		}
	}

	const int32 W = Kind.IsTwoRow() ? 512 : TexWidth;
	const int32 H = TexHeight;
	const double PxX = static_cast<double>(W) / Kind.WidthMm;    // 텍셀/mm (가로·세로가 다르다)
	const double PxY = static_cast<double>(H) / Kind.HeightMm;

	TArray<uint8> Mip0;
	Mip0.SetNumZeroed(W * H);
	for (const FPlateCell& C : Layout)
	{
		const FPlateGlyphCell& G = Cells[C.Ch];
		const double InkW = FMath::Max(1.0, static_cast<double>(G.InkX1 - G.InkX0));
		const double InkH = FMath::Max(1.0, static_cast<double>(G.InkY1 - G.InkY0));
		// 세로: 잉크 높이를 고시 mm 에 맞춘다. 가로: 비율 유지 폭을 칸의 [80 %(1 을 뺀 숫자만), 92 %] 로 누르거나 늘린다.
		const double ScaleY = (C.InkH * PxY) / InkH;
		const double NaturalWmm = InkW * (C.InkH / InkH);
		const double MaxWmm = C.Width() * 0.92;
		const double MinWmm = (C.bDigit && C.Ch != TEXT('1')) ? C.Width() * 0.80 : 0.0;
		const double TargetWmm = FMath::Clamp(NaturalWmm, MinWmm, MaxWmm);
		const double ScaleX = (TargetWmm * PxX) / InkW;
		// 칸 중앙에 잉크 상자를 놓는다(한글은 2 % 아래 — plategen 과 같다).
		const double CxMm = (C.X0 + C.X1) * 0.5;
		const double CyMm = (C.Y0 + C.Y1) * 0.5 + (C.bDigit ? 0.0 : 0.02 * C.Height());
		const double InkDstX0 = CxMm * PxX - TargetWmm * PxX * 0.5;
		const double InkDstY0 = CyMm * PxY - C.InkH * PxY * 0.5;
		// 거리장 여백(Spread)까지 함께 옮겨야 모따기·그림자가 잉크 경계에서 잘리지 않는다.
		const double M = Spread;
		BlitCell(Mip0, W, H, G,
			G.InkX0 - M, G.InkY0 - M, G.InkX1 + M, G.InkY1 + M,
			InkDstX0 - M * ScaleX, InkDstY0 - M * ScaleY, ScaleX, ScaleY);
	}

	int32 MipCount = 0;
	UTexture2D* Tex = MakeSdfTexture(Outer, MoveTemp(Mip0), W, H, MipCount);
	UE_LOG(LogTemp, Display, TEXT("[PlateSDF] kind=%s '%s' 합성: 칸 %d개, %dx%d, 밉 %d단"),
		Kind.Key, *Shown, Layout.Num(), W, H, MipCount);
	return Tex;
}
