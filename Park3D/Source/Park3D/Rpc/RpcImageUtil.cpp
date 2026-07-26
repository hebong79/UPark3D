// Copyright Epic Games, Inc. All Rights Reserved.

#include "RpcImageUtil.h"
#include "Misc/Base64.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"

namespace RpcImage
{
	bool EncodeColors(const TArray<FColor>& Pixels, int32 W, int32 H, bool bPng, int32 Quality, TArray<uint8>& OutBytes)
	{
		OutBytes.Reset();
		if (W <= 0 || H <= 0 || Pixels.Num() < W * H)
		{
			return false;
		}

		IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		const EImageFormat Format = bPng ? EImageFormat::PNG : EImageFormat::JPEG;
		TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(Format);
		if (!Wrapper.IsValid())
		{
			return false;
		}

		// FColor 는 메모리상 BGRA 순서.
		if (!Wrapper->SetRaw(Pixels.GetData(), static_cast<int64>(Pixels.Num()) * sizeof(FColor), W, H, ERGBFormat::BGRA, 8))
		{
			return false;
		}

		// JPEG 는 Quality(1~100), PNG 는 무시(무손실).
		const int32 Q = bPng ? 0 : FMath::Clamp(Quality, 1, 100);
		const TArray64<uint8>& Compressed = Wrapper->GetCompressed(Q);
		if (Compressed.Num() == 0)
		{
			return false;
		}
		OutBytes.Append(Compressed.GetData(), static_cast<int32>(Compressed.Num()));
		return true;
	}

	FString ToBase64(const TArray<uint8>& Bytes)
	{
		if (Bytes.Num() == 0)
		{
			return FString();
		}
		return FBase64::Encode(Bytes.GetData(), Bytes.Num());
	}
}
