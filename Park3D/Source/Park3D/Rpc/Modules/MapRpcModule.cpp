// Copyright Epic Games, Inc. All Rights Reserved.

#include "MapRpcModule.h"
#include "../RpcDispatcher.h"
#include "../RpcParamUtil.h"
#include "../../Map/MapFloorActor.h"
#include "../../Map/MapFloorLibrary.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

namespace
{
	FString ResolveMapPath(const TSharedPtr<FJsonObject>& P)
	{
		FString FileName = RpcParam::GetString(P, TEXT("fileName"), TEXT("MapSize"));
		if (!FileName.EndsWith(TEXT(".json")))
		{
			FileName += TEXT(".json");
		}
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MapData"), FileName);
	}

	/** 바닥판을 노면 위 얼마에 둘지(cm). 주차면 판(LV_Park_03 은 노면 +1.1 cm)보다 낮아야 면이 덮이지 않는다. */
	constexpr double MapFloorAboveGroundCm = 0.5;

	/**
	 * 바닥판 사각형(중심·크기·yaw, cm) 3×3 표본점의 노면 높이 중앙값(cm). 정적 오브젝트 질의
	 * (lot.set 의 OvGroundZ 와 같은 이유 — LV_Park_01 노면은 Visibility 를 무시한다). 주차면 판
	 * (BP_ParkingSlot·프롭 면)은 노면이 아니므로 뺀다. 하나도 못 찾으면 false.
	 */
	bool MapGroundZ(UWorld* World, const FVector2D& CenterCm, double SizeXCm, double SizeYCm, double YawDeg, double& OutZCm)
	{
		if (!World) return false;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MapFloorGround), /*bTraceComplex=*/true);
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			const FString Cls = It->GetClass()->GetName();
			if (Cls.StartsWith(TEXT("BP_ParkingSlot")) || Cls.StartsWith(TEXT("BayProp")))
			{
				Params.AddIgnoredActor(*It);
			}
		}
		const FVector2D AxisX = FVector2D(FMath::Cos(FMath::DegreesToRadians(YawDeg)), FMath::Sin(FMath::DegreesToRadians(YawDeg)));
		const FVector2D AxisY(-AxisX.Y, AxisX.X);
		TArray<double> Hits;
		for (int32 i = -1; i <= 1; ++i)
		{
			for (int32 j = -1; j <= 1; ++j)
			{
				// 가장자리는 연석·보도에 걸리기 쉬워 안쪽 40% 지점에서 잰다.
				const FVector2D Pt = CenterCm + AxisX * (i * 0.4 * SizeXCm) + AxisY * (j * 0.4 * SizeYCm);
				FHitResult Hit;
				if (World->LineTraceSingleByObjectType(Hit, FVector(Pt.X, Pt.Y, 300.0), FVector(Pt.X, Pt.Y, -1000.0),
					FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllStaticObjects), Params))
				{
					Hits.Add(Hit.ImpactPoint.Z);
				}
			}
		}
		if (Hits.Num() == 0) return false;
		Hits.Sort();
		OutZCm = Hits[Hits.Num() / 2];
		return true;
	}

	/** 바닥판 상태 — 바닥이 없으면 exists:false·크기 0(스폰하지 않는다). sizeZ 는 sizeY 와 같은 값(옛 이름). */
	TSharedPtr<FJsonObject> MapFloorState(UWorld* World)
	{
		const AMapFloorActor* Floor = AMapFloorActor::Find(World);
		TSharedPtr<FJsonObject> O;
		if (Floor)
		{
			O = UMapFloorLibrary::PlacementToJson(Floor->GetPlacement());
		}
		else
		{
			FMapFloorPlacement Empty;
			Empty.SizeXM = Empty.SizeYM = 0.f;
			Empty.ZM = 0.f;
			O = UMapFloorLibrary::PlacementToJson(Empty);
		}
		O->SetBoolField(TEXT("exists"), Floor != nullptr);
		O->SetNumberField(TEXT("sizeZ"), O->GetNumberField(TEXT("sizeY")));
		O->SetBoolField(TEXT("backdrop"), Floor && Floor->IsBackdropVisible());
		const FString Path = AMapFloorActor::GetPlacementFilePath(World);
		O->SetStringField(TEXT("file"), Path);
		O->SetBoolField(TEXT("fileExists"), FPaths::FileExists(Path));
		return O;
	}
}

void FMapRpcModule::Register(URpcDispatcher& Dispatcher)
{
	Dispatcher.Register(TEXT("map.resize"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AMapFloorActor* Floor = GetMapFloor(E); if (!Floor) return nullptr;
		double SizeX = 0.0, SizeZ = 0.0;
		if (!RpcParam::RequireFloat(P, TEXT("sizeX"), SizeX, E)) return nullptr;
		if (!RpcParam::RequireFloat(P, TEXT("sizeZ"), SizeZ, E)) return nullptr;
		Floor->SetFloorSize(static_cast<float>(SizeX), static_cast<float>(SizeZ));
		TSharedPtr<FJsonObject> O = MapFloorState(GetWorldPtr());
		O->SetBoolField(TEXT("ok"), true);
		return RpcDto::MakeObject(O);
	});

	// 조회 — 바닥이 없으면 스폰하지 않고 exists:false.
	Dispatcher.Register(TEXT("map.get"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* World = GetWorldPtr();
		if (!World) { E.FailDomain(TEXT("월드 컨텍스트 없음"), ERpcErrorKind::Busy); return nullptr; }
		return RpcDto::MakeObject(MapFloorState(World));
	});

	Dispatcher.Register(TEXT("map.save"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AMapFloorActor* Floor = GetMapFloor(E); if (!Floor) return nullptr;
		const FString Path = ResolveMapPath(P);
		if (!UMapFloorLibrary::SaveMapSizeToJson(Path, Floor->WidthM, Floor->DepthM))
		{
			E.FailDomain(FString::Printf(TEXT("맵 크기 저장 실패: %s"), *Path));
			return nullptr;
		}
		return RpcDto::OkTrue();
	});

	Dispatcher.Register(TEXT("map.load"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AMapFloorActor* Floor = GetMapFloor(E); if (!Floor) return nullptr;
		const FString Path = ResolveMapPath(P);
		float W = 0.f, D = 0.f;
		if (!UMapFloorLibrary::LoadMapSizeFromJson(Path, W, D))
		{
			E.FailDomain(FString::Printf(TEXT("맵 크기 로드 실패: %s"), *Path));
			return nullptr;
		}
		Floor->SetFloorSize(W, D);
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("sizeX"), Floor->WidthM);
		O->SetNumberField(TEXT("sizeZ"), Floor->DepthM);
		return RpcDto::MakeObject(O);
	});

	// map.setFloor {sizeX?, sizeY?(=sizeZ), center?{x,y}, yaw?, z?, save?=true} — 주차장 바닥판의 크기·위치·방향.
	// 바닥이 없는 레벨이면 원경 없이 새로 깔고(이때 sizeX·sizeY 필수), 레벨별 파일에 남겨 기동·레벨 전환 때 복원한다.
	Dispatcher.Register(TEXT("map.setFloor"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* World = GetWorldPtr();
		if (!World) { E.FailDomain(TEXT("월드 컨텍스트 없음"), ERpcErrorKind::Busy); return nullptr; }

		AMapFloorActor* Existing = AMapFloorActor::Find(World);
		const FMapFloorPlacement Cur = Existing ? Existing->GetPlacement() : FMapFloorPlacement();
		const bool bHasX = RpcParam::Has(P, TEXT("sizeX"));
		const FString YKey = RpcParam::Has(P, TEXT("sizeY")) ? TEXT("sizeY") : TEXT("sizeZ");
		const bool bHasY = RpcParam::Has(P, YKey);
		if (!Existing && !(bHasX && bHasY))
		{
			E.FailDomain(TEXT("바닥판이 없는 레벨 — 새로 깔려면 sizeX·sizeY 가 필요합니다"), ERpcErrorKind::BadParams);
			return nullptr;
		}
		const double SizeX = bHasX ? RpcParam::GetFloat(P, TEXT("sizeX")) : Cur.SizeXM;
		const double SizeY = bHasY ? RpcParam::GetFloat(P, YKey) : Cur.SizeYM;
		if (!FMath::IsFinite(SizeX) || !FMath::IsFinite(SizeY) || SizeX <= 0.0 || SizeY <= 0.0)
		{
			E.FailDomain(TEXT("sizeX·sizeY 는 0 보다 큰 수(m)여야 합니다"), ERpcErrorKind::BadParams);
			return nullptr;
		}
		const FVector C = RpcParam::GetVec3(P, TEXT("center"), FVector(Cur.CenterM.X, Cur.CenterM.Y, 0.0));
		const double Yaw = RpcParam::GetFloat(P, TEXT("yaw"), Cur.YawDeg);
		const bool bSave = RpcParam::GetBool(P, TEXT("save"), true);

		AMapFloorActor* Floor = Existing ? Existing : AMapFloorActor::GetOrSpawn(World, /*bBackdropIfSpawned=*/false);
		if (!Floor) { E.FailDomain(TEXT("맵 바닥 액터를 생성할 수 없습니다")); return nullptr; }
		Floor->SetFloorSize(static_cast<float>(SizeX), static_cast<float>(SizeY));

		// z 생략 = 클램프된 크기의 사각형 아래 노면 + 0.5 cm, 노면을 못 찾으면 기본 2 cm(빈 기본맵과 같은 높이).
		FString ZSource;
		double ZCm = 0.0;
		if (RpcParam::Has(P, TEXT("z")))
		{
			ZCm = RpcParam::GetFloat(P, TEXT("z")) * Floor->MetersToUU;
			ZSource = TEXT("param");
		}
		else if (MapGroundZ(World, FVector2D(C.X, C.Y) * Floor->MetersToUU, Floor->WidthM * Floor->MetersToUU,
			Floor->DepthM * Floor->MetersToUU, Yaw, ZCm))
		{
			ZCm += MapFloorAboveGroundCm;
			ZSource = TEXT("ground");
		}
		else
		{
			ZCm = Floor->FloorZ;
			ZSource = TEXT("default");
		}
		Floor->SetPlacement(FVector2D(C.X, C.Y), static_cast<float>(Yaw), static_cast<float>(ZCm));

		FString Path;
		const bool bSaved = bSave && Floor->SavePlacement(Path);
		if (bSave && !bSaved) { UE_LOG(LogTemp, Warning, TEXT("[MapFloor] 바닥판 저장 실패: %s"), *Path); }
		TSharedPtr<FJsonObject> O = MapFloorState(World);
		O->SetBoolField(TEXT("ok"), true);
		O->SetBoolField(TEXT("created"), Existing == nullptr);
		O->SetBoolField(TEXT("saved"), bSaved);
		O->SetStringField(TEXT("zSource"), ZSource);
		O->SetNumberField(TEXT("changed"), 1);
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("map.setFloor"), { true, false,
		TEXT("{sizeX?, sizeY?(=sizeZ), center?:{x,y}, yaw?, z?, save?=true}"),
		TEXT("주차장 바닥판(m·도) — 크기 10..500 클램프, z 생략 = 노면+0.5 cm. 레벨별 파일에 저장돼 재기동 후 복원. 응답 {exists, sizeX, sizeY, center, yaw, z, backdrop, created, saved, zSource, file}") });

	// map.clearFloor {keepFile?=false} — RPC 로 깐 바닥판은 지우고, 기본맵 바닥(원경 있음)은 160 m·원점으로 되돌린다.
	Dispatcher.Register(TEXT("map.clearFloor"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* World = GetWorldPtr();
		if (!World) { E.FailDomain(TEXT("월드 컨텍스트 없음"), ERpcErrorKind::Busy); return nullptr; }
		const bool bKeepFile = RpcParam::GetBool(P, TEXT("keepFile"), false);
		FString Action = TEXT("none");
		if (AMapFloorActor* Floor = AMapFloorActor::Find(World))
		{
			if (Floor->IsBackdropVisible())
			{
				Floor->ResetFloorSize();
				Floor->SetPlacement(FVector2D::ZeroVector, 0.f, Floor->FloorZ);
				Action = TEXT("reset");
			}
			else
			{
				Floor->Destroy();
				Action = TEXT("removed");
			}
		}
		const FString Path = AMapFloorActor::GetPlacementFilePath(World);
		const bool bFileRemoved = !bKeepFile && FPaths::FileExists(Path) && IFileManager::Get().Delete(*Path);
		TSharedPtr<FJsonObject> O = MapFloorState(World);
		O->SetBoolField(TEXT("ok"), true);
		O->SetStringField(TEXT("action"), Action);
		O->SetBoolField(TEXT("fileRemoved"), bFileRemoved);
		O->SetNumberField(TEXT("changed"), (Action != TEXT("none") || bFileRemoved) ? 1 : 0);
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("map.clearFloor"), { true, true, TEXT("{keepFile?:bool=false}"),
		TEXT("바닥판 지우기 — RPC 로 깐 판은 제거, 기본맵 바닥은 160 m·원점 복귀. 레벨 파일도 지운다(keepFile:true 면 화면만)") });
}
