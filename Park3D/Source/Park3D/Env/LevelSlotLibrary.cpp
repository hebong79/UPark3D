// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSlotLibrary.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace Park3DLevelSlots
{
	namespace
	{
		/** "BP_ParkingSlot_C_12" 의 끝 숫자(ParkingPresetManager 와 같은 규칙). */
		int32 LsTrailingNumber(const FString& Name)
		{
			int32 End = Name.Len();
			while (End > 0 && FChar::IsDigit(Name[End - 1])) --End;
			return End < Name.Len() ? FCString::Atoi(*Name.Mid(End)) : 0;
		}

		TSharedPtr<FJsonObject> LsVec(const FVector& V)
		{
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetNumberField(TEXT("x"), V.X);
			O->SetNumberField(TEXT("y"), V.Y);
			O->SetNumberField(TEXT("z"), V.Z);
			return O;
		}

		FVector LsVec(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, const FVector& Default)
		{
			const TSharedPtr<FJsonObject>* V = nullptr;
			if (!O.IsValid() || !O->TryGetObjectField(Key, V) || !V || !V->IsValid()) return Default;
			FVector Out = Default;
			double C = 0.0;
			if ((*V)->TryGetNumberField(TEXT("x"), C)) Out.X = C;
			if ((*V)->TryGetNumberField(TEXT("y"), C)) Out.Y = C;
			if ((*V)->TryGetNumberField(TEXT("z"), C)) Out.Z = C;
			return Out;
		}
	}

	bool IsSlotComponent(const UInstancedStaticMeshComponent* Comp)
	{
		return Comp && Comp->GetStaticMesh() && Comp->GetName().Contains(TEXT("Slot"));
	}

	void CollectSlotActors(UWorld* World, TArray<AActor*>& Out)
	{
		if (!World) return;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (IsValid(*It) && It->GetClass()->GetName().StartsWith(TEXT("BP_ParkingSlot")))
			{
				Out.Add(*It);
			}
		}
		Out.Sort([](const AActor& A, const AActor& B)
		{
			const int32 NA = LsTrailingNumber(A.GetName());
			const int32 NB = LsTrailingNumber(B.GetName());
			return NA != NB ? NA < NB : A.GetName() < B.GetName();
		});
	}

	UInstancedStaticMeshComponent* FindSlotComponent(AActor* Actor)
	{
		if (!Actor) return nullptr;
		TArray<UInstancedStaticMeshComponent*> Comps;
		Actor->GetComponents(Comps);
		for (UInstancedStaticMeshComponent* Comp : Comps)
		{
			if (IsSlotComponent(Comp)) return Comp;
		}
		return nullptr;
	}

	void Snapshot(UWorld* World, const TMap<FString, FTransform>& Override, TArray<FSlotInstance>& Out)
	{
		TArray<AActor*> Actors;
		CollectSlotActors(World, Actors);
		Snapshot(Actors, Override, Out);
	}

	void Snapshot(const TArray<AActor*>& Actors, const TMap<FString, FTransform>& Override, TArray<FSlotInstance>& Out)
	{
		for (AActor* Actor : Actors)
		{
			UInstancedStaticMeshComponent* Comp = FindSlotComponent(Actor);
			if (!Comp) continue;
			for (int32 i = 0; i < Comp->GetInstanceCount(); ++i)
			{
				FTransform T;
				if (!Comp->GetInstanceTransform(i, T, /*bWorldSpace=*/true)) continue;
				const FTransform* Saved = Override.Find(FString::Printf(TEXT("level:%s#%d"), *Actor->GetName(), i));
				FSlotInstance Inst;
				Inst.ActorName = Actor->GetName();
				Inst.Transform = Saved ? *Saved : T;
				Out.Add(MoveTemp(Inst));
			}
		}
	}

	int32 Apply(UWorld* World, const TArray<FSlotInstance>& In, TArray<FString>& OutMissingActors)
	{
		TArray<AActor*> Actors;
		CollectSlotActors(World, Actors);
		return Apply(Actors, In, OutMissingActors);
	}

	int32 Apply(const TArray<AActor*>& Actors, const TArray<FSlotInstance>& In, TArray<FString>& OutMissingActors)
	{
		// 액터 이름 → 인스턴스 목록(파일 순서 유지). 같은 액터가 파일에 흩어져 있어도 한 번에 바꾼다.
		TMap<FString, TArray<FTransform>> ByActor;
		TArray<FString> Order;
		for (const FSlotInstance& Inst : In)
		{
			if (!ByActor.Contains(Inst.ActorName)) Order.Add(Inst.ActorName);
			ByActor.FindOrAdd(Inst.ActorName).Add(Inst.Transform);
		}

		int32 Applied = 0;
		for (const FString& Name : Order)
		{
			AActor* const* Found = Actors.FindByPredicate([&Name](const AActor* A) { return A->GetName() == Name; });
			UInstancedStaticMeshComponent* Comp = Found ? FindSlotComponent(*Found) : nullptr;
			if (!Comp)
			{
				OutMissingActors.Add(Name);
				continue;
			}
			Comp->ClearInstances();
			for (const FTransform& T : ByActor[Name])
			{
				Comp->AddInstance(T, /*bWorldSpace=*/true);
				++Applied;
			}
			Comp->MarkRenderStateDirty();
		}
		return Applied;
	}

	TSharedRef<FJsonObject> ToJson(const FString& LevelPath, const TArray<FSlotInstance>& In)
	{
		TArray<TSharedPtr<FJsonValue>> Actors;
		TSharedPtr<FJsonObject> Cur;
		TArray<TSharedPtr<FJsonValue>> CurSlots;
		FString CurName;
		auto Flush = [&]()
		{
			if (!Cur.IsValid()) return;
			Cur->SetArrayField(TEXT("slots"), CurSlots);
			Actors.Add(MakeShared<FJsonValueObject>(Cur));
			Cur = nullptr;
			CurSlots.Reset();
		};
		for (const FSlotInstance& Inst : In)
		{
			if (!Cur.IsValid() || CurName != Inst.ActorName)
			{
				Flush();
				CurName = Inst.ActorName;
				Cur = MakeShared<FJsonObject>();
				Cur->SetStringField(TEXT("name"), CurName);
			}
			TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
			S->SetObjectField(TEXT("pos"), LsVec(Inst.Transform.GetLocation() / 100.f));
			S->SetNumberField(TEXT("yaw"), Inst.Transform.Rotator().Yaw);
			S->SetObjectField(TEXT("scale"), LsVec(Inst.Transform.GetScale3D()));
			CurSlots.Add(MakeShared<FJsonValueObject>(S));
		}
		Flush();

		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("isUnreal"), true);
		Root->SetStringField(TEXT("kind"), TEXT("levelSlots"));
		Root->SetStringField(TEXT("level"), LevelPath);
		Root->SetArrayField(TEXT("actors"), Actors);
		return Root;
	}

	bool FromJson(const TSharedPtr<FJsonObject>& Root, TArray<FSlotInstance>& Out, FString& OutLevelPath)
	{
		const TArray<TSharedPtr<FJsonValue>>* Actors = nullptr;
		FString Kind;
		if (!Root.IsValid() || !Root->TryGetStringField(TEXT("kind"), Kind) || Kind != TEXT("levelSlots")
			|| !Root->TryGetArrayField(TEXT("actors"), Actors) || !Actors)
		{
			return false;
		}
		Root->TryGetStringField(TEXT("level"), OutLevelPath);
		for (const TSharedPtr<FJsonValue>& AV : *Actors)
		{
			const TSharedPtr<FJsonObject>* A = nullptr;
			FString Name;
			const TArray<TSharedPtr<FJsonValue>>* Slots = nullptr;
			if (!AV.IsValid() || !AV->TryGetObject(A) || !A || !(*A)->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty()
				|| !(*A)->TryGetArrayField(TEXT("slots"), Slots) || !Slots)
			{
				continue;
			}
			for (const TSharedPtr<FJsonValue>& SV : *Slots)
			{
				const TSharedPtr<FJsonObject>* S = nullptr;
				if (!SV.IsValid() || !SV->TryGetObject(S) || !S) continue;
				FSlotInstance Inst;
				Inst.ActorName = Name;
				double Yaw = 0.0;
				(*S)->TryGetNumberField(TEXT("yaw"), Yaw);
				Inst.Transform = FTransform(FRotator(0.f, static_cast<float>(Yaw), 0.f),
					LsVec(*S, TEXT("pos"), FVector::ZeroVector) * 100.f,
					LsVec(*S, TEXT("scale"), FVector::OneVector));
				Out.Add(MoveTemp(Inst));
			}
		}
		return true;
	}

	bool SaveFile(const FString& Path, const FString& LevelPath, const TArray<FSlotInstance>& In)
	{
		FString Text;
		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Text);
		if (!FJsonSerializer::Serialize(ToJson(LevelPath, In), Writer)) return false;
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), /*Tree=*/true);
		return FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	bool LoadFile(const FString& Path, TArray<FSlotInstance>& Out, FString& OutLevelPath)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path)) return false;
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return false;
		return FromJson(Root, Out, OutLevelPath);
	}
}
