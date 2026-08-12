// Copyright 2026 Team Tofunut. All Rights Reserved.

/*
 * Converts Texture2D source data into immutable, broker-accounted pixel payloads.
 */

#include "WetClothing/Foundation/TextureAccess/WetClothingTextureReadback.h"

#include "Engine/Texture2D.h"
#include "UObject/ObjectKey.h"
#include "WetClothing/Foundation/Resources/DWCEditorResourceBroker.h"

namespace
{
    struct FReadbackCacheEntry
    {
        int32                       Width = 0;
        int32                       Height = 0;
        ETextureSourceFormat        Format = TSF_Invalid;
        FGuid                       SourceId;
        bool                        bSRGB = false;
        TextureAddress              AddressX = TA_Clamp;
        TextureAddress              AddressY = TA_Clamp;
        FWetClothingTextureReadback Data;
        uint64                      LastUsedSerial = 0;
    };

    TMap<FObjectKey, FReadbackCacheEntry> GTextureReadbackCache;
    TWeakPtr<FDWCEditorResourceBroker>     GReadbackResourceBroker;
    FGuid                                  GReadbackOwnerEpoch;
    uint64                                 GTextureReadbackUseSerial = 0;
    uint64                                 GTextureReadbackPayloadSerial = 0;
    uint64                                 GReadbackParticipantId = 0;

    uint64 GetReadbackCacheReferencedBytes()
    {
        uint64 TotalBytes = 0;
        for (const TPair<FObjectKey, FReadbackCacheEntry>& Pair : GTextureReadbackCache)
        {
            TotalBytes += Pair.Value.Data.GetAllocatedBytes();
        }
        return TotalBytes;
    }

    uint64 GetReadbackCacheBlockedBytes()
    {
        uint64 TotalBytes = 0;
        for (const TPair<FObjectKey, FReadbackCacheEntry>& Pair : GTextureReadbackCache)
        {
            if (Pair.Value.Data.GetPayloadSharedReferenceCount() > 1)
            {
                TotalBytes += Pair.Value.Data.GetAllocatedBytes();
            }
        }
        return TotalBytes;
    }

    uint64 GetReadbackCacheBudgetBytes()
    {
        if (const TSharedPtr<FDWCEditorResourceBroker> Broker = GReadbackResourceBroker.Pin())
        {
            return FMath::Max<uint64>(Broker->GetBudgetConfig().SharedCacheCPUBytes, 1);
        }
        return FDWCEditorResourceBudgetConfig().SharedCacheCPUBytes;
    }

    void TrimReadbackCache(const FObjectKey& ProtectedKey)
    {
        uint64 ReferencedBytes = GetReadbackCacheReferencedBytes();
        const uint64 BudgetBytes = GetReadbackCacheBudgetBytes();
        while (ReferencedBytes > BudgetBytes)
        {
            const FObjectKey* OldestKey = nullptr;
            uint64 OldestSerial = MAX_uint64;
            for (const TPair<FObjectKey, FReadbackCacheEntry>& Pair : GTextureReadbackCache)
            {
                if (Pair.Key != ProtectedKey &&
                    Pair.Value.Data.GetPayloadSharedReferenceCount() == 1 &&
                    Pair.Value.LastUsedSerial < OldestSerial)
                {
                    OldestSerial = Pair.Value.LastUsedSerial;
                    OldestKey = &Pair.Key;
                }
            }
            if (OldestKey == nullptr)
            {
                break;
            }
            GTextureReadbackCache.Remove(*OldestKey);
            ReferencedBytes = GetReadbackCacheReferencedBytes();
        }
    }

    void CollectReadbackMemory(TArray<FDWCEditorMemoryOwnerRecord>& OutOwners)
    {
        FDWCEditorMemoryOwnerRecord& CacheOwner = OutOwners.AddDefaulted_GetRef();
        CacheOwner.Identifier = TEXT("TextureReadback.CacheMetadata");
        CacheOwner.Subsystem = TEXT("TextureReadback");
        CacheOwner.Resource = TEXT("CacheMetadata");
        CacheOwner.Category = EDWCEditorMemoryCategory::SharedCacheCPU;
        CacheOwner.Accounting = EDWCEditorMemoryAccounting::Resident;
        CacheOwner.CurrentBytes = GTextureReadbackCache.GetAllocatedSize();
        CacheOwner.EntryCount = GTextureReadbackCache.Num();
        CacheOwner.Context = FString::Printf(
            TEXT("budget=%.2f MiB; referencedPayloadBytes=%.2f MiB; reclaimableNow=%.2f MiB; blocked=%.2f MiB"),
            static_cast<double>(GetReadbackCacheBudgetBytes()) / FDWCEditorResourceBudgetConfig::MiB,
            static_cast<double>(GetReadbackCacheReferencedBytes()) / FDWCEditorResourceBudgetConfig::MiB,
            static_cast<double>(FWetClothingTextureReadbackUtils::GetReclaimableCacheBytes()) /
                FDWCEditorResourceBudgetConfig::MiB,
            static_cast<double>(GetReadbackCacheBlockedBytes()) / FDWCEditorResourceBudgetConfig::MiB);
    }

    struct FReadbackMemoryDiagnosticRegistration
    {
        FReadbackMemoryDiagnosticRegistration()
        {
            FDWCEditorMemoryDiagnostics::RegisterCollector(TEXT("TextureReadback"), &CollectReadbackMemory);
        }

        ~FReadbackMemoryDiagnosticRegistration()
        {
            FDWCEditorMemoryDiagnostics::UnregisterCollector(TEXT("TextureReadback"));
        }
    } GReadbackMemoryDiagnosticRegistration;
} // namespace

bool FWetClothingTextureReadback::IsValid() const
{
    const TArray64<uint8>* RawData = GetRawData();
    return Width > 0 && Height > 0 && BytesPerPixel > 0 && RawData != nullptr &&
        RawData->Num() >= static_cast<int64>(Width) * Height * BytesPerPixel;
}

const TArray64<uint8>* FWetClothingTextureReadback::GetRawData() const
{
    return Payload.IsValid() ? &Payload->GetBytes() : nullptr;
}

uint64 FWetClothingTextureReadback::GetAllocatedBytes() const
{
    return Payload.IsValid() ? Payload->GetAllocatedBytes() : 0;
}

FLinearColor FWetClothingTextureReadback::GetLinearColor(const int32 X, const int32 Y) const
{
    const TArray64<uint8>* RawData = GetRawData();
    if (!IsValid() || RawData == nullptr)
    {
        return FLinearColor::Black;
    }

    const int32 ClampedX = FMath::Clamp(X, 0, Width - 1);
    const int32 ClampedY = FMath::Clamp(Y, 0, Height - 1);
    const int64 PixelOffset = (static_cast<int64>(ClampedY) * Width + ClampedX) * BytesPerPixel;
    const uint8* PixelPtr = RawData->GetData() + PixelOffset;
    FColor SRGBColor = FColor::Black;

    switch (Format)
    {
    case TSF_BGRA8:
        SRGBColor = *reinterpret_cast<const FColor*>(PixelPtr);
        break;
    case TSF_G8:
    {
        const uint8 Intensity = *PixelPtr;
        SRGBColor = FColor(Intensity, Intensity, Intensity, 255);
        break;
    }
    default:
        return FLinearColor::Black;
    }

    return bSRGB ? FLinearColor::FromSRGBColor(SRGBColor) : SRGBColor.ReinterpretAsLinear();
}

void FWetClothingTextureReadbackUtils::InitializeResourceBroker()
{
    check(IsInGameThread());
    if (GReadbackParticipantId != 0)
    {
        return;
    }

    const TSharedRef<FDWCEditorResourceBroker> Broker = FDWCEditorResourceBroker::Get();
    GReadbackResourceBroker = Broker;
    if (!GReadbackOwnerEpoch.IsValid())
    {
        GReadbackOwnerEpoch = FGuid::NewGuid();
    }

    FDWCEditorReclaimParticipantDescriptor Descriptor;
    Descriptor.Name = TEXT("DWC.TextureReadbackCache");
    Descriptor.Pool = EDWCEditorResourcePool::SharedCacheCPU;
    Descriptor.Priority = EDWCEditorReclaimPriority::SharedCache;
    Descriptor.QueryReclaimableBytes = []
    {
        return GetReclaimableCacheBytes();
    };
    Descriptor.Reclaim = [](const FDWCEditorResourceReclaimRequest& Request)
    {
        FDWCEditorResourceReclaimResult Result;
        Result.ImmediateBytes = ReclaimCacheBytes(Request.TargetBytes);
        return Result;
    };
    GReadbackParticipantId = Broker->RegisterParticipant(MoveTemp(Descriptor));
}

void FWetClothingTextureReadbackUtils::ShutdownResourceBroker()
{
    check(IsInGameThread());
    ClearCache();
    if (GReadbackParticipantId != 0)
    {
        if (const TSharedPtr<FDWCEditorResourceBroker> Broker = GReadbackResourceBroker.Pin())
        {
            Broker->UnregisterParticipant(GReadbackParticipantId);
        }
    }
    GReadbackParticipantId = 0;
    GReadbackResourceBroker.Reset();
    GReadbackOwnerEpoch.Invalidate();
}

bool FWetClothingTextureReadbackUtils::TryCreateReadbackInternal(
    const int32 Width,
    const int32 Height,
    const int32 BytesPerPixel,
    const ETextureSourceFormat Format,
    const bool bSRGB,
    const TextureAddress AddressX,
    const TextureAddress AddressY,
    TFunctionRef<bool(TArray64<uint8>&)> FillBytes,
    FWetClothingTextureReadback& OutTextureData,
    FString& OutErrorMessage,
    const FString& DebugName,
    const FString& FillFailureMessage)
{
    check(IsInGameThread());
    OutTextureData = FWetClothingTextureReadback();
    if (Width <= 0 || Height <= 0 || BytesPerPixel <= 0 ||
        (Format != TSF_BGRA8 && Format != TSF_G8))
    {
        OutErrorMessage = TEXT("The texture readback descriptor is invalid.");
        return false;
    }

    const uint64 ExpectedBytes = static_cast<uint64>(Width) * Height * BytesPerPixel;
    InitializeResourceBroker();
    const TSharedPtr<FDWCEditorResourceBroker> Broker = GReadbackResourceBroker.Pin();
    if (!Broker.IsValid())
    {
        OutErrorMessage = TEXT("The WCA editor resource broker is unavailable.");
        return false;
    }

    TSharedRef<FDWCEditorTextureReadbackPayload, ESPMode::ThreadSafe> Payload =
        MakeShared<FDWCEditorTextureReadbackPayload, ESPMode::ThreadSafe>();
    FDWCEditorAsyncOperationIdentity Owner;
    Owner.Key.Namespace = TEXT("DWC.TextureReadbackPayload");
    Owner.SessionEpoch = GReadbackOwnerEpoch;
    Owner.OperationId = ++GTextureReadbackPayloadSerial;
    Owner.Generation = 1;
    Payload->AccountedMemory.Configure(
        Broker->GetResourceGovernor(),
        EDWCEditorResourcePool::SharedCacheCPU,
        Owner,
        DebugName);

    FString AdmissionError;
    if (!Payload->AccountedMemory.TryAdoptActualBytes(ExpectedBytes, &AdmissionError))
    {
        OutErrorMessage = FString::Printf(
            TEXT("The shared editor memory budget cannot admit '%s' (%0.2f MiB). %s"),
            *DebugName,
            static_cast<double>(ExpectedBytes) / FDWCEditorResourceBudgetConfig::MiB,
            *AdmissionError);
        return false;
    }
    if (!FillBytes(Payload->Bytes))
    {
        OutErrorMessage = FillFailureMessage;
        return false;
    }

    const uint64 ActualBytes = Payload->Bytes.GetAllocatedSize();
    if (Payload->Bytes.Num() < static_cast<int64>(ExpectedBytes))
    {
        OutErrorMessage = TEXT("The texture readback payload contains fewer bytes than expected.");
        return false;
    }
    if (!Payload->AccountedMemory.TryAdoptActualBytes(ActualBytes, &AdmissionError))
    {
        OutErrorMessage = FString::Printf(
            TEXT("The shared editor memory budget cannot retain '%s'. %s"),
            *DebugName,
            *AdmissionError);
        return false;
    }

    FDWCEditorMemoryOwnerRecord ResidentRecord;
    ResidentRecord.Identifier = FString::Printf(
        TEXT("TextureReadback.Payload.%llu"),
        Owner.OperationId);
    ResidentRecord.Subsystem = TEXT("TextureReadback");
    ResidentRecord.Resource = TEXT("PixelPayload");
    ResidentRecord.Category = EDWCEditorMemoryCategory::SharedCacheCPU;
    ResidentRecord.Accounting = EDWCEditorMemoryAccounting::Resident;
    ResidentRecord.CurrentBytes = ActualBytes;
    ResidentRecord.EntryCount = 1;
    ResidentRecord.Context = DebugName;
    Payload->ResidentMemoryOwner = FDWCEditorMemoryOwner(ResidentRecord);

    OutTextureData.Width = Width;
    OutTextureData.Height = Height;
    OutTextureData.BytesPerPixel = BytesPerPixel;
    OutTextureData.bSRGB = bSRGB;
    OutTextureData.Format = Format;
    OutTextureData.AddressX = AddressX;
    OutTextureData.AddressY = AddressY;
    OutTextureData.Payload = Payload;
    OutErrorMessage.Reset();
    return true;
}

bool FWetClothingTextureReadbackUtils::TryReadTextureSourceData(
    UTexture2D* Texture,
    FWetClothingTextureReadback& OutTextureData,
    FString& OutErrorMessage)
{
#if WITH_EDITORONLY_DATA
    check(IsInGameThread());
    OutTextureData = FWetClothingTextureReadback();
    if (Texture == nullptr)
    {
        OutErrorMessage = TEXT("Texture is null.");
        return false;
    }

    const FObjectKey Key(Texture);
    if (FReadbackCacheEntry* Cached = GTextureReadbackCache.Find(Key))
    {
        if (Cached->Width == Texture->Source.GetSizeX() &&
            Cached->Height == Texture->Source.GetSizeY() &&
            Cached->Format == Texture->Source.GetFormat() &&
            Cached->SourceId == Texture->Source.GetId() &&
            Cached->bSRGB == Texture->SRGB &&
            Cached->AddressX == Texture->AddressX &&
            Cached->AddressY == Texture->AddressY)
        {
            OutTextureData = Cached->Data;
            Cached->LastUsedSerial = ++GTextureReadbackUseSerial;
            OutErrorMessage.Reset();
            return true;
        }
        GTextureReadbackCache.Remove(Key);
    }

    if (!Texture->Source.IsValid())
    {
        OutErrorMessage = FString::Printf(
            TEXT("Texture '%s' does not have readable source data."),
            *Texture->GetName());
        return false;
    }

    const ETextureSourceFormat SourceFormat = Texture->Source.GetFormat();
    if (SourceFormat != TSF_BGRA8 && SourceFormat != TSF_G8)
    {
        OutErrorMessage = FString::Printf(
            TEXT("Texture '%s' uses an unsupported source format. DWC currently supports BGRA8 and G8 source data."),
            *Texture->GetName());
        return false;
    }

    if (!TryCreateReadbackInternal(
            IntCastChecked<int32>(Texture->Source.GetSizeX()),
            IntCastChecked<int32>(Texture->Source.GetSizeY()),
            IntCastChecked<int32>(Texture->Source.GetBytesPerPixel()),
            SourceFormat,
            Texture->SRGB,
            Texture->AddressX,
            Texture->AddressY,
            [Texture](TArray64<uint8>& Bytes)
            {
                return Texture->Source.GetMipData(Bytes, 0);
            },
            OutTextureData,
            OutErrorMessage,
            FString::Printf(TEXT("Texture readback '%s'"), *Texture->GetPathName()),
            FString::Printf(TEXT("Failed to read source pixels from texture '%s'."), *Texture->GetName())))
    {
        return false;
    }

    FReadbackCacheEntry& CacheEntry = GTextureReadbackCache.FindOrAdd(Key);
    CacheEntry.Width = OutTextureData.Width;
    CacheEntry.Height = OutTextureData.Height;
    CacheEntry.Format = OutTextureData.Format;
    CacheEntry.SourceId = Texture->Source.GetId();
    CacheEntry.bSRGB = OutTextureData.bSRGB;
    CacheEntry.AddressX = OutTextureData.AddressX;
    CacheEntry.AddressY = OutTextureData.AddressY;
    CacheEntry.Data = OutTextureData;
    CacheEntry.LastUsedSerial = ++GTextureReadbackUseSerial;
    TrimReadbackCache(Key);
    return true;
#else
    OutErrorMessage = TEXT("Texture source readback requires editor-only source data.");
    return false;
#endif
}

bool FWetClothingTextureReadbackUtils::TryCreateBGRA8Readback(
    TArray<FColor>&& Pixels,
    const FIntPoint Resolution,
    const bool bSRGB,
    FWetClothingTextureReadback& OutTextureData,
    FString& OutErrorMessage,
    const FString& DebugName)
{
    const int64 ExpectedPixels = static_cast<int64>(Resolution.X) * Resolution.Y;
    if (Resolution.X <= 0 || Resolution.Y <= 0 || Pixels.Num() != ExpectedPixels)
    {
        OutErrorMessage = TEXT("The BGRA8 texture readback dimensions are incomplete.");
        return false;
    }
    return TryCreateReadbackInternal(
        Resolution.X,
        Resolution.Y,
        sizeof(FColor),
        TSF_BGRA8,
        bSRGB,
        TA_Clamp,
        TA_Clamp,
        [&Pixels](TArray64<uint8>& Bytes)
        {
            Bytes.SetNumUninitialized(static_cast<int64>(Pixels.Num()) * sizeof(FColor));
            FMemory::Memcpy(Bytes.GetData(), Pixels.GetData(), Bytes.Num());
            Pixels.Reset();
            return true;
        },
        OutTextureData,
        OutErrorMessage,
        DebugName,
        TEXT("Could not copy the BGRA8 texture readback payload."));
}

bool FWetClothingTextureReadbackUtils::TryCreateG8Readback(
    TArray<uint8>&& Values,
    const FIntPoint Resolution,
    FWetClothingTextureReadback& OutTextureData,
    FString& OutErrorMessage,
    const FString& DebugName)
{
    const int64 ExpectedPixels = static_cast<int64>(Resolution.X) * Resolution.Y;
    if (Resolution.X <= 0 || Resolution.Y <= 0 || Values.Num() != ExpectedPixels)
    {
        OutErrorMessage = TEXT("The G8 texture readback dimensions are incomplete.");
        return false;
    }
    return TryCreateReadbackInternal(
        Resolution.X,
        Resolution.Y,
        1,
        TSF_G8,
        false,
        TA_Clamp,
        TA_Clamp,
        [&Values](TArray64<uint8>& Bytes)
        {
            Bytes.Append(Values.GetData(), Values.Num());
            Values.Reset();
            return true;
        },
        OutTextureData,
        OutErrorMessage,
        DebugName,
        TEXT("Could not copy the G8 texture readback payload."));
}

uint64 FWetClothingTextureReadbackUtils::GetReclaimableCacheBytes()
{
    check(IsInGameThread());
    uint64 ReclaimableBytes = 0;
    for (const TPair<FObjectKey, FReadbackCacheEntry>& Pair : GTextureReadbackCache)
    {
        if (Pair.Value.Data.GetPayloadSharedReferenceCount() == 1)
        {
            ReclaimableBytes += Pair.Value.Data.GetAllocatedBytes();
        }
    }
    return ReclaimableBytes;
}

uint64 FWetClothingTextureReadbackUtils::ReclaimCacheBytes(const uint64 TargetBytes)
{
    check(IsInGameThread());
    uint64 ReclaimedBytes = 0;
    while (ReclaimedBytes < TargetBytes)
    {
        const FObjectKey* OldestKey = nullptr;
        uint64 OldestSerial = MAX_uint64;
        for (const TPair<FObjectKey, FReadbackCacheEntry>& Pair : GTextureReadbackCache)
        {
            if (Pair.Value.Data.GetPayloadSharedReferenceCount() == 1 &&
                Pair.Value.LastUsedSerial < OldestSerial)
            {
                OldestKey = &Pair.Key;
                OldestSerial = Pair.Value.LastUsedSerial;
            }
        }
        if (OldestKey == nullptr)
        {
            break;
        }
        const FReadbackCacheEntry* Entry = GTextureReadbackCache.Find(*OldestKey);
        ReclaimedBytes += Entry != nullptr ? Entry->Data.GetAllocatedBytes() : 0;
        GTextureReadbackCache.Remove(*OldestKey);
    }
    return ReclaimedBytes;
}

void FWetClothingTextureReadbackUtils::ClearCache()
{
    check(IsInGameThread());
    GTextureReadbackCache.Reset();
    GTextureReadbackUseSerial = 0;
}

void FWetClothingTextureReadbackUtils::InvalidateTexture(UTexture2D* Texture)
{
    check(IsInGameThread());
    if (Texture != nullptr)
    {
        GTextureReadbackCache.Remove(FObjectKey(Texture));
    }
}
