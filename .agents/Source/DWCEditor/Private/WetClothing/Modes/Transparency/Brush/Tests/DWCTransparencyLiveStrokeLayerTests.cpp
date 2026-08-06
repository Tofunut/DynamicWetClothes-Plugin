#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyLiveStrokeLayer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyLiveStrokeLayerTest,
    "DWC.Editor.Transparency.Brush.LiveStrokeLayer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyLiveStrokeLayerTest::RunTest(const FString& Parameters)
{
    FDWCTransparencyLiveStrokeLayer Layer;
    const FGuid StrokeGuid = FGuid::NewGuid();
    const FIntPoint Resolution(4096, 4096);
    Layer.Begin(StrokeGuid, Resolution);

    FDWCTransparencyBrushSample Sample;
    Sample.PositionUV = FVector2D(0.5, 0.5);
    Sample.RadiusUV = 0.02f;
    Sample.Strength = 1.0f;
    Layer.RecordSample(Sample, EDWCTransparencyUVAddressMode::Clamp);

    FDWCTransparencyBrushSample WrappedSample = Sample;
    WrappedSample.PositionUV = FVector2D(0.995, 0.5);
    Layer.RecordSample(WrappedSample, EDWCTransparencyUVAddressMode::Wrap);

    TestTrue(TEXT("The layer tracks its active stroke"), Layer.IsForStroke(StrokeGuid));
    TestEqual(TEXT("Both live samples are retained once"), Layer.GetSampleCount(), 2);
    TestTrue(TEXT("Only touched tiles are allocated"), Layer.GetTileCount() > 0);
    const int32 FullTileCount =
        FMath::DivideAndRoundUp(Resolution.X, FDWCTransparencyLiveStrokeLayer::TileSize) *
        FMath::DivideAndRoundUp(Resolution.Y, FDWCTransparencyLiveStrokeLayer::TileSize);
    TestTrue(
        TEXT("A 4K stroke keeps a sparse tile subset"),
        Layer.GetTileCount() < FullTileCount / 8);
    TestTrue(TEXT("The sample produces a dirty region"), !Layer.GetDirtyRegions().IsEmpty());
    TestTrue(TEXT("Sparse 4K state owns a bounded allocation"), Layer.GetAllocatedBytes() > 0);
    TestTrue(TEXT("Sparse 4K state stays below 512 KiB"), Layer.GetAllocatedBytes() < 512ull * 1024ull);

    Layer.Reset();
    TestFalse(TEXT("Reset releases the active stroke"), Layer.IsActive());
    TestEqual(TEXT("Reset releases tile records"), Layer.GetTileCount(), 0);
    TestEqual(TEXT("Reset releases sparse allocation"), Layer.GetAllocatedBytes(), 0ull);
    return true;
}

#endif
