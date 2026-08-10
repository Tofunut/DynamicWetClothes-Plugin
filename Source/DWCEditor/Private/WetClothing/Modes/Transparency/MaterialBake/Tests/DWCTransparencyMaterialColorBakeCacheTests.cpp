//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "Misc/AutomationTest.h"
#include "WetClothing/Modes/Transparency/MaterialBake/DWCTransparencyMaterialColorBakeCache.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    FWetClothingTextureReadback MakeBGRA8Readback(const FColor& Pixel)
    {
        FWetClothingTextureReadback Result;
        Result.Width = 1;
        Result.Height = 1;
        Result.BytesPerPixel = sizeof(FColor);
        Result.bSRGB = false;
        Result.Format = TSF_BGRA8;
        Result.RawData = MakeShared<TArray64<uint8>>();
        Result.RawData->SetNumUninitialized(sizeof(FColor));
        FMemory::Memcpy(Result.RawData->GetData(), &Pixel, sizeof(FColor));
        return Result;
    }

    FWetClothingTextureReadback MakeG8Readback(const uint8 Value)
    {
        FWetClothingTextureReadback Result;
        Result.Width = 1;
        Result.Height = 1;
        Result.BytesPerPixel = 1;
        Result.bSRGB = false;
        Result.Format = TSF_G8;
        Result.RawData = MakeShared<TArray64<uint8>>();
        Result.RawData->Add(Value);
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
    A.OwnerAssetPath = FSoftObjectPath(TEXT("/Game/Test/WCA_A.WCA_A"));
    A.SourceMeshPath = FSoftObjectPath(TEXT("/Game/Test/SK_Source.SK_Source"));
    A.MaterialSlotIndex = 3;
    A.SourceUVChannel = 1;
    A.LogicalResolution = 2048;
    A.MaterialBakeSignature = TEXT("ABC");

    FDWCTransparencyMaterialColorBakeKey B = A;
    TestTrue(TEXT("An exact source dependency produces the same cache key."), A == B);
    TestEqual(TEXT("Equal keys produce equal hashes."), GetTypeHash(A), GetTypeHash(B));
    TestTrue(TEXT("A complete key is valid."), A.IsValid());

    B.SourceUVChannel = 2;
    TestFalse(TEXT("Source UV participates in cache identity."), A == B);
    B = A;
    B.LogicalResolution = 4096;
    TestFalse(TEXT("Resolution participates in cache identity."), A == B);
    B = A;
    B.MaterialBakeSignature = TEXT("DEF");
    TestFalse(TEXT("Material state participates in cache identity."), A == B);
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
    Result.Key.OwnerAssetPath = FSoftObjectPath(TEXT("/Game/Test/WCA_A.WCA_A"));
    Result.Key.SourceMeshPath = FSoftObjectPath(TEXT("/Game/Test/SK_Source.SK_Source"));
    Result.Key.MaterialSlotIndex = 2;
    Result.Key.SourceUVChannel = 0;
    Result.Key.LogicalResolution = 4096;
    Result.Key.MaterialBakeSignature = TEXT("Constant");

    TArray<FColor> Pixels{FColor(32, 64, 96, 255)};
    FString Error;
    TestTrue(
        TEXT("A uniform 1x1 engine bake is accepted as a constant payload."),
        Result.InitializePayload(
            EDWCTransparencyMaterialColorPayloadKind::ConstantColor,
            FIntPoint(4096, 4096), FIntPoint(1, 1), MoveTemp(Pixels), false, Error));
    TestTrue(TEXT("The constant payload is valid."), Result.IsValid());
    TestEqual(TEXT("Logical resolution remains the requested resolution."), Result.LogicalResolution, FIntPoint(4096, 4096));
    TestEqual(TEXT("Physical resolution remains compact."), Result.PhysicalResolution, FIntPoint(1, 1));
    TestTrue(
        TEXT("The compact payload retains one texel instead of a logical 4K image."),
        Result.AllocatedBytes >= sizeof(FColor) && Result.AllocatedBytes < 1024);

    const FLinearColor A = Result.Sample(FVector2D(0.0, 0.0));
    const FLinearColor B = Result.Sample(FVector2D(0.73, 0.41));
    TestTrue(TEXT("A constant payload ignores UV position."), A.Equals(B, KINDA_SMALL_NUMBER));
    TestTrue(TEXT("The constant payload preserves red."), FMath::IsNearlyEqual(A.R, 32.0f / 255.0f));
    TestTrue(TEXT("The constant payload preserves green."), FMath::IsNearlyEqual(A.G, 64.0f / 255.0f));
    TestTrue(TEXT("The constant payload preserves blue."), FMath::IsNearlyEqual(A.B, 96.0f / 255.0f));
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
    Result.Key.OwnerAssetPath = FSoftObjectPath(TEXT("/Game/Test/WCA_A.WCA_A"));
    Result.Key.SourceMeshPath = FSoftObjectPath(TEXT("/Game/Test/SK_Source.SK_Source"));
    Result.Key.MaterialSlotIndex = 1;
    Result.Key.SourceUVChannel = 0;
    Result.Key.LogicalResolution = 1024;
    Result.Key.MaterialBakeSignature = TEXT("Surface");

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
        FMath::IsNearlyEqual(Result.SampleMetallic(FVector2D(0.3, 0.7)), 96.0f / 255.0f));
    TestFalse(TEXT("The normal availability flag distinguishes an explicit flat fallback."),
        Result.bHasBakedNormalProperty);
    TestTrue(TEXT("The metallic availability flag records a material property result."),
        Result.bHasBakedMetallicProperty);
    return true;
}

#endif
