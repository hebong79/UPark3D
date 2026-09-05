// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParkingPresetManager.h"
#include "ParkingGeometryLibrary.h"
#include "PresetMakerWidget.h"
#include "Config/Park3DAppConfig.h"
#include "DrawDebugHelpers.h"
#include "Components/SceneComponent.h"
#include "Components/DecalComponent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

AParkingPresetManager::AParkingPresetManager()
{
	PrimaryActorTick.bCanEverTick = false;

	// 데칼 부착용 루트 씬 컴포넌트(기존엔 RootComponent 없음). Identity 스폰이라 기존 동작 무영향(설계 R7).
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// 데칼 머티리얼 기본값을 확인된 경로에서 하드로드 시도. 실패해도 크래시 금지(Succeeded 체크).
	// 실패 시 null 유지 → 런타임에 해당 데칼 스킵 + 경고. 에디터에서 재지정 가능(EditAnywhere).
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> LineFinder(
		TEXT("/Game/M/Decal/MI_ParkingLine.MI_ParkingLine"));
	if (LineFinder.Succeeded())
	{
		LineDecalMaterial = LineFinder.Object;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[ParkingManager] 라인 데칼 머티리얼 로드 실패(MI_ParkingLine) — 에디터에서 지정 필요."));
	}

	// fill 데칼 기본값: 라인과 같은 마스터(M_ParkingDecal, Deferred Decal · Translucent)의 인스턴스로
	// Color=(0,0.6,1)/Opacity=0.3. 라인과 마스터를 공유해야 두 데칼의 렌더 규칙이 갈리지 않는다.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FillFinder(
		TEXT("/Game/M/Decal/MI_ParkingSelectFill.MI_ParkingSelectFill"));
	if (FillFinder.Succeeded())
	{
		SelectFillDecalMaterial = FillFinder.Object;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[ParkingManager] fill 데칼 머티리얼 로드 실패(MI_ParkingSelectFill) — 에디터에서 지정 필요."));
	}
}

FVector AParkingPresetManager::RotateZAround(const FVector& P, const FVector& Pivot, float AngleDeg)
{
	const float Rad = FMath::DegreesToRadians(AngleDeg);
	const float Cs = FMath::Cos(Rad);
	const float Sn = FMath::Sin(Rad);
	const FVector D = P - Pivot;
	return Pivot + FVector(D.X * Cs - D.Y * Sn, D.X * Sn + D.Y * Cs, D.Z);
}

void AParkingPresetManager::ComputeSlotCorners(
	const FParkingPreset& P, int32 FaceIndex,
	float MetersToUU, float FaceHeightZ,
	FVector(&OutBottom)[4])
{
	// DrawPreset 의 면별 Bottom[4] 계산을 100% 동일하게 옮긴 순수 함수(디버그·데칼 공유).
	const float U = MetersToUU;
	const FVector Origin = P.Offset * U;
	const float FaceRot = P.FaceRotate;
	const float GroupRot = P.GroupFaceRotate;
	const float UnityXs = P.BoxSizeX * U;
	const float UnityZs = P.BoxSizeZ * U;

	// 사선 스텝 간격 보정(Default 타입에서 면이 겹치지 않도록 폭/cos)
	float Width = UnityXs;
	float Height = UnityZs;
	const float CosF = FMath::Abs(FMath::Cos(FMath::DegreesToRadians(FaceRot)));
	if (P.DirType == EFaceDirType::Default && CosF > 0.001f)
	{
		Width = UnityXs / CosF;
		Height = UnityZs / CosF;
	}
	const float Step = P.bIsBaseWidth ? Width : Height;
	const float NormalizedFaceRot = FMath::Fmod(FaceRot + 360.f, 360.f);
	const bool bReverseStep = NormalizedFaceRot > 180.f; // Unity CPMakerParkSpaceUI target.eulerAngles.y 규약.

	// FaceIndex번째 면의 누적 위치(시작점 기준)
	FVector Pos = Origin;
	const float Disp = Step * FaceIndex;
	if (P.DirType == EFaceDirType::Default)
	{
		// Unity baseWidth ±X -> UE ±Y, baseLength ±Z -> UE ±X.
		const float SignedDisp = bReverseStep ? -Disp : Disp;
		if (P.bIsBaseWidth) Pos.Y += SignedDisp;
		else                Pos.X += SignedDisp;
	}
	else // Dir : 면 로컬 방향으로 진행
	{
		const float Rad = FMath::DegreesToRadians(FaceRot);
		// Unity right=(cos, -sin in X/Z), forward=(sin, cos in X/Z).
		// Unity (x,z)->UE(X=z,Y=x): right->UE local +Y, forward->UE local +X.
		const FVector RightDir(-FMath::Sin(Rad), FMath::Cos(Rad), 0.f);
		const FVector ForwardDir(FMath::Cos(Rad), FMath::Sin(Rad), 0.f);
		Pos += (bReverseStep ? -1.f : 1.f) * (P.bIsBaseWidth ? RightDir : ForwardDir) * Disp;
	}

	// Unity CLineRect local (x,z) -> UE local (X=z,Y=x), then same-sign yaw rotation.
	const FVector Local[4] = {
		FVector(-UnityZs * 0.5f, -UnityXs * 0.5f, FaceHeightZ),
		FVector( UnityZs * 0.5f, -UnityXs * 0.5f, FaceHeightZ),
		FVector( UnityZs * 0.5f,  UnityXs * 0.5f, FaceHeightZ),
		FVector(-UnityZs * 0.5f,  UnityXs * 0.5f, FaceHeightZ),
	};

	for (int32 k = 0; k < 4; ++k)
	{
		FVector R = RotateZAround(Local[k], FVector::ZeroVector, FaceRot); // 면 회전
		R += Pos;                                                          // 위치 이동
		R = RotateZAround(R, Origin, GroupRot);                            // 그룹 회전
		OutBottom[k] = R;
	}
}

const FParkingPreset* AParkingPresetManager::FindSlotAtWorld(
	const TArray<FParkingPreset>& Presets, const FVector& WorldLoc, float MetersToUU,
	int32& OutSlotId, FVector& OutCenterWorld, float& OutAxisYaw)
{
	for (const FParkingPreset& P : Presets)
	{
		for (int32 j = 0; j < P.FaceCount; ++j)
		{
			FVector C[4];
			// 차량은 바닥에 놓이므로 FaceHeightZ=0 으로 부른다(면 라인의 5cm 띄움은 렌더 전용).
			ComputeSlotCorners(P, j, MetersToUU, /*FaceHeightZ=*/0.f, C);

			// 사각형은 볼록하다(ComputeSlotCorners 규약) → 네 변의 외적 부호가 모두 같으면 내부.
			bool bNeg = false, bPos = false;
			for (int32 k = 0; k < 4; ++k)
			{
				const FVector2D Edge(C[(k + 1) % 4].X - C[k].X, C[(k + 1) % 4].Y - C[k].Y);
				const FVector2D ToPoint(WorldLoc.X - C[k].X, WorldLoc.Y - C[k].Y);
				const float Cross = Edge.X * ToPoint.Y - Edge.Y * ToPoint.X;
				if (Cross < 0.f) bNeg = true;
				if (Cross > 0.f) bPos = true;
			}
			if (bNeg && bPos)
			{
				continue; // 밖(변 위의 0 은 어느 쪽도 세우지 않으므로 안으로 친다)
			}

			OutSlotId = j + 1;
			OutCenterWorld = (C[0] + C[1] + C[2] + C[3]) * 0.25f;

			// 긴 변 = 주차 깊이(차량 길이축). Local[0]→[1] 이 zSize 변, [0]→[3] 이 xSize 변이다.
			const FVector EdgeZ = C[1] - C[0];
			const FVector EdgeX = C[3] - C[0];
			const FVector Depth = (EdgeZ.SizeSquared2D() >= EdgeX.SizeSquared2D()) ? EdgeZ : EdgeX;
			// Unity yaw = atan2(x, z). Unity x→UE Y, Unity z→UE X 이므로 atan2(UE.Y, UE.X).
			OutAxisYaw = (Depth.SizeSquared2D() < 1e-6f)
				? 0.f
				: FMath::RadiansToDegrees(FMath::Atan2(Depth.Y, Depth.X));
			return &P;
		}
	}
	return nullptr;
}

const TArray<FParkingPreset>& AParkingPresetManager::ResolvePresets()
{
	if (StoredPresets.Num() > 0)
	{
		return StoredPresets;
	}

	FPark3DAppConfig Config;
	const FString Path = UPark3DAppConfigLibrary::Load(Config) && !Config.PresetFile.IsEmpty()
		? UPark3DAppConfigLibrary::ResolveDataPath(TEXT("Preset"), Config.PresetFile)
		: FString();

	if (Path.IsEmpty())
	{
		ConfigPresets.Reset();
		ConfigPresetPath.Reset();
		return ConfigPresets;
	}
	if (Path != ConfigPresetPath || ConfigPresets.Num() == 0)
	{
		ConfigPresetPath = Path;
		ConfigPresets.Reset();
		if (!UPresetMakerWidget::LoadPresetsFromJson(Path, ConfigPresets))
		{
			UE_LOG(LogTemp, Warning, TEXT("[ParkingManager] 프리셋 파일을 읽지 못했습니다: %s"), *Path);
		}
	}
	return ConfigPresets;
}

void AParkingPresetManager::DrawClosedRect(const FVector(&Corners)[4], const FColor& Color)
{
	UWorld* World = GetWorld();
	if (!World) return;
	for (int32 k = 0; k < 4; ++k)
	{
		DrawDebugLine(World, Corners[k], Corners[(k + 1) % 4], Color, /*bPersistent*/ true, /*LifeTime*/ -1.f, /*Depth*/ 0, LineThickness);
	}
}

void AParkingPresetManager::DrawFilledQuad(const FVector(&Corners)[4], const FColor& FillColor)
{
	UWorld* World = GetWorld();
	if (!World) return;

	// 라인(FaceHeightZ)보다 살짝 아래로 내려 Z-fighting 회피. RotateZAround가 Z를 보존하므로 균일하게 내려감.
	const FVector Offset(0.f, 0.f, SelectFillZBias);
	TArray<FVector> Verts = {
		Corners[0] + Offset, Corners[1] + Offset, Corners[2] + Offset, Corners[3] + Offset
	};
	// Bottom 정점 순서 (-,-),(-,+),(+,+),(+,-) 에 대응. 디버그 메시는 양면이라 상/하 어디서 봐도 보임.
	const TArray<int32> Indices = { 0, 1, 2, 0, 2, 3 };
	DrawDebugMesh(World, Verts, Indices, FillColor, /*bPersistent*/ true, /*LifeTime*/ -1.f, /*Depth*/ 0);
}

void AParkingPresetManager::DrawPreset(const FParkingPreset& P, bool bSelected, bool bShow3D)
{
	UWorld* World = GetWorld();
	if (!World || P.FaceCount <= 0) return;

	// 면 누적 배치 방향은 회전 부호와 무관하게 일정해야 한다(회전은 제자리에서 일어남).
	// (이전: 0~360 정규화 후 >180 이면 방향 반전 → 음수 각도가 330° 등으로 정규화되어
	//  잘못 반전되면서 그룹 전체가 반대편으로 평행이동하는 버그가 있었음)

	const FColor Color = bSelected ? SelectColor : LineColor;

	for (int32 j = 0; j < P.FaceCount; ++j)
	{
		// 면별 바닥 사각형 4점(월드 cm) 계산 — 데칼 경로와 동일 순수 함수 공유.
		FVector Bottom[4];
		ComputeSlotCorners(P, j, MetersToUU, FaceHeightZ, Bottom);

		// R2: 선택 프리셋만 바닥 사각형을 반투명 면으로 채운다(라인 아래에 깔림). 3D여도 fill은 바닥에만.
		if (bSelected)
		{
			DrawFilledQuad(Bottom, SelectFillColor);
		}

		DrawClosedRect(Bottom, Color);

		// 3D 큐브: 바닥 4점을 위로 압출 + 수직 모서리
		if (bShow3D)
		{
			FVector Top[4];
			for (int32 k = 0; k < 4; ++k)
			{
				Top[k] = Bottom[k] + FVector(0.f, 0.f, QubeHeight);
			}
			DrawClosedRect(Top, Color);
			for (int32 k = 0; k < 4; ++k)
			{
				DrawDebugLine(World, Bottom[k], Top[k], Color, true, -1.f, 0, LineThickness);
			}
		}
	}
}

void AParkingPresetManager::RebuildAll(const TArray<FParkingPreset>& Presets, int32 SelectedIndex, bool bShow3D)
{
	UWorld* World = GetWorld();
	if (!World) return;

	FlushPersistentDebugLines(World);

	for (int32 i = 0; i < Presets.Num(); ++i)
	{
		// 프리셋 단위 큐브 숨김(setBoxVisible)은 전역 3D 토글보다 우선한다. 바닥 사각형은 계속 그린다.
		const bool bBox = bShow3D && !HiddenBoxPresetIdxs.Contains(Presets[i].PresetIdx);
		DrawPreset(Presets[i], i == SelectedIndex, bBox);
	}

	UE_LOG(LogTemp, Log, TEXT("[ParkingManager] %d개 프리셋 라인 생성(3D=%s)"),
		Presets.Num(), bShow3D ? TEXT("on") : TEXT("off"));
}

void AParkingPresetManager::ClearAll()
{
	if (UWorld* World = GetWorld())
	{
		FlushPersistentDebugLines(World);
	}
}

// ─────────────────────────────────────────────────────────────
// 데이터 권위(RPC preset.* 백엔드)
// ─────────────────────────────────────────────────────────────

FParkingPreset* AParkingPresetManager::FindPresetByIdx(int32 PresetIdx)
{
	for (FParkingPreset& P : StoredPresets)
	{
		if (P.PresetIdx == PresetIdx)
		{
			return &P;
		}
	}
	return nullptr;
}

int32 AParkingPresetManager::NextPresetIdx() const
{
	int32 MaxIdx = 0;
	for (const FParkingPreset& P : StoredPresets)
	{
		MaxIdx = FMath::Max(MaxIdx, P.PresetIdx);
	}
	return MaxIdx + 1;
}

int32 AParkingPresetManager::AddPreset(const FParkingPreset& InPreset)
{
	FParkingPreset P = InPreset;
	if (P.PresetIdx <= 0)
	{
		P.PresetIdx = NextPresetIdx();
	}
	StoredPresets.Add(P);
	return P.PresetIdx;
}

bool AParkingPresetManager::RemovePresetByIdx(int32 PresetIdx)
{
	const int32 Removed = StoredPresets.RemoveAll([PresetIdx](const FParkingPreset& P) { return P.PresetIdx == PresetIdx; });
	if (Removed > 0)
	{
		if (!StoredPresets.IsValidIndex(SelectedPresetIndex))
		{
			SelectedPresetIndex = INDEX_NONE;
		}
		return true;
	}
	return false;
}

void AParkingPresetManager::ClearPresets()
{
	StoredPresets.Reset();
	SelectedPresetIndex = INDEX_NONE;
	RefreshView();
}

void AParkingPresetManager::SetSelectedByIdx(int32 PresetIdx)
{
	SelectedPresetIndex = INDEX_NONE;
	if (PresetIdx >= 0)
	{
		for (int32 i = 0; i < StoredPresets.Num(); ++i)
		{
			if (StoredPresets[i].PresetIdx == PresetIdx) { SelectedPresetIndex = i; break; }
		}
	}
}

bool AParkingPresetManager::SetBoxVisible(int32 PresetIdx, bool bVisible)
{
	if (!FindPresetByIdx(PresetIdx))
	{
		return false;
	}
	if (bVisible)
	{
		HiddenBoxPresetIdxs.Remove(PresetIdx);
	}
	else
	{
		HiddenBoxPresetIdxs.Add(PresetIdx);
	}
	return true;
}

void AParkingPresetManager::RefreshView()
{
	// 데칼과 디버그 라인은 상호 배타(위젯 RefreshView 와 동일 규칙) — 같은 기하를 쓰므로 겹치면 이중 라인이 된다.
	if (bUseDecalView)
	{
		ClearAll(); // 디버그 라인/반투명 메시 제거
		RebuildDecals(StoredPresets, SelectedPresetIndex, DecalLineThicknessCm, true);
	}
	else
	{
		RebuildAll(StoredPresets, SelectedPresetIndex, bShow3DView);
		RebuildDecals(StoredPresets, SelectedPresetIndex, DecalLineThicknessCm, false); // 데칼 전부 숨김
	}
}

// ─────────────────────────────────────────────────────────────
// 데칼 기반 실사 주차면(배포판 렌더) — 디버그 라인 경로와 완전 분리
// ─────────────────────────────────────────────────────────────

UDecalComponent* AParkingPresetManager::AcquireDecal(int32 Index)
{
	// cursor 순차 호출 전제(0,1,2,...): 기존 인덱스는 재사용, 그 외엔 새로 만들어 풀 끝에 추가.
	if (DecalPool.IsValidIndex(Index) && DecalPool[Index])
	{
		return DecalPool[Index];
	}

	UDecalComponent* D = NewObject<UDecalComponent>(this);
	D->SetupAttachment(RootComponent);
	D->RegisterComponent();
	DecalPool.Add(D);
	return D;
}

void AParkingPresetManager::PlaceLineDecal(UDecalComponent* D, const FVector& A, const FVector& B, float ThicknessCm)
{
	if (!D || !LineDecalMaterial) return;

	const FVector Mid = (A + B) * 0.5f;
	const FVector Dir = (B - A).GetSafeNormal2D();  // 변 방향(수평)
	const float Len = (B - A).Size2D();             // 변 길이 L(cm)
	const float T = ThicknessCm;                    // 스트립 폭(cm)

	// UDecalComponent 는 로컬 -X 로 투영 → 바닥(−Z)으로 투영하려면 로컬 +X = 월드 +Z(up).
	// 로컬 X=up(투영 -X=down), 로컬 Y=변 방향, 로컬 Z=X×Y(수평 두께축).
	const FRotator Rot = FRotationMatrix::MakeFromXY(FVector::UpVector, Dir).Rotator();

	// half-extent: X=투영 깊이, Y=변 길이(+코너 보정 T), Z=스트립 폭(두께).
	const FVector Size(DecalProjectionDepth * 0.5f, (Len + T) * 0.5f, T * 0.5f);

	D->SetWorldLocationAndRotation(FVector(Mid.X, Mid.Y, DecalCenterZ), Rot);
	D->DecalSize = Size;
	D->SetDecalMaterial(LineDecalMaterial);
	D->SetSortOrder(LineSortOrder);
	D->SetVisibility(true);
}

void AParkingPresetManager::PlaceFillDecal(UDecalComponent* D, const FVector(&Bottom)[4])
{
	if (!D || !SelectFillDecalMaterial) return;

	FVector Center = (Bottom[0] + Bottom[1] + Bottom[2] + Bottom[3]) * 0.25f;
	const FVector EdgeX = Bottom[3] - Bottom[0]; // (-,-)→(+,-) : 폭 방향
	const FVector EdgeY = Bottom[1] - Bottom[0]; // (-,-)→(-,+) : 길이 방향
	const FVector Dir = EdgeX.GetSafeNormal2D();

	const FRotator Rot = FRotationMatrix::MakeFromXY(FVector::UpVector, Dir).Rotator();
	const FVector Size(DecalProjectionDepth * 0.5f, EdgeX.Size2D() * 0.5f, EdgeY.Size2D() * 0.5f);

	Center.Z = DecalCenterZ;
	D->SetWorldLocationAndRotation(Center, Rot);
	D->DecalSize = Size;
	D->SetDecalMaterial(SelectFillDecalMaterial);
	D->SetSortOrder(FillSortOrder); // 라인보다 아래
	D->SetVisibility(true);
}

void AParkingPresetManager::RebuildDecals(const TArray<FParkingPreset>& Presets, int32 SelectedIndex, float ThicknessCm, bool bEnable)
{
	// 토글 Off → 데칼 전부 숨김(풀은 재사용 위해 유지).
	if (!bEnable)
	{
		ClearDecals();
		return;
	}

	// 라인 머티리얼이 없으면 그릴 수 없음 → 경고 후 기존 데칼 숨김.
	if (!LineDecalMaterial)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ParkingManager] LineDecalMaterial 이 null → 데칼을 그릴 수 없습니다. 에디터에서 지정하세요."));
		ClearDecals();
		return;
	}

	// 선택은 있으나 fill 머티리얼이 없으면 1회 경고(fill 만 생략, 라인은 계속 그림).
	if (Presets.IsValidIndex(SelectedIndex) && !SelectFillDecalMaterial)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ParkingManager] SelectFillDecalMaterial 이 null → 선택 fill 데칼 생략."));
	}

	int32 Cursor = 0;
	for (int32 i = 0; i < Presets.Num(); ++i)
	{
		const FParkingPreset& Pi = Presets[i];
		for (int32 j = 0; j < Pi.FaceCount; ++j)
		{
			FVector Bottom[4];
			ComputeSlotCorners(Pi, j, MetersToUU, FaceHeightZ, Bottom);

			// 선택 프리셋이면 fill 먼저 배치(라인보다 아래 정렬).
			if (i == SelectedIndex && SelectFillDecalMaterial)
			{
				PlaceFillDecal(AcquireDecal(Cursor++), Bottom);
			}

			// 슬롯 4변 라인 데칼.
			for (int32 k = 0; k < 4; ++k)
			{
				PlaceLineDecal(AcquireDecal(Cursor++), Bottom[k], Bottom[(k + 1) % 4], ThicknessCm);
			}
		}
	}

	// 잉여 풀은 파괴 대신 숨김(재사용 대비, 누수·재생성 최소화).
	for (int32 idx = Cursor; idx < DecalPool.Num(); ++idx)
	{
		if (DecalPool[idx]) DecalPool[idx]->SetVisibility(false);
	}

	UE_LOG(LogTemp, Log, TEXT("[ParkingManager] 데칼 %d개 활성(풀 %d), 두께=%.0fcm"),
		Cursor, DecalPool.Num(), ThicknessCm);
}

void AParkingPresetManager::ClearDecals()
{
	for (TObjectPtr<UDecalComponent>& D : DecalPool)
	{
		if (D) D->SetVisibility(false);
	}
}
