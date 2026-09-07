// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParkingPresetManager.h"
#include "ParkingGeometryLibrary.h"
#include "PresetMakerWidget.h"
#include "CameraControlManager.h"
#include "PTZCameraActor.h"
#include "Config/Park3DAppConfig.h"
#include "DrawDebugHelpers.h"
#include "Components/SceneComponent.h"
#include "Components/DecalComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** 레벨 BP_ParkingSlot ISM 면 하나. 인스턴스 자체에는 번호가 없어 나열 순서가 곧 번호다. */
	struct FLevelSlot
	{
		FVector Center = FVector::ZeroVector;
		FVector AxisDir = FVector::ForwardVector; // 길이축(수평 단위 벡터)
		float WidthCm = 0.f;                      // 짧은 변
		float LengthCm = 0.f;                     // 긴 변(AxisDir 방향)
		FString ActorName;                        // 소유 BP_ParkingSlot 액터 이름
		int32 Instance = -1;                      // 그 액터 ISM 안의 인스턴스 번호
	};

	/** "BP_ParkingSlot_C_12" 의 끝 숫자. 없으면 0. 이름 문자열 정렬은 _10 이 _2 앞에 오므로 숫자로 비교한다. */
	int32 TrailingNumber(const FString& Name)
	{
		int32 End = Name.Len();
		while (End > 0 && FChar::IsDigit(Name[End - 1])) --End;
		return End < Name.Len() ? FCString::Atoi(*Name.Mid(End)) : 0;
	}

	/**
	 * 레벨의 BP_ParkingSlot ISM_Slot 인스턴스를 액터 이름 번호순 → 인스턴스 순으로 모은다.
	 * 선별 규약(클래스 이름 접두사 + 컴포넌트 이름 "Slot")은 ACarPlacementManager 의 FindLevelSlotAtWorld 와 같다.
	 */
	void CollectLevelSlots(UWorld* World, TArray<FLevelSlot>& Out)
	{
		TArray<AActor*> SlotActors;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetClass()->GetName().StartsWith(TEXT("BP_ParkingSlot")))
			{
				SlotActors.Add(*It);
			}
		}
		SlotActors.Sort([](const AActor& A, const AActor& B)
		{
			const int32 NA = TrailingNumber(A.GetName());
			const int32 NB = TrailingNumber(B.GetName());
			return NA != NB ? NA < NB : A.GetName() < B.GetName();
		});

		for (AActor* Actor : SlotActors)
		{
			TArray<UInstancedStaticMeshComponent*> Comps;
			Actor->GetComponents(Comps);
			for (UInstancedStaticMeshComponent* Comp : Comps)
			{
				if (!Comp || !Comp->GetStaticMesh() || !Comp->GetName().Contains(TEXT("Slot")))
				{
					continue; // ISM_Stopper·ISM_Space 제외
				}
				const FVector Extent = Comp->GetStaticMesh()->GetBounds().BoxExtent;
				for (int32 i = 0; i < Comp->GetInstanceCount(); ++i)
				{
					FTransform T;
					if (!Comp->GetInstanceTransform(i, T, /*bWorldSpace=*/true))
					{
						continue;
					}
					const FVector Scale = T.GetScale3D();
					const float HalfX = Extent.X * FMath::Abs(Scale.X);
					const float HalfY = Extent.Y * FMath::Abs(Scale.Y);
					FLevelSlot S;
					S.Center = T.GetLocation();
					// 긴 변이 주차 깊이(차량 길이축). 플레인 로컬 X 가 길면 X 축, 아니면 Y 축.
					S.AxisDir = (HalfX >= HalfY ? T.GetUnitAxis(EAxis::X) : T.GetUnitAxis(EAxis::Y)).GetSafeNormal2D();
					S.WidthCm = 2.f * FMath::Min(HalfX, HalfY);
					S.LengthCm = 2.f * FMath::Max(HalfX, HalfY);
					S.ActorName = Actor->GetName();
					S.Instance = i;
					Out.Add(S);
				}
			}
		}
	}
}

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
	RebuildSlotNumbers(StoredPresets);
}

AParkingPresetManager* AParkingPresetManager::GetOrSpawn(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AParkingPresetManager> It(World); It; ++It)
	{
		return *It;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<AParkingPresetManager>(AParkingPresetManager::StaticClass(), FTransform::Identity, Params);
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

// ─────────────────────────────────────────────────────────────
// 주차면 바닥 번호(3D 텍스트)
// ─────────────────────────────────────────────────────────────

UTextRenderComponent* AParkingPresetManager::AcquireNumber(int32 Index)
{
	if (NumberPool.IsValidIndex(Index) && NumberPool[Index])
	{
		return NumberPool[Index];
	}

	// 엔진 기본 폰트(RobotoDistanceField)는 Offline 캐시라 숫자가 그려진다 — 프로젝트 한글 폰트(Runtime)를
	// 걸면 아무것도 안 나온다(팀 보드 #63, CarPlateNumberWidget.h). 여기는 숫자만 쓰므로 기본값을 그대로 둔다.
	UTextRenderComponent* T = NewObject<UTextRenderComponent>(this);
	T->SetupAttachment(RootComponent);
	T->SetHorizontalAlignment(EHTA_Center);
	T->SetVerticalAlignment(EVRTA_TextCenter);
	T->SetCastShadow(false);
	T->RegisterComponent();
	NumberPool.Add(T);
	return T;
}

void AParkingPresetManager::PlaceNumber(UTextRenderComponent* T, const FVector& Center, const FVector& AxisDir, float SlotWidthCm, int32 Number, const FVector& RowDir)
{
	if (!T) return;

	// 글자 축 = 면의 두 축(길이·폭) 중 **열 방향(이웃 면 쪽)에 수직인 축**. 수직주차는 열 방향이 폭 축이라
	// 길이축이 되고, 객리단길 같은 평행주차는 열 방향이 길이축이라 폭 축이 된다 — 어느 쪽이든 글자가
	// 면 안에 세로로 눕고 번호가 열을 따라 나란히 읽힌다.
	// (버린 규칙 둘: ① 길이축 고정 → 평행주차에서 옆으로 누움 ② "가장 가까운 카메라와 나란한 축" →
	//  객리단 카메라가 폴대 끝(도로 쪽 2~4m)에 있어 카메라 벡터가 열 방향 성분이 더 커 ①과 같아진다. 둘 다 캡처로 확인.)
	FVector TextAxis = AxisDir.GetSafeNormal2D();
	if (TextAxis.IsNearlyZero()) TextAxis = FVector::ForwardVector;
	if (!RowDir.IsNearlyZero())
	{
		const FVector Perp(-TextAxis.Y, TextAxis.X, 0.f); // 폭 축
		if (FMath::Abs(FVector::DotProduct(Perp, RowDir)) < FMath::Abs(FVector::DotProduct(TextAxis, RowDir)))
		{
			TextAxis = Perp;
		}
	}
	// 부호: 축이 **가장 가까운 카메라를 향하게** 둔다. 반대로 두면 카메라 화면에서 숫자가 거꾸로 나온다 —
	// `MakeFromXZ` 의 로컬 +Z 는 글자 위쪽이 아니라 아래쪽이기 때문이다(엔진 정점 Z = −Top).
	// 감시 카메라 화면이 이 앱의 판정 화면이므로 그쪽에서 바로 읽히는 것을 기준으로 삼는다. 캡처로 확정.
	for (TActorIterator<ACameraControlManager> It(GetWorld()); It; ++It)
	{
		float BestSq = TNumericLimits<float>::Max();
		FVector ToCam = FVector::ZeroVector;
		for (int32 i = 0; i < It->GetCameraCount(); ++i)
		{
			const APTZCameraActor* C = It->GetCamera(i);
			if (!C) continue;
			const FVector D = C->GetActorLocation() - Center;
			const float Sq = static_cast<float>(D.SizeSquared2D());
			if (Sq < BestSq) { BestSq = Sq; ToCam = D; }
		}
		if (FVector::DotProduct(TextAxis, ToCam) < 0.f)
		{
			TextAxis = -TextAxis;
		}
		break;
	}

	// TextRender 메시는 로컬 YZ 평면(법선 +X) — 법선을 월드 위로 눕히고 로컬 +Z 를 TextAxis 에 맞춘다.
	const FRotator Rot = FRotationMatrix::MakeFromXZ(FVector::UpVector, TextAxis).Rotator();
	// 높이는 **면 자신의 Z 기준 상대**여야 한다 — 절대 6cm 로 박았더니 LV_Park_01(면 판 z=10cm)에서
	// 글자가 판 밑에 깔려 위에서 안 보였다(LV_Park_03 은 z=1.1cm 라 우연히 보였다). 캡처로 확인.
	T->SetWorldLocationAndRotation(FVector(Center.X, Center.Y, Center.Z + SlotNumberZ), Rot);
	// 두 자리 숫자 폭 ≈ 높이 × 1.1 — 좁은 면에서 라인을 넘지 않도록 면 폭의 45% 로 상한.
	const float Size = SlotWidthCm > 0.f ? FMath::Min(SlotNumberSizeCm, SlotWidthCm * 0.45f) : SlotNumberSizeCm;
	T->SetWorldSize(Size);
	T->SetTextRenderColor(SlotNumberColor);
	T->SetText(FText::AsNumber(Number));
	T->SetVisibility(true);
}

void AParkingPresetManager::CollectSlotNumbers(const TArray<FParkingPreset>& Presets, TArray<FParkingSlotNumberInfo>& Out) const
{
	Out.Reset();

	// ① 프리셋 면 — 번호는 리스트의 [시작~끝] 과 같은 할당(카메라 → 프리셋 순 연속 부여).
	const TArray<FParkingSpaceAssignment> Assigns = UParkingGeometryLibrary::CalculateParkingSpaceAssignments(Presets);
	for (const FParkingPreset& P : Presets)
	{
		const FParkingSpaceAssignment* A = Assigns.FindByPredicate([&P](const FParkingSpaceAssignment& X) { return X.PresetIdx == P.PresetIdx; });
		const int32 Start = A ? A->StartFaceNum : 1;
		for (int32 j = 0; j < P.FaceCount; ++j)
		{
			FVector C[4];
			ComputeSlotCorners(P, j, MetersToUU, FaceHeightZ, C);
			// [0]→[1] 이 zSize(길이) 변, [0]→[3] 이 xSize(폭) 변(FindSlotAtWorld 와 같은 규약).
			const FVector EdgeZ = C[1] - C[0];
			const FVector EdgeX = C[3] - C[0];
			const bool bZLong = EdgeZ.SizeSquared2D() >= EdgeX.SizeSquared2D();

			FParkingSlotNumberInfo Info;
			Info.Number = Start + j;
			Info.Center = (C[0] + C[1] + C[2] + C[3]) * 0.25f;
			Info.AxisDir = (bZLong ? EdgeZ : EdgeX).GetSafeNormal2D();
			Info.WidthCm = static_cast<float>((bZLong ? EdgeX : EdgeZ).Size2D());
			Info.LengthCm = static_cast<float>((bZLong ? EdgeZ : EdgeX).Size2D());
			Info.bFromPreset = true;
			Info.PresetIdx = P.PresetIdx;
			Info.SlotId = j + 1;
			Out.Add(Info);
		}
	}

	// ② 레벨 면 — 항상 있는 쪽. 프리셋과 별개로 1부터.
	TArray<FLevelSlot> LevelSlots;
	if (UWorld* World = GetWorld())
	{
		CollectLevelSlots(World, LevelSlots);
	}
	for (int32 i = 0; i < LevelSlots.Num(); ++i)
	{
		FParkingSlotNumberInfo Info;
		Info.Number = i + 1;
		Info.Center = LevelSlots[i].Center;
		Info.AxisDir = LevelSlots[i].AxisDir;
		Info.WidthCm = LevelSlots[i].WidthCm;
		Info.LengthCm = LevelSlots[i].LengthCm;
		Info.LevelActor = LevelSlots[i].ActorName;
		Info.LevelInstance = LevelSlots[i].Instance;
		Out.Add(Info);
	}

	ApplyNumberAnchors(Out);
}

void AParkingPresetManager::ApplyNumberAnchors(TArray<FParkingSlotNumberInfo>& Slots) const
{
	// 순번은 항상 남긴다 — 기준점을 걸지 않아도 사람이 면을 부를 이름이고, 패널 안내 문구가 이것을 보여 준다.
	for (FParkingSlotNumberInfo& S : Slots)
	{
		S.BaseNumber = S.Number;
	}
	if (NumberAnchors.Num() == 0)
	{
		return;
	}
	TMap<FString, FSlotNumberAnchor> ByKey;
	for (const FSlotNumberAnchor& A : NumberAnchors)
	{
		ByKey.Add(A.FaceKey, A); // 같은 키는 뒤에 온 것이 덮는다.
	}

	bool bPrevFromPreset = true;
	int32 Next = 0;
	int32 Remaining = 0; // 남은 개수이자 "적용 중" 표시(0 이면 원래 순번을 그대로 둔다).
	for (FParkingSlotNumberInfo& S : Slots)
	{
		if (S.bFromPreset != bPrevFromPreset)
		{
			Remaining = 0; // 프리셋 면 → 레벨 면 경계: 이어 매기기를 끊는다(두 묶음은 독립).
			bPrevFromPreset = S.bFromPreset;
		}
		if (const FSlotNumberAnchor* A = ByKey.Find(S.FaceKey()))
		{
			Next = A->Number;
			// 개수를 안 주면(0) 묶음 끝까지 — 09-08 최초 규약. 주면 그 개수만 바꾸고 뒤는 원래 순번.
			Remaining = A->Count > 0 ? A->Count : TNumericLimits<int32>::Max();
		}
		if (Remaining > 0)
		{
			S.Number = Next++;
			--Remaining;
		}
	}
}

void AParkingPresetManager::SetNumberAnchors(const TArray<FSlotNumberAnchor>& InAnchors)
{
	NumberAnchors.Reset();
	for (const FSlotNumberAnchor& A : InAnchors)
	{
		if (!A.FaceKey.IsEmpty() && A.Number > 0)
		{
			NumberAnchors.Add(A);
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[ParkingManager] 번호 기준점 %d개 적용"), NumberAnchors.Num());
	RebuildSlotNumbers(ResolvePresets());
}

bool AParkingPresetManager::FindSlotNumberAtWorld(const FVector& WorldLoc, FParkingSlotNumberInfo& OutInfo)
{
	TArray<FParkingSlotNumberInfo> Slots;
	CollectSlotNumbers(ResolvePresets(), Slots);

	for (const FParkingSlotNumberInfo& S : Slots)
	{
		if (S.LengthCm <= 0.f || S.WidthCm <= 0.f)
		{
			continue;
		}
		// 면 축으로 옮겨 반쪽 크기와 비교(OBB). Z 는 무시한다 — 클릭은 바닥을 맞히고 면은 두께가 0 이다.
		const FVector D = WorldLoc - S.Center;
		const FVector Perp(-S.AxisDir.Y, S.AxisDir.X, 0.f);
		const float Along  = static_cast<float>(D.X * S.AxisDir.X + D.Y * S.AxisDir.Y);
		const float Across = static_cast<float>(D.X * Perp.X + D.Y * Perp.Y);
		if (FMath::Abs(Along) <= S.LengthCm * 0.5f && FMath::Abs(Across) <= S.WidthCm * 0.5f)
		{
			OutInfo = S;
			return true;
		}
	}
	return false;
}

void AParkingPresetManager::RebuildSlotNumbers(const TArray<FParkingPreset>& Presets)
{
	if (!bShowSlotNumbers)
	{
		ClearSlotNumbers();
		return;
	}

	TArray<FParkingSlotNumberInfo> Slots;
	CollectSlotNumbers(Presets, Slots);

	int32 PresetCount = 0;
	for (const FParkingSlotNumberInfo& S : Slots)
	{
		if (S.bFromPreset) ++PresetCount;
	}

	// 열 방향 = 가장 가까운 다른 면 중심 쪽(면이 하나뿐이면 없음 → 길이축).
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		FVector RowDir = FVector::ZeroVector;
		float BestSq = TNumericLimits<float>::Max();
		for (int32 k = 0; k < Slots.Num(); ++k)
		{
			if (k == i) continue;
			const FVector D = Slots[k].Center - Slots[i].Center;
			const float Sq = static_cast<float>(D.SizeSquared2D());
			if (Sq > 1.f && Sq < BestSq) { BestSq = Sq; RowDir = D.GetSafeNormal2D(); }
		}
		PlaceNumber(AcquireNumber(i), Slots[i].Center, Slots[i].AxisDir, Slots[i].WidthCm, Slots[i].Number, RowDir);
	}

	for (int32 idx = Slots.Num(); idx < NumberPool.Num(); ++idx)
	{
		if (NumberPool[idx]) NumberPool[idx]->SetVisibility(false);
	}

	UE_LOG(LogTemp, Log, TEXT("[ParkingManager] 주차면 번호 %d개 표시(프리셋 %d, 레벨 %d, 풀 %d)"),
		Slots.Num(), PresetCount, Slots.Num() - PresetCount, NumberPool.Num());
}

void AParkingPresetManager::ClearSlotNumbers()
{
	for (TObjectPtr<UTextRenderComponent>& T : NumberPool)
	{
		if (T) T->SetVisibility(false);
	}
}
