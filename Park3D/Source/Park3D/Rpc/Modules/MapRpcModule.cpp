// Copyright Epic Games, Inc. All Rights Reserved.

#include "MapRpcModule.h"
#include "../RpcDispatcher.h"
#include "../RpcParamUtil.h"
#include "../../Map/MapFloorActor.h"
#include "../../Map/MapFloorLibrary.h"
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
		return RpcDto::OkTrue();
	});

	Dispatcher.Register(TEXT("map.get"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AMapFloorActor* Floor = GetMapFloor(E); if (!Floor) return nullptr;
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("sizeX"), Floor->WidthM);
		O->SetNumberField(TEXT("sizeZ"), Floor->DepthM);
		return RpcDto::MakeObject(O);
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
}
