//Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "RenderingThread.h"

#include "DataAssets/WetClothingTransparencyData.h"
#include "Engine/Texture2D.h"
#include "WetClothing/Foundation/Preview/Diagnostics/DWCEditorPreviewDiagnostics.h"
#include "WetClothing/Foundation/Preview/Orchestration/DWCEditorPreviewLayerStack.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorRenderUploadQueue.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorTextureWorkspace.h"
#include "WetClothing/Modes/Transparency/AutoMap/DWCTransparencyAutoMapGenerator.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyAlphaTileStore.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyBrushRasterizer.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyRevealColorTileStore.h"
#include "WetClothing/Modes/Transparency/Material/WetTransparencyPreviewMaterialParameters.h"

namespace
{
    constexpr int32 HoverTestIsland = 7;

    int32 WrapIndex(const int32 Value, const int32 Size)
    {
        return (Value % Size + Size) % Size;
    }

    FDWCTransparencyAutoBakeResult BuildHoverTestResult(const FIntPoint Resolution)
    {
        FDWCTransparencyAutoBakeResult Result;
        Result.Resolution = Resolution;
        const int32 PixelCount = Resolution.X * Resolution.Y;
        Result.AutoAlphaBuffer.SetNumUninitialized(PixelCount);
        Result.InnerColorBuffer.SetNumUninitialized(PixelCount);
        Result.OuterCoverageBuffer.Init(255, PixelCount);
        Result.OuterIslandIDBuffer.SetNumUninitialized(PixelCount);
        for (int32 Y = 0; Y < Resolution.Y; ++Y)
        {
            for (int32 X = 0; X < Resolution.X; ++X)
            {
                const int32 Index = Y * Resolution.X + X;
                Result.AutoAlphaBuffer[Index] = static_cast<uint8>(32 + (Index * 29) % 192);
                Result.InnerColorBuffer[Index] = FColor(
                    static_cast<uint8>(24 + (X * 11 + Y * 3) % 192),
                    static_cast<uint8>(32 + (X * 5 + Y * 13) % 176),
                    static_cast<uint8>(48 + (X * 7 + Y * 9) % 160),
                    255);
                const int32 IslandID = X < Resolution.X - 3 ? HoverTestIsland : HoverTestIsland + 1;
                Result.OuterIslandIDBuffer[Index] =
                    FDWCTransparencyAutoBakeResult::EncodeOuterIslandID(IslandID);
            }
        }
        return Result;
    }

    bool IsTestIslandPixel(const FDWCTransparencyAutoBakeResult& Result, const int32 PixelIndex)
    {
        return Result.OuterIslandIDBuffer.IsValidIndex(PixelIndex) &&
            FDWCTransparencyAutoBakeResult::MatchesOuterIslandID(
                Result.OuterIslandIDBuffer[PixelIndex],
                HoverTestIsland);
    }

    float ComputeHoverWeight(
        const FDWCTransparencyBrushSample& Sample,
        const float Falloff,
        const FIntPoint Resolution,
        const int32 X,
        const int32 Y,
        const bool bWrap)
    {
        FVector2D Delta(
            (X + 0.5) / static_cast<double>(Resolution.X) - Sample.PositionUV.X,
            (Y + 0.5) / static_cast<double>(Resolution.Y) - Sample.PositionUV.Y);
        if (bWrap)
        {
            Delta.X -= FMath::RoundToDouble(Delta.X);
            Delta.Y -= FMath::RoundToDouble(Delta.Y);
        }
        const float Distance = static_cast<float>(Delta.Size()) / FMath::Max(Sample.RadiusUV, 0.00001f);
        if (Distance > 1.0f)
        {
            return 0.0f;
        }
        const float ClampedFalloff = FMath::Clamp(Falloff, 0.0f, 1.0f);
        const float InnerRadius = 1.0f - ClampedFalloff;
        const float RadialWeight = ClampedFalloff <= 0.00001f || Distance <= InnerRadius
            ? 1.0f
            : 1.0f - FMath::SmoothStep(InnerRadius, 1.0f, Distance);
        return FMath::Clamp(RadialWeight * FMath::Max(Sample.Strength, 0.0f), 0.0f, 1.0f);
    }

    void GatherStrokeTiles(
        const FDWCTransparencyBrushSample& Sample,
        const FIntPoint Resolution,
        const EDWCTransparencyUVAddressMode AddressMode,
        TArray<FIntPoint>& OutTiles)
    {
        TArray<FIntRect> Regions;
        FDWCTransparencyBrushRasterizer::BuildSampleRegions(Sample, Resolution, AddressMode, Regions);
        FDWCEditorDirtyRegionSet RegionSet;
        for (const FIntRect& Region : Regions)
        {
            RegionSet.Add(Region, Resolution, false);
        }
        FDWCTransparencyAlphaTileStore Store;
        Store.Initialize(Resolution);
        Store.GatherTileCoordinates(RegionSet.GetRegions(), false, false, OutTiles);
    }

    bool RasterizeAlpha(
        const FDWCTransparencyAutoBakeResult& Result,
        const FDWCTransparencyBrushStroke& Stroke,
        FDWCTransparencyAlphaTileStore& Store)
    {
        TArray<FIntPoint> OutputTiles;
        GatherStrokeTiles(Stroke.Samples[0], Result.Resolution, Stroke.UVAddressMode, OutputTiles);
        TArray<FDWCTransparencyAlphaTilePayload> Payloads;
        Store.SnapshotTiles(OutputTiles, Payloads);
        if (!FDWCTransparencyBrushRasterizer::RasterizeSamplesToTiles(
                Result, Stroke, Stroke.Samples, OutputTiles, Payloads))
        {
            return false;
        }
        return Store.Commit(Store.GetRevision(), Payloads);
    }

    bool RasterizeRevealColor(
        const FDWCTransparencyAutoBakeResult& Result,
        const FDWCTransparencyRevealColorStroke& Stroke,
        const FLinearColor& BaseColor,
        FDWCTransparencyRevealColorTileStore& Store)
    {
        TArray<FIntPoint> OutputTiles;
        GatherStrokeTiles(Stroke.Samples[0], Result.Resolution, Stroke.UVAddressMode, OutputTiles);
        TArray<FDWCTransparencyRevealColorTilePayload> Payloads;
        Store.SnapshotTiles(OutputTiles, MakeArrayView(Result.InnerColorBuffer), Payloads);
        if (!FDWCTransparencyBrushRasterizer::RasterizeRevealColorSamplesToTiles(
                Result, Stroke, Stroke.Samples, BaseColor, OutputTiles, Payloads))
        {
            return false;
        }
        return Store.Commit(Store.GetRevision(), Payloads, MakeArrayView(Result.InnerColorBuffer));
    }

    float ResolveAlpha(const FDWCTransparencyAutoBakeResult& Result,
        const FDWCTransparencyAlphaTileStore& Store, const int32 PixelIndex)
    {
        return FDWCTransparencyBrushRasterizer::ResolveEditedAlpha(Result, Store, PixelIndex);
    }

    float ComputeSmoothAlpha(
        const FDWCTransparencyAutoBakeResult& Result,
        const FDWCTransparencyAlphaTileStore& Store,
        const int32 X,
        const int32 Y,
        const bool bWrap)
    {
        float Sum = 0.0f;
        int32 Count = 0;
        for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
        {
            for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
            {
                int32 SampleX = X + OffsetX;
                int32 SampleY = Y + OffsetY;
                if (bWrap)
                {
                    SampleX = WrapIndex(SampleX, Result.Resolution.X);
                    SampleY = WrapIndex(SampleY, Result.Resolution.Y);
                }
                else
                {
                    SampleX = FMath::Clamp(SampleX, 0, Result.Resolution.X - 1);
                    SampleY = FMath::Clamp(SampleY, 0, Result.Resolution.Y - 1);
                }
                const int32 SampleIndex = SampleY * Result.Resolution.X + SampleX;
                if (IsTestIslandPixel(Result, SampleIndex))
                {
                    Sum += ResolveAlpha(Result, Store, SampleIndex);
                    ++Count;
                }
            }
        }
        const int32 PixelIndex = Y * Result.Resolution.X + X;
        return Count > 0 ? Sum / Count : ResolveAlpha(Result, Store, PixelIndex);
    }

    uint64 FindCounter(
        const TArray<FDWCEditorPreviewOperationCounter>& Counters,
        const TCHAR* Name,
        const bool bBytes)
    {
        const FDWCEditorPreviewOperationCounter* Counter = Counters.FindByPredicate(
            [Name](const FDWCEditorPreviewOperationCounter& Candidate)
            {
                return Candidate.Name == Name;
            });
        return Counter != nullptr ? (bBytes ? Counter->Bytes : Counter->Count) : MAX_uint64;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyMaterialHoverAlphaParityTest,
    "DWC.Editor.Transparency.HoverRegression.AlphaParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyMaterialHoverAlphaParityTest::RunTest(const FString&)
{
    const FIntPoint Resolution(16, 16);
    const FDWCTransparencyAutoBakeResult Result = BuildHoverTestResult(Resolution);
    const TArray<EDWCTransparencyBrushMode> Modes = {
        EDWCTransparencyBrushMode::Apply,
        EDWCTransparencyBrushMode::SetValue,
        EDWCTransparencyBrushMode::Erase,
        EDWCTransparencyBrushMode::ResetToAuto,
        EDWCTransparencyBrushMode::Smooth};

    for (const EDWCTransparencyBrushMode Mode : Modes)
    {
        FDWCTransparencyAlphaTileStore Store;
        Store.Initialize(Resolution);

        FDWCTransparencyBrushStroke Seed;
        Seed.MaterialSlotIndex = 0;
        Seed.UVAddressMode = EDWCTransparencyUVAddressMode::Clamp;
        Seed.BrushMode = EDWCTransparencyBrushMode::SetValue;
        Seed.Falloff = 0.4f;
        Seed.TargetAlpha = 0.83f;
        FDWCTransparencyBrushSample& SeedSample = Seed.Samples.AddDefaulted_GetRef();
        SeedSample.PositionUV = FVector2D(0.5, 0.5);
        SeedSample.UVIslandID = HoverTestIsland;
        SeedSample.RadiusUV = 0.32f;
        SeedSample.Strength = 0.65f;
        TestTrue(TEXT("The parity fixture seed stroke rasterizes"), RasterizeAlpha(Result, Seed, Store));

        FDWCTransparencyAlphaTileStore Before = Store;
        FDWCTransparencyBrushStroke Stroke;
        Stroke.MaterialSlotIndex = 0;
        Stroke.UVAddressMode = Mode == EDWCTransparencyBrushMode::Apply
            ? EDWCTransparencyUVAddressMode::Wrap
            : EDWCTransparencyUVAddressMode::Clamp;
        Stroke.BrushMode = Mode;
        Stroke.Falloff = 0.6f;
        Stroke.TargetAlpha = 0.27f;
        FDWCTransparencyBrushSample& Sample = Stroke.Samples.AddDefaulted_GetRef();
        Sample.PositionUV = Mode == EDWCTransparencyBrushMode::Apply
            ? FVector2D(0.98, 0.08)
            : FVector2D(0.5, 0.5);
        Sample.UVIslandID = HoverTestIsland;
        Sample.RadiusUV = Mode == EDWCTransparencyBrushMode::Apply ? 0.16f : 0.24f;
        Sample.Strength = 0.72f;
        TestTrue(TEXT("The alpha parity stroke rasterizes"), RasterizeAlpha(Result, Stroke, Store));

        const bool bWrap = Stroke.UVAddressMode == EDWCTransparencyUVAddressMode::Wrap;
        for (int32 Y = 0; Y < Resolution.Y; ++Y)
        {
            for (int32 X = 0; X < Resolution.X; ++X)
            {
                const int32 PixelIndex = Y * Resolution.X + X;
                const float Current = ResolveAlpha(Result, Before, PixelIndex);
                float Weight = IsTestIslandPixel(Result, PixelIndex)
                    ? ComputeHoverWeight(Sample, Stroke.Falloff, Resolution, X, Y, bWrap)
                    : 0.0f;
                float Target = Stroke.TargetAlpha;
                if (Mode == EDWCTransparencyBrushMode::Apply)
                {
                    Target = 1.0f;
                }
                else if (Mode == EDWCTransparencyBrushMode::Erase)
                {
                    Target = 0.0f;
                }
                else if (Mode == EDWCTransparencyBrushMode::ResetToAuto)
                {
                    Target = Result.AutoAlphaBuffer[PixelIndex] / 255.0f;
                }
                else if (Mode == EDWCTransparencyBrushMode::Smooth)
                {
                    Target = ComputeSmoothAlpha(Result, Before, X, Y, bWrap);
                }
                const float Expected = FMath::Lerp(Current, Target, Weight);
                const float Actual = ResolveAlpha(Result, Store, PixelIndex);
                if (!FMath::IsNearlyEqual(Expected, Actual, 3.0f / 255.0f))
                {
                    AddError(FString::Printf(
                        TEXT("Alpha hover parity failed for mode %d at (%d,%d): expected %.5f, actual %.5f"),
                        static_cast<int32>(Mode), X, Y, Expected, Actual));
                    return false;
                }
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyMaterialHoverRevealColorParityTest,
    "DWC.Editor.Transparency.HoverRegression.RevealColorParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyMaterialHoverRevealColorParityTest::RunTest(const FString&)
{
    const FIntPoint Resolution(16, 16);
    const FDWCTransparencyAutoBakeResult Result = BuildHoverTestResult(Resolution);
    const FLinearColor BaseColor(0.08f, 0.16f, 0.24f, 1.0f);
    const TArray<EDWCTransparencyRevealColorBrushMode> Modes = {
        EDWCTransparencyRevealColorBrushMode::Paint,
        EDWCTransparencyRevealColorBrushMode::EraseToBase,
        EDWCTransparencyRevealColorBrushMode::Smooth};

    for (const EDWCTransparencyRevealColorBrushMode Mode : Modes)
    {
        FDWCTransparencyRevealColorTileStore Store;
        Store.Initialize(Resolution);
        FDWCTransparencyRevealColorStroke Seed;
        Seed.MaterialSlotIndex = 0;
        Seed.UVAddressMode = EDWCTransparencyUVAddressMode::Clamp;
        Seed.BrushMode = EDWCTransparencyRevealColorBrushMode::Paint;
        Seed.PaintColor = FLinearColor(0.8f, 0.2f, 0.1f, 1.0f);
        Seed.Falloff = 0.3f;
        FDWCTransparencyBrushSample& SeedSample = Seed.Samples.AddDefaulted_GetRef();
        SeedSample.PositionUV = FVector2D(0.5, 0.5);
        SeedSample.UVIslandID = HoverTestIsland;
        SeedSample.RadiusUV = 0.3f;
        SeedSample.Strength = 0.7f;
        TestTrue(TEXT("The reveal parity seed stroke rasterizes"),
            RasterizeRevealColor(Result, Seed, BaseColor, Store));

        FDWCTransparencyRevealColorTileStore Before = Store;
        FDWCTransparencyRevealColorStroke Stroke;
        Stroke.MaterialSlotIndex = 0;
        Stroke.UVAddressMode = EDWCTransparencyUVAddressMode::Clamp;
        Stroke.BrushMode = Mode;
        Stroke.PaintColor = FLinearColor(0.15f, 0.75f, 0.35f, 1.0f);
        Stroke.Falloff = 0.55f;
        FDWCTransparencyBrushSample& Sample = Stroke.Samples.AddDefaulted_GetRef();
        Sample.PositionUV = FVector2D(0.5, 0.5);
        Sample.UVIslandID = HoverTestIsland;
        Sample.RadiusUV = 0.22f;
        Sample.Strength = 0.68f;
        TestTrue(TEXT("The reveal-color parity stroke rasterizes"),
            RasterizeRevealColor(Result, Stroke, BaseColor, Store));

        for (int32 Y = 0; Y < Resolution.Y; ++Y)
        {
            for (int32 X = 0; X < Resolution.X; ++X)
            {
                const int32 PixelIndex = Y * Resolution.X + X;
                const FLinearColor Current(Before.GetColor(PixelIndex, MakeArrayView(Result.InnerColorBuffer)));
                const float Weight = IsTestIslandPixel(Result, PixelIndex)
                    ? ComputeHoverWeight(Sample, Stroke.Falloff, Resolution, X, Y, false)
                    : 0.0f;
                FLinearColor Target = Stroke.PaintColor.CopyWithNewOpacity(1.0f);
                if (Mode == EDWCTransparencyRevealColorBrushMode::EraseToBase)
                {
                    Target = BaseColor.CopyWithNewOpacity(1.0f);
                }
                else if (Mode == EDWCTransparencyRevealColorBrushMode::Smooth)
                {
                    Target = FLinearColor::Black;
                    for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
                    {
                        for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
                        {
                            const int32 NX = FMath::Clamp(X + OffsetX, 0, Resolution.X - 1);
                            const int32 NY = FMath::Clamp(Y + OffsetY, 0, Resolution.Y - 1);
                            const int32 NeighborIndex = NY * Resolution.X + NX;
                            Target += FLinearColor(Before.GetColor(
                                IsTestIslandPixel(Result, NeighborIndex) ? NeighborIndex : PixelIndex,
                                MakeArrayView(Result.InnerColorBuffer)));
                        }
                    }
                    Target /= 9.0f;
                    Target.A = 1.0f;
                }
                const FColor Expected = FMath::Lerp(Current, Target, Weight).ToFColor(true);
                const FColor Actual = Store.GetColor(PixelIndex, MakeArrayView(Result.InnerColorBuffer));
                const int32 MaxChannelError = FMath::Max3(
                    FMath::Abs(static_cast<int32>(Expected.R) - Actual.R),
                    FMath::Abs(static_cast<int32>(Expected.G) - Actual.G),
                    FMath::Abs(static_cast<int32>(Expected.B) - Actual.B));
                if (MaxChannelError > 2)
                {
                    AddError(FString::Printf(
                        TEXT("Reveal-color hover parity failed for mode %d at (%d,%d), max channel error %d"),
                        static_cast<int32>(Mode), X, Y, MaxChannelError));
                    return false;
                }
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyMaterialHoverZeroUploadTest,
    "DWC.Editor.Transparency.HoverRegression.SteadyStateZeroUpload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyMaterialHoverZeroUploadTest::RunTest(const FString&)
{
    const TSharedRef<FDWCEditorRenderUploadQueue> UploadQueue =
        MakeShared<FDWCEditorRenderUploadQueue>();
    FDWCEditorTextureWorkspace Workspace(UploadQueue);
    Workspace.ResetDiagnosticCounters();
    UploadQueue->ResetDiagnosticCounters();

    FDWCEditorPreviewLayerStack Stack;
    Stack.MaterialSlotIndex = 4;
    for (int32 Index = 0; Index < 256; ++Index)
    {
        FDWCEditorPreviewLayer Hover;
        Hover.Kind = EDWCEditorPreviewLayerKind::LiveTransparencyHover;
        Hover.MaterialSlotIndex = Stack.MaterialSlotIndex;
        Hover.AuthoringRevision = Index + 1;
        Hover.AddVector(
            DWCTransparencyPreviewMaterialParameters::HoverState0(),
            FLinearColor(Index / 256.0f, 0.5f, 0.1f, 0.5f));
        Hover.AddVector(
            DWCTransparencyPreviewMaterialParameters::HoverState1(),
            FLinearColor(1.0f, 0.8f, 0.4f, 0.0f));
        Stack.AddOrReplace(MoveTemp(Hover));
        FDWCEditorPreviewParameterSet Parameters;
        Stack.BuildParameterSet(Parameters);
        TestEqual(TEXT("Steady-state hover keeps one semantic layer"), Stack.Layers.Num(), 1);
    }

    TArray<FDWCEditorPreviewOperationCounter> WorkspaceCounters;
    Workspace.AppendDiagnosticOperationCounters(WorkspaceCounters);
    TArray<FDWCEditorPreviewOperationCounter> UploadCounters;
    UploadQueue->AppendDiagnosticOperationCounters(UploadCounters);
    TestEqual(TEXT("Hover parameter replacement performs no preview region commits"),
        FindCounter(WorkspaceCounters, TEXT("Preview region commit requests"), false), 0ull);
    TestEqual(TEXT("Hover parameter replacement submits no render uploads"),
        FindCounter(UploadCounters, TEXT("Render texture region uploads"), false), 0ull);
    TestEqual(TEXT("Hover parameter replacement uploads zero bytes"),
        FindCounter(UploadCounters, TEXT("Render texture region uploads"), true), 0ull);

    Workspace.Reset();
    UploadQueue->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyMaterialHoverAuxiliaryResourceReuseTest,
    "DWC.Editor.Transparency.HoverRegression.AuxiliaryResourceReuse",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyMaterialHoverAuxiliaryResourceReuseTest::RunTest(const FString&)
{
    const TSharedRef<FDWCEditorRenderUploadQueue> UploadQueue =
        MakeShared<FDWCEditorRenderUploadQueue>();
    FDWCEditorTextureWorkspace Workspace(UploadQueue);
    UTexture2D* Owner = NewObject<UTexture2D>(GetTransientPackage());

    FDWCEditorTextureDescriptor Descriptor;
    Descriptor.Size = FIntPoint(16, 16);
    Descriptor.PixelFormat = PF_G8;
    Descriptor.bSRGB = false;
    Descriptor.CompressionSettings = TC_Masks;
    Descriptor.MipGenSettings = TMGS_NoMipmaps;
    Descriptor.Filter = TF_Bilinear;
    Descriptor.AddressX = TA_Clamp;
    Descriptor.AddressY = TA_Clamp;

    FDWCEditorTextureKey Key;
    Key.Owner = FObjectKey(Owner);
    Key.Purpose = EDWCEditorTexturePurpose::TransparencyHoverIslandMask;
    Key.MaterialSlotIndex = 4;
    Key.LayerGuid = FGuid::NewGuid();

    TArray<uint8> Pixels;
    Pixels.Init(255, Descriptor.Size.X * Descriptor.Size.Y);
    FDWCEditorTextureLease WarmLease = Workspace.TransferG8AndAcquireLease(
        Key,
        Descriptor,
        MoveTemp(Pixels),
        EDWCEditorTextureUploadPriority::Interactive);
    TestTrue(TEXT("The hover auxiliary fixture creates its warm resource"), WarmLease.IsValid());
    UploadQueue->Flush();
    FlushRenderingCommands();
    Workspace.ResetDiagnosticCounters();
    UploadQueue->ResetDiagnosticCounters();

    for (int32 Index = 0; Index < 128; ++Index)
    {
        const FDWCEditorTextureHandle Handle = Workspace.Acquire(Key, Descriptor);
        TestTrue(TEXT("The warmed auxiliary texture remains reusable"), Handle == WarmLease.GetHandle());
        FDWCEditorTextureLease ReusedLease = Workspace.AcquireLease(Handle);
        TestTrue(TEXT("Repeated hover can lease the existing auxiliary texture"), ReusedLease.IsValid());
    }

    TArray<FDWCEditorPreviewOperationCounter> WorkspaceCounters;
    Workspace.AppendDiagnosticOperationCounters(WorkspaceCounters);
    TArray<FDWCEditorPreviewOperationCounter> UploadCounters;
    UploadQueue->AppendDiagnosticOperationCounters(UploadCounters);
    TestEqual(TEXT("Auxiliary reuse creates no additional texture"),
        FindCounter(WorkspaceCounters, TEXT("Transient preview texture creates"), false), 0ull);
    TestEqual(TEXT("Auxiliary reuse recreates no texture"),
        FindCounter(WorkspaceCounters, TEXT("Transient preview texture recreates"), false), 0ull);
    TestEqual(TEXT("Auxiliary reuse submits no upload"),
        FindCounter(UploadCounters, TEXT("Render texture region uploads"), false), 0ull);
    TestEqual(TEXT("Auxiliary reuse uploads zero bytes"),
        FindCounter(UploadCounters, TEXT("Render texture region uploads"), true), 0ull);

    WarmLease.Reset();
    Workspace.Reset();
    FlushRenderingCommands();
    Workspace.ProcessRetiredGPUResources();
    UploadQueue->Shutdown();
    return true;
}

#endif
