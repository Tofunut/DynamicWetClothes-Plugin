// Copyright 2026 Team Tofunut. All Rights Reserved.

/*
 * Declares texture readback results and helpers for reading Texture2D source data.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture.h"
#include "WetClothing/Foundation/Diagnostics/DWCEditorMemoryDiagnostics.h"
#include "WetClothing/Foundation/Resources/DWCEditorAccountedMemory.h"

class UTexture2D;

/** Immutable pixel storage whose allocation and resource-governor lease share one lifetime. */
class FDWCEditorTextureReadbackPayload final
{
  public:
    FDWCEditorTextureReadbackPayload() = default;
    ~FDWCEditorTextureReadbackPayload() = default;

    const TArray64<uint8>& GetBytes() const { return Bytes; }
    uint64 GetAllocatedBytes() const { return Bytes.GetAllocatedSize(); }

  private:
    friend class FWetClothingTextureReadbackUtils;

    TArray64<uint8> Bytes;
    FDWCEditorAccountedMemory AccountedMemory;
    FDWCEditorMemoryOwner ResidentMemoryOwner;
};

struct FWetClothingTextureReadback
{
    int32                Width = 0;
    int32                Height = 0;
    int32                BytesPerPixel = 0;
    bool                 bSRGB = true;
    ETextureSourceFormat Format = TSF_Invalid;
    TextureAddress       AddressX = TA_Clamp;
    TextureAddress       AddressY = TA_Clamp;

    bool         IsValid() const;
    FLinearColor GetLinearColor(int32 X, int32 Y) const;
    const TArray64<uint8>* GetRawData() const;
    uint64 GetAllocatedBytes() const;
    const void* GetPayloadIdentity() const { return Payload.Get(); }
    int32 GetPayloadSharedReferenceCount() const
    {
        return Payload.IsValid() ? Payload.GetSharedReferenceCount() : 0;
    }

  private:
    friend class FWetClothingTextureReadbackUtils;
    TSharedPtr<const FDWCEditorTextureReadbackPayload, ESPMode::ThreadSafe> Payload;
};

class FWetClothingTextureReadbackUtils
{
  public:
    /** Registers the process-wide readback cache with the editor resource broker. */
    static void InitializeResourceBroker();
    static void ShutdownResourceBroker();

    static bool TryReadTextureSourceData(
        UTexture2D*                  Texture,
        FWetClothingTextureReadback& OutTextureData,
        FString&                     OutErrorMessage);
    static bool TryCreateBGRA8Readback(
        TArray<FColor>&&              Pixels,
        FIntPoint                     Resolution,
        bool                          bSRGB,
        FWetClothingTextureReadback&  OutTextureData,
        FString&                      OutErrorMessage,
        const FString&                DebugName = TEXT("Generated BGRA8 texture readback"));
    static bool TryCreateG8Readback(
        TArray<uint8>&&               Values,
        FIntPoint                     Resolution,
        FWetClothingTextureReadback&  OutTextureData,
        FString&                      OutErrorMessage,
        const FString&                DebugName = TEXT("Generated G8 texture readback"));

    static uint64 GetReclaimableCacheBytes();
    static uint64 ReclaimCacheBytes(uint64 TargetBytes);
    static void InvalidateTexture(UTexture2D* Texture);
    static void ClearCache();

  private:
    static bool TryCreateReadbackInternal(
        int32                         Width,
        int32                         Height,
        int32                         BytesPerPixel,
        ETextureSourceFormat          Format,
        bool                          bSRGB,
        TextureAddress                AddressX,
        TextureAddress                AddressY,
        TFunctionRef<bool(TArray64<uint8>&)> FillBytes,
        FWetClothingTextureReadback&  OutTextureData,
        FString&                      OutErrorMessage,
        const FString&                DebugName,
        const FString&                FillFailureMessage);
};
