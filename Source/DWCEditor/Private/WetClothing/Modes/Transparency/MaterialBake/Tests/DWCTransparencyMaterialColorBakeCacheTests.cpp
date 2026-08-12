//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "Misc/AutomationTest.h"
#include "WetClothing/Foundation/Resources/DWCEditorResourceBroker.h"
#include "WetClothing/Modes/Transparency/MaterialBake/DWCTransparencyMaterialColorBakeCache.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySignatureService.h"

#if WITH_DEV_AUTOMATION_TESTS

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

    void InitializeValidKey(
        FDWCTransparencyMaterialColorBakeKey& Key,
        const TCHAR* Identity)
    {
        Key.OwnerAssetPath = FSoftObjectPath(TEXT("/Game/Test/WCA_A.WCA_A"));
        Key.SourceMeshPath = FSoftObjectPath(TEXT("/Game/Test/SK_Source.SK_Source"));
        Key.MaterialSlotIndex = 3;
        Key.SourceUVChannel = 1;
        Key.SourceBakeResolution = 2048;
        Key.IdentityVersion = FDWCTransparencyMaterialSurfaceBakeIdentity::Version;
        Key.CacheIdentity = Identity;
        Key.SourceMeshContentSignature = TEXT("MeshContent");
        Key.EffectiveMaterialSignature = TEXT("MaterialState");
        Key.PlacementSignature = TEXT("Placement");
    }

    FWetClothingTextureReadback MakeBGRA8Readback(const FColor& Pixel)
    {
        FWetClothingTextureReadback Result;
        TArray<FColor> Pixels{Pixel};
        FString Error;
        FWetClothingTextureReadbackUtils::TryCreateBGRA8Readback(
            MoveTemp(Pixels), FIntPoint(1, 1), false, Result, Error,
            TEXT("Material cache test BGRA8 payload"));
        return Result;
    }

    FWetClothingTextureReadback MakeG8Readback(const uint8 Value)
    {
        FWetClothingTextureReadback Result;
        TArray<uint8> Values{Value};
        FString Error;
        FWetClothingTextureReadbackUtils::TryCreateG8Readback(
            MoveTemp(Values), FIntPoint(1, 1), Result, Error,
            TEXT("Material cache test G8 payload"));
        return Result;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyMaterialColorBakeKeyTest,
    "DWC.Transparency.MaterialBake.KeyIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyMaterialColorBakeKeyTest::RunTest(const FString& Parameters)
{
    FDWCTransparencyMaterialColorBakeKey A;
    InitializeValidKey(A, TEXT("ABC"));

    FDWCTransparencyMaterialColorBakeKey B = A;
    TestTrue(TEXT("An exact source dependency produces the same cache key."), A == B);
    TestEqual(TEXT("Equal keys produce equal hashes."), GetTypeHash(A), GetTypeHash(B));
    TestTrue(TEXT("A complete key is valid."), A.IsValid());

    B.SourceUVChannel = 2;
    TestFalse(TEXT("Source UV participates in cache identity."), A == B);
    B = A;
    B.SourceBakeResolution = 4096;
    TestFalse(TEXT("Source bake resolution participates in cache identity."), A == B);
    B = A;
    B.CacheIdentity = TEXT("DEF");
    TestFalse(TEXT("Material state participates in cache identity."), A == B);
    B = A;
    B.SourceMeshContentSignature = TEXT("ChangedMeshContent");
    TestFalse(TEXT("Source mesh content participates in cache identity."), A == B);
    B = A;
    B.EffectiveMaterialSignature = TEXT("ChangedMaterialState");
    TestFalse(TEXT("Effective material parameters participate in cache identity."), A == B);
    B = A;
    B.PlacementSignature = TEXT("ChangedPlacement");
    TestFalse(TEXT("Type 2/3 source placement participates in cache identity."), A == B);
    B = A;
    ++B.IdentityVersion;
    TestFalse(TEXT("Cache schema changes invalidate old resident entries."), A == B);
    B = A;
    B.OwnerAssetPath = FSoftObjectPath(TEXT("/Game/Test/WCA_B.WCA_B"));
    TestFalse(TEXT("Different WCA editor sessions do not share material color cache entries."), A == B);
    TestNotEqual(TEXT("WCA ownership participates in the cache hash."), GetTypeHash(A), GetTypeHash(B));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyMaterialColorConstantPayloadTest,
    "DWC.Transparency.MaterialBake.ConstantPayload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyMaterialColorConstantPayloadTest::RunTest(const FString& Parameters)
{
    FDWCTransparencyMaterialColorBakeResult Result;
    InitializeValidKey(Result.Key, TEXT("Constant"));
    Result.Key.MaterialSlotIndex = 2;
    Result.Key.SourceUVChannel = 0;
    Result.Key.SourceBakeResolution = 4096;

    TArray<FColor> Pixels{FColor(32, 64, 96, 255)};
    FString Error;
    TestTrue(
        TEXT("A uniform 1x1 engine bake is accepted as a constant payload."),
        Result.InitializePayload(
            EDWCTransparencyMaterialColorPayloadKind::ConstantColor,
            FIntPoint(4096, 4096), FIntPoint(1, 1), MoveTemp(Pixels), false, Error));
    TestTrue(TEXT("The constant payload is valid."), Result.IsValid());
    TestEqual(
        TEXT("Source bake resolution remains the requested resolution."),
        Result.SourceBakeResolution,
        FIntPoint(4096, 4096));
    TestEqual(TEXT("Physical resolution remains compact."), Result.PhysicalResolution, FIntPoint(1, 1));
    TestTrue(
        TEXT("The compact payload retains one texel instead of a logical 4K image."),
        Result.AllocatedBytes >= sizeof(FColor) && Result.AllocatedBytes < 1024);

    const FLinearColor A = Result.Sample(FVector2D(0.0, 0.0));
    const FLinearColor B = Result.Sample(FVector2D(0.73, 0.41));
    TestTrue(TEXT("A constant payload ignores UV position."), A.Equals(B, KINDA_SMALL_NUMBER));
    constexpr float ByteTolerance = 0.5f / 255.0f;
    TestTrue(TEXT("The constant payload preserves red."),
        FMath::IsNearlyEqual(A.R, 32.0f / 255.0f, ByteTolerance));
    TestTrue(TEXT("The constant payload preserves green."),
        FMath::IsNearlyEqual(A.G, 64.0f / 255.0f, ByteTolerance));
    TestTrue(TEXT("The constant payload preserves blue."),
        FMath::IsNearlyEqual(A.B, 96.0f / 255.0f, ByteTolerance));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyMaterialColorPayloadShapeTest,
    "DWC.Transparency.MaterialBake.PayloadShape",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyMaterialColorPayloadShapeTest::RunTest(const FString& Parameters)
{
    FDWCTransparencyMaterialColorBakeResult Result;
    TArray<FColor> Pixels;
    Pixels.Init(FColor::White, 4);
    FString Error;
    TestFalse(
        TEXT("A non-constant payload cannot silently use a smaller physical resolution."),
        Result.InitializePayload(
            EDWCTransparencyMaterialColorPayloadKind::Texture,
            FIntPoint(4, 4), FIntPoint(2, 2), MoveTemp(Pixels), true, Error));
    TestTrue(TEXT("Rejected payloads report a useful reason."), !Error.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyMaterialSurfacePayloadTest,
    "DWC.Transparency.MaterialBake.SurfacePayload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyMaterialSurfacePayloadTest::RunTest(const FString& Parameters)
{
    FDWCTransparencyMaterialColorBakeResult Result;
    InitializeValidKey(Result.Key, TEXT("Surface"));
    Result.Key.MaterialSlotIndex = 1;
    Result.Key.SourceUVChannel = 0;
    Result.Key.SourceBakeResolution = 1024;

    TArray<FColor> BaseColor{FColor(64, 96, 128, 255)};
    FString Error;
    TestTrue(
        TEXT("A surface result accepts the required constant Base Color payload."),
        Result.InitializePayload(
            EDWCTransparencyMaterialColorPayloadKind::ConstantColor,
            FIntPoint(1024, 1024), FIntPoint(1, 1), MoveTemp(BaseColor), false, Error));
    TestTrue(
        TEXT("A surface result accepts flat Normal and scalar Metallic payloads."),
        Result.InitializeSurfacePayloadFromReadbacks(
            EDWCTransparencyMaterialColorPayloadKind::ConstantColor,
            MakeBGRA8Readback(FColor(128, 128, 255, 255)), false,
            EDWCTransparencyMaterialColorPayloadKind::ConstantColor,
            MakeG8Readback(96), true, Error));
    TestTrue(TEXT("The combined material surface payload is complete."), Result.HasCompleteSurfacePayload());
    const FVector3f Normal = Result.SampleTangentNormal(FVector2D(0.3, 0.7));
    TestTrue(TEXT("Flat source normal decodes to tangent +Z."),
        Normal.Equals(FVector3f(0.0f, 0.0f, 1.0f), 0.02f));
    TestTrue(TEXT("Metallic scalar sampling preserves the linear G8 value."),
        FMath::IsNearlyEqual(
            Result.SampleMetallic(FVector2D(0.3, 0.7)), 96.0f / 255.0f, 0.5f / 255.0f));
    TestFalse(TEXT("The normal availability flag distinguishes an explicit flat fallback."),
        Result.bHasBakedNormalProperty);
    TestTrue(TEXT("The metallic availability flag records a material property result."),
        Result.bHasBakedMetallicProperty);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyMaterialSurfacePayloadOwnershipTest,
    "DWC.Transparency.MaterialBake.SurfacePayloadOwnership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyMaterialSurfacePayloadOwnershipTest::RunTest(const FString&)
{
    const uint64 BaselineBytes = GetSharedCacheUsedBytes();
    {
        FDWCTransparencyMaterialColorBakeResult Result;
        InitializeValidKey(Result.Key, TEXT("SurfaceOwnership"));
        Result.Key.SourceBakeResolution = 1024;
        FString Error;
        TArray<FColor> BaseColor{FColor(64, 96, 128, 255)};
        TestTrue(
            TEXT("Base Color creates a broker-owned payload."),
            Result.InitializePayload(
                EDWCTransparencyMaterialColorPayloadKind::ConstantColor,
                FIntPoint(1024, 1024), FIntPoint(1, 1), MoveTemp(BaseColor), false, Error));
        TestTrue(
            TEXT("Normal and Metallic create broker-owned payloads."),
            Result.InitializeSurfacePayloadFromReadbacks(
                EDWCTransparencyMaterialColorPayloadKind::ConstantColor,
                MakeBGRA8Readback(FColor(128, 128, 255, 255)), false,
                EDWCTransparencyMaterialColorPayloadKind::ConstantColor,
                MakeG8Readback(96), true, Error));
        TestEqual(
            TEXT("The material result references exactly the reservations owned by its payloads."),
            GetSharedCacheUsedBytes() - BaselineBytes,
            Result.AllocatedBytes);
        TestEqual(
            TEXT("A uniquely held material result can release all referenced payload bytes."),
            Result.GetImmediatelyReclaimableBytes(),
            Result.AllocatedBytes);
    }
    TestEqual(
        TEXT("Destroying the result releases every payload reservation."),
        GetSharedCacheUsedBytes(),
        BaselineBytes);
    return true;
}

#endif
