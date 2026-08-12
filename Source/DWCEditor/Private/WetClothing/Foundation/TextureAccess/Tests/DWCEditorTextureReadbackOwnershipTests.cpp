// Copyright 2026 Team Tofunut. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Texture2D.h"
#include "Misc/AutomationTest.h"
#include "WetClothing/Foundation/Resources/DWCEditorResourceBroker.h"
#include "WetClothing/Foundation/TextureAccess/WetClothingTextureReadback.h"

namespace
{
    uint64 GetSharedCacheUsedBytes()
    {
        const FDWCEditorResourceGovernorDiagnostics Diagnostics =
            FDWCEditorResourceBroker::Get()->GetResourceGovernor()->GetDiagnostics();
        for (const FDWCEditorResourcePoolDiagnostics& Pool : Diagnostics.Pools)
        {
            if (Pool.Pool == EDWCEditorResourcePool::SharedCacheCPU)
            {
                return Pool.UsedBytes;
            }
        }
        return 0;
    }

    UTexture2D* MakeSourceTexture()
    {
        UTexture2D* Texture = NewObject<UTexture2D>(GetTransientPackage());
        const FColor Pixels[] = {
            FColor::Red,
            FColor::Green,
            FColor::Blue,
            FColor::White};
        Texture->Source.Init(
            2,
            2,
            1,
            1,
            TSF_BGRA8,
            reinterpret_cast<const uint8*>(Pixels));
        Texture->SRGB = false;
        Texture->AddressX = TA_Clamp;
        Texture->AddressY = TA_Clamp;
        return Texture;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorTextureReadbackSharedPayloadOwnershipTest,
    "DWC.Editor.Foundation.TextureReadback.SharedPayloadOwnership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorTextureReadbackSharedPayloadOwnershipTest::RunTest(const FString&)
{
    FWetClothingTextureReadbackUtils::ClearCache();
    const uint64 BaselineBytes = GetSharedCacheUsedBytes();

    FWetClothingTextureReadback First;
    FString Error;
    TArray<FColor> Pixels{FColor(32, 64, 96, 255)};
    TestTrue(
        TEXT("A generated readback acquires broker-owned payload memory."),
        FWetClothingTextureReadbackUtils::TryCreateBGRA8Readback(
            MoveTemp(Pixels), FIntPoint(1, 1), false, First, Error,
            TEXT("Texture readback ownership test")));
    TestTrue(TEXT("The generated readback is valid."), First.IsValid());
    const uint64 PayloadBytes = First.GetAllocatedBytes();
    TestTrue(TEXT("The payload has a physical allocation."), PayloadBytes >= sizeof(FColor));
    TestEqual(
        TEXT("The payload contributes one SharedCacheCPU reservation."),
        GetSharedCacheUsedBytes() - BaselineBytes,
        PayloadBytes);

    FWetClothingTextureReadback Second = First;
    TestEqual(
        TEXT("Copying a readback view does not reserve the same payload twice."),
        GetSharedCacheUsedBytes() - BaselineBytes,
        PayloadBytes);
    First = FWetClothingTextureReadback();
    TestEqual(
        TEXT("The reservation survives while another view retains the payload."),
        GetSharedCacheUsedBytes() - BaselineBytes,
        PayloadBytes);
    Second = FWetClothingTextureReadback();
    TestEqual(
        TEXT("The last view releases the payload and its reservation together."),
        GetSharedCacheUsedBytes(),
        BaselineBytes);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorTextureReadbackCacheLifetimeTest,
    "DWC.Editor.Foundation.TextureReadback.CacheLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorTextureReadbackCacheLifetimeTest::RunTest(const FString&)
{
    FWetClothingTextureReadbackUtils::ClearCache();
    const uint64 BaselineBytes = GetSharedCacheUsedBytes();
    UTexture2D* Texture = MakeSourceTexture();

    FWetClothingTextureReadback First;
    FWetClothingTextureReadback Second;
    FString Error;
    TestTrue(
        TEXT("The source texture can be read."),
        FWetClothingTextureReadbackUtils::TryReadTextureSourceData(Texture, First, Error));
    TestTrue(
        TEXT("A second read resolves the cached payload."),
        FWetClothingTextureReadbackUtils::TryReadTextureSourceData(Texture, Second, Error));
    TestEqual(
        TEXT("Cache hits return the same immutable payload."),
        First.GetPayloadIdentity(),
        Second.GetPayloadIdentity());
    const uint64 PayloadBytes = First.GetAllocatedBytes();
    TestEqual(
        TEXT("Cache and external views share one reservation."),
        GetSharedCacheUsedBytes() - BaselineBytes,
        PayloadBytes);

    FWetClothingTextureReadbackUtils::ClearCache();
    TestEqual(
        TEXT("Clearing cache ownership does not release a payload still used externally."),
        GetSharedCacheUsedBytes() - BaselineBytes,
        PayloadBytes);
    First = FWetClothingTextureReadback();
    Second = FWetClothingTextureReadback();
    TestEqual(
        TEXT("External release after cache eviction returns the reservation."),
        GetSharedCacheUsedBytes(),
        BaselineBytes);

    TestTrue(
        TEXT("The texture can be cached again."),
        FWetClothingTextureReadbackUtils::TryReadTextureSourceData(Texture, First, Error));
    const uint64 ReclaimableBytes = FWetClothingTextureReadbackUtils::GetReclaimableCacheBytes();
    TestEqual(
        TEXT("An externally referenced cache payload is not immediately reclaimable."),
        ReclaimableBytes,
        static_cast<uint64>(0));
    First = FWetClothingTextureReadback();
    TestEqual(
        TEXT("A cache-only payload becomes reclaimable."),
        FWetClothingTextureReadbackUtils::GetReclaimableCacheBytes(),
        PayloadBytes);
    TestEqual(
        TEXT("LRU reclaim releases the cache-only payload."),
        FWetClothingTextureReadbackUtils::ReclaimCacheBytes(PayloadBytes),
        PayloadBytes);
    TestEqual(TEXT("Reclaim returns to baseline."), GetSharedCacheUsedBytes(), BaselineBytes);
    return true;
}

#endif
