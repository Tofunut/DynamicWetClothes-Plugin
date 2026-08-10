//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "Misc/AutomationTest.h"
#include "Misc/SecureHash.h"

#include "Engine/Texture2D.h"
#include "UObject/Package.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyFinalWorkingSet.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyAffectedStage4Rebake.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySignatureService.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySourcePayload.h"
#include "WetClothing/Modes/Transparency/Processing/DWCTransparencyComposite.h"
#include "WetClothing/Modes/Transparency/Temp/DWCTransparencyTempAssetStore.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    UTexture2D* MakeArtifactTexture(
        const FIntPoint Resolution,
        const ETextureSourceFormat Format,
        const void* Bytes)
    {
        const FString AssetName = FString::Printf(
            TEXT("T_StageArtifact_%s"),
            *FGuid::NewGuid().ToString(EGuidFormats::Digits));
        UPackage* Package = CreatePackage(*FString::Printf(
            TEXT("/Game/__DWC_Automation__/Generated/WCA_Test/Textures/Transparency/Temp/%s"),
            *AssetName));
        UTexture2D* Texture = NewObject<UTexture2D>(Package, *AssetName);
        Texture->Source.Init(
            Resolution.X,
            Resolution.Y,
            1,
            1,
            Format,
            static_cast<const uint8*>(Bytes));
        return Texture;
    }

    void AddArtifactReference(
        FWetClothingTransparencyLayerData& Layer,
        const EDWCTransparencyTempArtifactKind Kind,
        UTexture2D* Texture,
        const FString& Signature,
        const FIntPoint Resolution)
    {
        FDWCTransparencyTempArtifactReference& Reference =
            Layer.EditorStageCache.Artifacts.AddDefaulted_GetRef();
        Reference.Kind = Kind;
        Reference.Texture = Texture;
        Reference.BuildSignature = Signature;
        Reference.Resolution = Resolution;
    }

    void AddCanonicalArtifacts(
        FWetClothingTransparencyLayerData& Layer,
        const FString& Signature,
        const FIntPoint Resolution,
        const TArray<FColor>& PackedReveal,
        const TArray<uint8>& ValidHit,
        const TArray<uint8>& Coverage,
        const TArray<uint16>& IslandIDs,
        const TArray<uint16>* EncodedSourcePriorities = nullptr,
        const TArray<FFloat16>* HitDistances = nullptr)
    {
        const int32 PixelCount = Resolution.X * Resolution.Y;
        check(PackedReveal.Num() == PixelCount);
        check(ValidHit.Num() == PixelCount);
        check(Coverage.Num() == PixelCount);
        check(IslandIDs.Num() == PixelCount);

        TArray<uint16> DefaultSourcePriorities;
        TArray<FFloat16> DefaultHitDistances;
        if (EncodedSourcePriorities == nullptr)
        {
            DefaultSourcePriorities.SetNumUninitialized(PixelCount);
            for (int32 Index = 0; Index < PixelCount; ++Index)
            {
                DefaultSourcePriorities[Index] = ValidHit[Index] != 0 ? 1 : 0;
            }
            EncodedSourcePriorities = &DefaultSourcePriorities;
        }
        if (HitDistances == nullptr)
        {
            DefaultHitDistances.SetNumUninitialized(PixelCount);
            for (int32 Index = 0; Index < PixelCount; ++Index)
            {
                DefaultHitDistances[Index] = FFloat16(ValidHit[Index] != 0 ? 1.0f : 0.0f);
            }
            HitDistances = &DefaultHitDistances;
        }
        check(EncodedSourcePriorities->Num() == PixelCount);
        check(HitDistances->Num() == PixelCount);

        TArray<FColor> PackedRevealSurface;
        PackedRevealSurface.SetNumUninitialized(PixelCount);
        for (int32 Index = 0; Index < PixelCount; ++Index)
        {
            PackedRevealSurface[Index] = FColor(
                128,
                128,
                0,
                ValidHit[Index] != 0 ? 255 : 0);
        }

        AddArtifactReference(
            Layer,
            EDWCTransparencyTempArtifactKind::BaseRevealColor,
            MakeArtifactTexture(Resolution, TSF_BGRA8, PackedReveal.GetData()),
            Signature,
            Resolution);
        AddArtifactReference(
            Layer,
            EDWCTransparencyTempArtifactKind::BaseRevealSurface,
            MakeArtifactTexture(Resolution, TSF_BGRA8, PackedRevealSurface.GetData()),
            Signature,
            Resolution);
        AddArtifactReference(
            Layer,
            EDWCTransparencyTempArtifactKind::ValidHit,
            MakeArtifactTexture(Resolution, TSF_G8, ValidHit.GetData()),
            Signature,
            Resolution);
        AddArtifactReference(
            Layer,
            EDWCTransparencyTempArtifactKind::OuterCoverage,
            MakeArtifactTexture(Resolution, TSF_G8, Coverage.GetData()),
            Signature,
            Resolution);
        AddArtifactReference(
            Layer,
            EDWCTransparencyTempArtifactKind::OuterIslandID,
            MakeArtifactTexture(Resolution, TSF_G16, IslandIDs.GetData()),
            Signature,
            Resolution);
        AddArtifactReference(
            Layer,
            EDWCTransparencyTempArtifactKind::HitSource,
            MakeArtifactTexture(Resolution, TSF_G16, EncodedSourcePriorities->GetData()),
            Signature,
            Resolution);
        AddArtifactReference(
            Layer,
            EDWCTransparencyTempArtifactKind::HitDistance,
            MakeArtifactTexture(Resolution, TSF_R16F, HitDistances->GetData()),
            Signature,
            Resolution);
    }

    FDWCTransparencySourcePayload MakeCanonicalIdentity(
        const FWetClothingTransparencyLayerData& Layer,
        const FString& Signature,
        const FIntPoint Resolution)
    {
        FDWCTransparencySourcePayload Identity;
        Identity.LayerGuid = Layer.LayerGuid;
        Identity.MaterialSlotIndex = Layer.TargetSurface.OuterMaterialSlotIndex;
        Identity.UVChannelIndex = 2;
        Identity.LODIndex = 0;
        Identity.Resolution = Resolution;
        Identity.BuildSignature = Signature;
        return Identity;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyFinalSignatureContractTest,
    "DWC.Transparency.Pipeline.FinalSignatureContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyFinalSignatureContractTest::RunTest(const FString& Parameters)
{
    FWetClothingTransparencyLayerData Layer;
    Layer.LayerGuid = FGuid::NewGuid();
    Layer.TargetSurface.OuterMaterialSlotIndex = 3;
    FDWCTransparencyBrushStroke& Stroke = Layer.EditableStrokes.AddDefaulted_GetRef();
    Stroke.StrokeGuid = FGuid::NewGuid();
    Stroke.MaterialSlotIndex = 3;
    Stroke.Samples.AddDefaulted();

    FDWCTransparencyFinalSignatureInputs Inputs;
    Inputs.RevealSignature = TEXT("Reveal");
    Inputs.AlphaAuthoringSignature =
        FDWCTransparencySignatureService::BuildAlphaAuthoringSignature(Layer);
    Inputs.WrinkleMaskBuildSignature = TEXT("Wrinkle");
    Inputs.SuppressionSettingsSignature =
        FDWCTransparencySignatureService::BuildSuppressionSettingsSignature(0.15f, 0.05f, 0.6f, 0.4f);
    Inputs.PaddingPixels = 8;
    Inputs.EdgeFeatherPixels = 4.0f;

    const FString Signature = FDWCTransparencySignatureService::BuildFinalSignature(Inputs);
    TestEqual(TEXT("Explicit final signature inputs are deterministic."),
        Signature, FDWCTransparencySignatureService::BuildFinalSignature(Inputs));
    Inputs.EdgeFeatherPixels = 5.0f;
    TestNotEqual(TEXT("Output setting changes invalidate the final signature."),
        Signature, FDWCTransparencySignatureService::BuildFinalSignature(Inputs));
    Inputs.EdgeFeatherPixels = 4.0f;
    Inputs.AlphaAuthoringSignature = TEXT("ChangedAlpha");
    TestNotEqual(TEXT("Alpha authoring changes invalidate the final signature."),
        Signature, FDWCTransparencySignatureService::BuildFinalSignature(Inputs));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyCorrectedRevealArtifactContractTest,
    "DWC.Transparency.Pipeline.CorrectedRevealArtifactContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyCorrectedRevealArtifactContractTest::RunTest(const FString& Parameters)
{
    const FIntPoint Resolution(2, 2);
    FWetClothingTransparencyLayerData Layer;
    Layer.LayerGuid = FGuid::NewGuid();
    Layer.TargetSurface.OuterMaterialSlotIndex = 4;
    FDWCTransparencySourcePayload Source = MakeCanonicalIdentity(
        Layer,
        TEXT("CorrectedRevealStage2"),
        Resolution);

    const FString RevealSignature =
        FDWCTransparencySignatureService::BuildRevealSignature(Source.BuildSignature, Layer);
    const FString CorrectedArtifactSignature = FMD5::HashAnsiString(*FString::Printf(
        TEXT("DWC.Transparency.CorrectedReveal.v2|Reveal=%s"),
        *RevealSignature));
    const TArray<FColor> CorrectedPixels = {
        FColor(10, 20, 30, 40),
        FColor(50, 60, 70, 80),
        FColor(90, 100, 110, 120),
        FColor(130, 140, 150, 160)
    };
    AddArtifactReference(
        Layer,
        EDWCTransparencyTempArtifactKind::CorrectedRevealColor,
        MakeArtifactTexture(Resolution, TSF_BGRA8, CorrectedPixels.GetData()),
        CorrectedArtifactSignature,
        Resolution);

    TArray<FColor> RestoredPixels;
    FString Error;
    TestEqual(
        TEXT("A current corrected reveal checkpoint restores directly."),
        FDWCTransparencyTempAssetStore::RestoreCurrentCorrectedReveal(
            Layer,
            Source,
            RestoredPixels,
            Error),
        EDWCTransparencyCorrectedRevealRestoreResult::Restored);
    TestEqual(TEXT("Corrected reveal RGB and automatic alpha are preserved."),
        RestoredPixels,
        CorrectedPixels);

    Layer.EditorStageCache.Artifacts[0].BuildSignature = RevealSignature;
    TestEqual(
        TEXT("The pre-alpha-contract checkpoint is treated as stale."),
        FDWCTransparencyTempAssetStore::RestoreCurrentCorrectedReveal(
            Layer,
            Source,
            RestoredPixels,
            Error),
        EDWCTransparencyCorrectedRevealRestoreResult::MissingOrStale);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyAlphaWorkingSnapshotContractTest,
    "DWC.Transparency.Pipeline.AlphaWorkingSnapshotContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyAlphaWorkingSnapshotContractTest::RunTest(const FString& Parameters)
{
    FDWCTransparencyAlphaTileStore Store;
    Store.Initialize(FIntPoint(256, 256));
    Store.SetPixel(5, 7, 64, 128);

    FDWCTransparencyAlphaWorkingSnapshot Sparse;
    Sparse.Mode = EDWCTransparencyAlphaSnapshotMode::SparseTiles;
    Sparse.Resolution = Store.GetResolution();
    Sparse.StoreRevision = Store.GetRevision();
    Store.SnapshotModifiedTiles(Sparse.ModifiedTiles);
    TestTrue(TEXT("A sparse snapshot owns a valid immutable tile copy."), Sparse.IsValid());
    TestEqual(TEXT("Only the modified tile is copied."), Sparse.ModifiedTiles.Num(), 1);

    FDWCTransparencyAlphaWorkingSnapshot Replay;
    Replay.Mode = EDWCTransparencyAlphaSnapshotMode::StrokeReplay;
    Replay.Resolution = FIntPoint(256, 256);
    Replay.FallbackStrokes.AddDefaulted();
    Replay.AuthoredStrokeCount = Replay.FallbackStrokes.Num();
    TestTrue(TEXT("A stroke replay snapshot is a valid canonical fallback."), Replay.IsValid());

    Sparse.FallbackStrokes.AddDefaulted();
    TestFalse(TEXT("Sparse tiles and fallback strokes cannot be mixed."), Sparse.IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyFinalCurrentnessContractTest,
    "DWC.Transparency.Pipeline.FinalCurrentnessContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyFinalCurrentnessContractTest::RunTest(const FString& Parameters)
{
    FDWCTransparencyFinalWorkingSet WorkingSet;
    WorkingSet.Identity.MaterialSlotIndex = 5;
    WorkingSet.Identity.Resolution = FIntPoint(1024, 1024);
    WorkingSet.Settings.PaddingPixels = 8;
    WorkingSet.SuppressionSettingsSignature = TEXT("Settings");
    WorkingSet.FinalSignature = TEXT("Final");
    WorkingSet.WrinkleDependency.BuildSignature = TEXT("Wrinkle");
    WorkingSet.WrinkleDependency.BakeGuid = FGuid::NewGuid();

    FWetClothingBakedTransparencyMap Baked;
    Baked.MaterialSlotIndex = 5;
    Baked.TransparencyMap = NewObject<UTexture2D>();
    Baked.Resolution = 1024;
    Baked.PaddingPixels = 8;
    Baked.BakeGuid = FGuid::NewGuid();
    Baked.BuildSignature = TEXT("Final");
    Baked.SourceWrinkleMaskBuildSignature = TEXT("Wrinkle");
    Baked.SourceWrinkleMaskBakeGuid = WorkingSet.WrinkleDependency.BakeGuid;
    Baked.WrinkleSuppressionSettingsSignature = TEXT("Settings");

    TestTrue(TEXT("Manual Color accepts a baked map without a Reveal Surface payload."),
        FDWCTransparencyFinalWorkingSetBuilder::EvaluateCurrentness(&Baked, WorkingSet).IsCurrent());
    WorkingSet.RevealSurfaceSignature = TEXT("ManualRevealSurfaceIsOptional");
    Baked.RevealSurfaceBuildSignature = TEXT("LegacyOptionalRevealSurface");
    TestTrue(TEXT("Manual Color ignores an optional Reveal Surface signature mismatch."),
        FDWCTransparencyFinalWorkingSetBuilder::EvaluateCurrentness(&Baked, WorkingSet).IsCurrent());
    WorkingSet.RevealSurfaceSignature.Reset();
    Baked.RevealSurfaceBuildSignature.Reset();
    Baked.SourceWrinkleMaskBuildSignature = TEXT("ChangedWrinkle");
    TestEqual(TEXT("Wrinkle changes have a precise stale reason."),
        FDWCTransparencyFinalWorkingSetBuilder::EvaluateCurrentness(&Baked, WorkingSet).Reason,
        EDWCTransparencyStaleReason::WrinkleDependencyChanged);
    Baked.SourceWrinkleMaskBuildSignature = TEXT("Wrinkle");
    Baked.RevealSurfaceMap = NewObject<UTexture2D>();
    Baked.bContainsRevealNormalRG = true;
    Baked.bContainsInnerMetallicB = true;
    Baked.bContainsRevealSurfaceCoverageAlpha = true;
    WorkingSet.bRequiresRevealSurface = true;
    WorkingSet.RevealSurfaceSignature = TEXT("RevealSurface");
    Baked.RevealSurfaceBuildSignature = TEXT("PreviousRevealSurface");
    TestEqual(TEXT("Raycast Reveal Surface identity has its own stale reason."),
        FDWCTransparencyFinalWorkingSetBuilder::EvaluateCurrentness(&Baked, WorkingSet).Reason,
        EDWCTransparencyStaleReason::SourceInputsChanged);

    Baked.RevealSurfaceBuildSignature = TEXT("RevealSurface");
    TestTrue(TEXT("Raycast layers accept a complete Reveal Surface payload."),
        FDWCTransparencyFinalWorkingSetBuilder::EvaluateCurrentness(&Baked, WorkingSet).IsCurrent());

    Baked.RevealSurfaceMap = nullptr;
    TestEqual(TEXT("Raycast layers require their Reveal Surface runtime payload."),
        FDWCTransparencyFinalWorkingSetBuilder::EvaluateCurrentness(&Baked, WorkingSet).Reason,
        EDWCTransparencyStaleReason::MissingArtifact);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyAffectedStage4ArtifactRestoreTest,
    "DWC.Transparency.Pipeline.AffectedStage4ArtifactRestore",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyAffectedStage4ArtifactRestoreTest::RunTest(const FString& Parameters)
{
    constexpr int32 PixelCount = 4;
    const FIntPoint Resolution(2, 2);
    const FString Signature(TEXT("Stage2Signature"));
    FWetClothingTransparencyLayerData Layer;
    Layer.LayerGuid = FGuid::NewGuid();
    Layer.TargetSurface.OuterMaterialSlotIndex = 3;

    auto AddArtifact = [&Layer, &Signature, Resolution](
        const EDWCTransparencyTempArtifactKind Kind,
        UTexture2D* Texture)
    {
        FDWCTransparencyTempArtifactReference& Reference =
            Layer.EditorStageCache.Artifacts.AddDefaulted_GetRef();
        Reference.Kind = Kind;
        Reference.Texture = Texture;
        Reference.BuildSignature = Signature;
        Reference.Resolution = Resolution;
    };

    const FColor BasePixels[PixelCount] = {
        FColor(10, 20, 30, 40), FColor(50, 60, 70, 80),
        FColor(90, 100, 110, 120), FColor(130, 140, 150, 160)
    };
    const uint8 ValidPixels[PixelCount] = {255, 0, 255, 0};
    const FColor SurfacePixels[PixelCount] = {
        FColor(128, 128, 8, 255), FColor(128, 128, 0, 0),
        FColor(160, 96, 255, 255), FColor(128, 128, 0, 0)};
    const uint8 CoveragePixels[PixelCount] = {255, 255, 0, 0};
    const uint16 IslandPixels[PixelCount] = {4, 4, MAX_uint16, MAX_uint16};
    const uint16 SourcePriorityPixels[PixelCount] = {1, 0, 3, 0};
    const FFloat16 HitDistancePixels[PixelCount] = {
        FFloat16(1.25f), FFloat16(0.0f), FFloat16(2.5f), FFloat16(0.0f)};

    UTexture2D* Base = NewObject<UTexture2D>();
    Base->Source.Init(2, 2, 1, 1, TSF_BGRA8,
        reinterpret_cast<const uint8*>(BasePixels));
    UTexture2D* Valid = NewObject<UTexture2D>();
    Valid->Source.Init(2, 2, 1, 1, TSF_G8, ValidPixels);
    UTexture2D* Surface = NewObject<UTexture2D>();
    Surface->Source.Init(2, 2, 1, 1, TSF_BGRA8,
        reinterpret_cast<const uint8*>(SurfacePixels));
    UTexture2D* Coverage = NewObject<UTexture2D>();
    Coverage->Source.Init(2, 2, 1, 1, TSF_G8, CoveragePixels);
    UTexture2D* Islands = NewObject<UTexture2D>();
    Islands->Source.Init(2, 2, 1, 1, TSF_G16,
        reinterpret_cast<const uint8*>(IslandPixels));
    UTexture2D* HitSource = NewObject<UTexture2D>();
    HitSource->Source.Init(2, 2, 1, 1, TSF_G16,
        reinterpret_cast<const uint8*>(SourcePriorityPixels));
    UTexture2D* HitDistance = NewObject<UTexture2D>();
    HitDistance->Source.Init(2, 2, 1, 1, TSF_R16F,
        reinterpret_cast<const uint8*>(HitDistancePixels));
    AddArtifact(EDWCTransparencyTempArtifactKind::BaseRevealColor, Base);
    AddArtifact(EDWCTransparencyTempArtifactKind::BaseRevealSurface, Surface);
    AddArtifact(EDWCTransparencyTempArtifactKind::ValidHit, Valid);
    AddArtifact(EDWCTransparencyTempArtifactKind::OuterCoverage, Coverage);
    AddArtifact(EDWCTransparencyTempArtifactKind::OuterIslandID, Islands);
    AddArtifact(EDWCTransparencyTempArtifactKind::HitSource, HitSource);
    AddArtifact(EDWCTransparencyTempArtifactKind::HitDistance, HitDistance);

    FDWCTransparencySourcePayload Identity;
    Identity.LayerGuid = Layer.LayerGuid;
    Identity.MaterialSlotIndex = 3;
    Identity.UVChannelIndex = 2;
    Identity.LODIndex = 0;
    Identity.Resolution = Resolution;
    Identity.BuildSignature = Signature;
    FDWCTransparencySourcePayload Restored;
    FString Error;
    TestTrue(TEXT("Canonical Stage 2 artifacts restore without ray projection."),
        FDWCTransparencyAffectedStage4Rebake::RestoreCanonicalArtifacts(
            Layer, Identity, Restored, Error));
    TestTrue(TEXT("Restore error remains empty."), Error.IsEmpty());
    TestEqual(TEXT("Reveal RGB is preserved."), Restored.InnerColorBuffer[1], FColor(50, 60, 70, 255));
    TestEqual(TEXT("Packed alpha is restored."), Restored.AutoAlphaBuffer[1], static_cast<uint8>(80));
    TestEqual(TEXT("Reveal Surface payload is restored."), Restored.RevealSurfaceBuffer[2], SurfacePixels[2]);
    TestTrue(TEXT("Valid-hit bit is restored."), Restored.ValidHitBuffer[2]);
    TestEqual(TEXT("Coverage is restored."), Restored.OuterCoverageBuffer[0], static_cast<uint8>(255));
    TestEqual(TEXT("Island identity is restored exactly."), Restored.OuterIslandIDBuffer[0], static_cast<uint16>(4));
    TestEqual(TEXT("Hit source priority is decoded from the packed Temp artifact."),
        Restored.SourcePriorityBuffer[2], static_cast<int16>(2));
    TestEqual(TEXT("No hit source remains explicitly unset."),
        Restored.SourcePriorityBuffer[1], static_cast<int16>(INDEX_NONE));
    TestEqual(TEXT("Hit distance is restored from the half-float Temp artifact."),
        Restored.HitDistanceBuffer[2], 2.5f);
    TestEqual(TEXT("No-hit count is derived from covered texels."), Restored.NoHitCount, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyAffectedStage4SignatureEligibilityTest,
    "DWC.Transparency.Pipeline.AffectedStage4.SignatureEligibility",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyAffectedStage4SignatureEligibilityTest::RunTest(const FString& Parameters)
{
    FWetClothingTransparencyLayerData Layer;
    Layer.LayerGuid = FGuid::NewGuid();
    Layer.TargetSurface.OuterMaterialSlotIndex = 7;
    FDWCTransparencyBrushStroke& Stroke = Layer.EditableStrokes.AddDefaulted_GetRef();
    Stroke.StrokeGuid = FGuid::NewGuid();
    Stroke.MaterialSlotIndex = 7;
    Stroke.Samples.AddDefaulted();

    const FString RevealSignature(TEXT("RevealStage3"));
    const FString PreviousWrinkle(TEXT("WrinkleA"));
    const FString CurrentWrinkle(TEXT("WrinkleB"));
    const FString SettingsSignature =
        FDWCTransparencySignatureService::BuildSuppressionSettingsSignature(
            0.15f, 0.05f, 0.6f, 0.4f);
    const FString BakedFinal = FDWCTransparencySignatureService::BuildFinalSignature(
        RevealSignature, Layer, PreviousWrinkle, SettingsSignature, 8, 4.0f);

    TestEqual(
        TEXT("Reconstructing Stage 4 with the previous wrinkle dependency proves that non-wrinkle inputs are unchanged."),
        BakedFinal,
        FDWCTransparencySignatureService::BuildFinalSignature(
            RevealSignature, Layer, PreviousWrinkle, SettingsSignature, 8, 4.0f));
    TestNotEqual(
        TEXT("Replacing only the wrinkle dependency produces the affected Stage 4 signature."),
        BakedFinal,
        FDWCTransparencySignatureService::BuildFinalSignature(
            RevealSignature, Layer, CurrentWrinkle, SettingsSignature, 8, 4.0f));

    Layer.EditableStrokes[0].TargetAlpha = 0.25f;
    TestNotEqual(
        TEXT("Alpha authoring changes cannot be classified as a wrinkle-only affected rebake."),
        BakedFinal,
        FDWCTransparencySignatureService::BuildFinalSignature(
            RevealSignature, Layer, PreviousWrinkle, SettingsSignature, 8, 4.0f));
    Layer.EditableStrokes[0].TargetAlpha = 1.0f;
    TestNotEqual(
        TEXT("Output settings changes cannot be classified as a wrinkle-only affected rebake."),
        BakedFinal,
        FDWCTransparencySignatureService::BuildFinalSignature(
            RevealSignature, Layer, PreviousWrinkle, SettingsSignature, 16, 4.0f));
    TestNotEqual(
        TEXT("Reveal changes cannot be classified as a wrinkle-only affected rebake."),
        BakedFinal,
        FDWCTransparencySignatureService::BuildFinalSignature(
            TEXT("ChangedReveal"), Layer, PreviousWrinkle, SettingsSignature, 8, 4.0f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyAffectedStage4ArtifactValidationTest,
    "DWC.Transparency.Pipeline.AffectedStage4.ArtifactValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyAffectedStage4ArtifactValidationTest::RunTest(const FString& Parameters)
{
    const FIntPoint Resolution(4, 4);
    const int32 PixelCount = Resolution.X * Resolution.Y;
    const FString Signature(TEXT("CanonicalStage2"));
    FWetClothingTransparencyLayerData Layer;
    Layer.LayerGuid = FGuid::NewGuid();
    Layer.TargetSurface.OuterMaterialSlotIndex = 2;
    TArray<FColor> PackedReveal;
    PackedReveal.Init(FColor(20, 40, 60, 80), PixelCount);
    TArray<uint8> ValidHit;
    ValidHit.Init(255, PixelCount);
    TArray<uint8> Coverage;
    Coverage.Init(255, PixelCount);
    TArray<uint16> Islands;
    Islands.Init(3, PixelCount);
    AddCanonicalArtifacts(
        Layer, Signature, Resolution, PackedReveal, ValidHit, Coverage, Islands);
    const FDWCTransparencySourcePayload Identity =
        MakeCanonicalIdentity(Layer, Signature, Resolution);

    FDWCTransparencySourcePayload Restored;
    FString Error;
    FDWCTransparencyTempArtifactReference* CoverageReference =
        Layer.EditorStageCache.Artifacts.FindByPredicate(
            [](const FDWCTransparencyTempArtifactReference& Reference)
            {
                return Reference.Kind == EDWCTransparencyTempArtifactKind::OuterCoverage;
            });
    TestNotNull(TEXT("Coverage artifact fixture exists."), CoverageReference);
    if (CoverageReference == nullptr)
    {
        return false;
    }

    CoverageReference->bObsolete = true;
    TestFalse(
        TEXT("An obsolete canonical artifact is rejected instead of silently using stale Stage 2 data."),
        FDWCTransparencyAffectedStage4Rebake::RestoreCanonicalArtifacts(
            Layer, Identity, Restored, Error));
    TestTrue(TEXT("A failed restore releases the reveal buffer."),
        Restored.InnerColorBuffer.IsEmpty());
    TestTrue(TEXT("A failed restore releases the alpha buffer."),
        Restored.AutoAlphaBuffer.IsEmpty());
    TestTrue(TEXT("A failed restore releases the coverage buffer."),
        Restored.OuterCoverageBuffer.IsEmpty());
    TestTrue(TEXT("A failed restore releases the island buffer."),
        Restored.OuterIslandIDBuffer.IsEmpty());
    TestTrue(TEXT("A failed restore retains only small object metadata."),
        Restored.GetAllocatedBytes() <=
            static_cast<uint64>(sizeof(FDWCTransparencySourcePayload)) + 64ull);

    CoverageReference->bObsolete = false;
    TArray<uint16> WrongFormatPixels;
    WrongFormatPixels.Init(1, PixelCount);
    CoverageReference->Texture = MakeArtifactTexture(
        Resolution, TSF_G16, WrongFormatPixels.GetData());
    TestFalse(
        TEXT("A canonical artifact with the wrong source format is rejected."),
        FDWCTransparencyAffectedStage4Rebake::RestoreCanonicalArtifacts(
            Layer, Identity, Restored, Error));
    TestTrue(TEXT("Artifact format failures identify the invalid contract."),
        Error.Contains(TEXT("unexpected format or resolution")));

    CoverageReference->Texture = MakeArtifactTexture(
        Resolution, TSF_G8, Coverage.GetData());
    FDWCTransparencySourcePayload WrongIdentity = Identity;
    WrongIdentity.BuildSignature = TEXT("DifferentStage2");
    TestFalse(
        TEXT("Artifacts from another Stage 2 signature are rejected."),
        FDWCTransparencyAffectedStage4Rebake::RestoreCanonicalArtifacts(
            Layer, WrongIdentity, Restored, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyAffectedStage4FinalParityTest,
    "DWC.Transparency.Pipeline.AffectedStage4.FinalParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyAffectedStage4FinalParityTest::RunTest(const FString& Parameters)
{
    const FIntPoint Resolution(8, 8);
    const int32 PixelCount = Resolution.X * Resolution.Y;
    const FString Signature(TEXT("ParityStage2"));
    FWetClothingTransparencyLayerData Layer;
    Layer.LayerGuid = FGuid::NewGuid();
    Layer.TargetSurface.OuterMaterialSlotIndex = 6;

    TArray<FColor> PackedReveal;
    TArray<uint8> ValidHit;
    TArray<uint8> Coverage;
    TArray<uint16> Islands;
    PackedReveal.SetNumUninitialized(PixelCount);
    ValidHit.SetNumUninitialized(PixelCount);
    Coverage.SetNumUninitialized(PixelCount);
    Islands.SetNumUninitialized(PixelCount);
    FDWCTransparencySourcePayload Original =
        MakeCanonicalIdentity(Layer, Signature, Resolution);
    Original.InnerColorBuffer.SetNumUninitialized(PixelCount);
    Original.RevealSurfaceBuffer.SetNumUninitialized(PixelCount);
    Original.AutoAlphaBuffer.SetNumUninitialized(PixelCount);
    Original.OuterCoverageBuffer.SetNumUninitialized(PixelCount);
    Original.OuterIslandIDBuffer.SetNumUninitialized(PixelCount);
    Original.ValidHitBuffer.Init(false, PixelCount);
    Original.HitDistanceBuffer.SetNumUninitialized(PixelCount);
    Original.SourcePriorityBuffer.SetNumUninitialized(PixelCount);
    TArray<uint16> EncodedSourcePriorities;
    TArray<FFloat16> HitDistances;
    EncodedSourcePriorities.SetNumUninitialized(PixelCount);
    HitDistances.SetNumUninitialized(PixelCount);
    for (int32 Index = 0; Index < PixelCount; ++Index)
    {
        const uint8 Alpha = static_cast<uint8>((Index * 37) & 0xff);
        const FColor Reveal(
            static_cast<uint8>((Index * 11) & 0xff),
            static_cast<uint8>((Index * 17) & 0xff),
            static_cast<uint8>((Index * 23) & 0xff),
            255);
        PackedReveal[Index] = FColor(Reveal.R, Reveal.G, Reveal.B, Alpha);
        ValidHit[Index] = (Index % 3) != 0 ? 255 : 0;
        Coverage[Index] = Index < PixelCount - 4 ? 255 : 0;
        Islands[Index] = Coverage[Index] != 0 ? static_cast<uint16>(Index % 4) : MAX_uint16;
        Original.InnerColorBuffer[Index] = Reveal;
        Original.RevealSurfaceBuffer[Index] = FColor(
            128,
            128,
            0,
            ValidHit[Index] != 0 ? 255 : 0);
        Original.AutoAlphaBuffer[Index] = Alpha;
        Original.OuterCoverageBuffer[Index] = Coverage[Index];
        Original.OuterIslandIDBuffer[Index] = Islands[Index];
        Original.ValidHitBuffer[Index] = ValidHit[Index] != 0;
        Original.SourcePriorityBuffer[Index] = ValidHit[Index] != 0
            ? static_cast<int16>(Index % 3)
            : INDEX_NONE;
        Original.HitDistanceBuffer[Index] = static_cast<float>(Index) * 0.125f;
        EncodedSourcePriorities[Index] = Original.SourcePriorityBuffer[Index] >= 0
            ? static_cast<uint16>(Original.SourcePriorityBuffer[Index] + 1)
            : 0;
        HitDistances[Index] = FFloat16(Original.HitDistanceBuffer[Index]);
    }
    AddCanonicalArtifacts(
        Layer, Signature, Resolution, PackedReveal, ValidHit, Coverage, Islands,
        &EncodedSourcePriorities, &HitDistances);

    FDWCTransparencySourcePayload Restored;
    FString Error;
    TestTrue(
        TEXT("Affected Stage 4 restores its canonical source."),
        FDWCTransparencyAffectedStage4Rebake::RestoreCanonicalArtifacts(
            Layer, Original, Restored, Error));
    TestEqual(TEXT("Reveal payload is byte-identical after restore."),
        Restored.InnerColorBuffer, Original.InnerColorBuffer);
    TestEqual(TEXT("Alpha payload is byte-identical after restore."),
        Restored.AutoAlphaBuffer, Original.AutoAlphaBuffer);
    TestEqual(TEXT("Reveal Surface payload is byte-identical after restore."),
        Restored.RevealSurfaceBuffer, Original.RevealSurfaceBuffer);
    TestEqual(TEXT("Coverage payload is byte-identical after restore."),
        Restored.OuterCoverageBuffer, Original.OuterCoverageBuffer);
    TestEqual(TEXT("Island payload is byte-identical after restore."),
        Restored.OuterIslandIDBuffer, Original.OuterIslandIDBuffer);
    TestEqual(TEXT("Hit-distance payload is preserved after restore."),
        Restored.HitDistanceBuffer, Original.HitDistanceBuffer);
    TestEqual(TEXT("Hit-source priority payload is preserved after restore."),
        Restored.SourcePriorityBuffer, Original.SourcePriorityBuffer);

    TArray<uint8> ManualPremultiplied;
    TArray<uint8> ManualWeight;
    TArray<uint8> WrinkleSuppression;
    TArray<uint8> EdgeFeather;
    ManualPremultiplied.Init(0, PixelCount);
    ManualWeight.Init(0, PixelCount);
    WrinkleSuppression.SetNumUninitialized(PixelCount);
    EdgeFeather.SetNumUninitialized(PixelCount);
    for (int32 Index = 0; Index < PixelCount; ++Index)
    {
        if ((Index % 7) == 0)
        {
            ManualWeight[Index] = 128;
            ManualPremultiplied[Index] = 96;
        }
        WrinkleSuppression[Index] = static_cast<uint8>((Index * 13) & 0xff);
        EdgeFeather[Index] = static_cast<uint8>(255 - ((Index * 5) & 0x7f));
    }

    auto MakeContext = [&](const FDWCTransparencySourcePayload& Source)
    {
        FDWCTransparencyPixelComposeContext Context;
        Context.SourcePayload = &Source;
        Context.ManualPremultipliedBuffer = ManualPremultiplied;
        Context.ManualWeightBuffer = ManualWeight;
        Context.WrinkleSuppressionBuffer = WrinkleSuppression;
        Context.OuterEdgeFeatherBuffer = EdgeFeather;
        Context.VisualizationMode = EDWCTransparencyVisualizationMode::Final;
        Context.TransparencyStrength = 0.72f;
        Context.WrinkleSuppressionStrength = 0.61f;
        return Context;
    };
    FDWCTransparencyPixelComposeContext FullContext = MakeContext(Original);
    FDWCTransparencyPixelComposeContext AffectedContext = MakeContext(Restored);
    TArray<FColor> FullPixels;
    TArray<FColor> AffectedPixels;
    TestTrue(TEXT("Full Stage 4 source composes."),
        FDWCTransparencyComposite::ComposeVisualizationPixels(FullContext, FullPixels));
    TestTrue(TEXT("Affected Stage 4 source composes."),
        FDWCTransparencyComposite::ComposeVisualizationPixels(AffectedContext, AffectedPixels));
    TestEqual(TEXT("Affected Stage 4 output is pixel-identical to a full rebuild."),
        AffectedPixels, FullPixels);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyAffectedStage4MemoryRegressionTest,
    "DWC.Transparency.Pipeline.AffectedStage4.MemoryRegression",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyAffectedStage4MemoryRegressionTest::RunTest(const FString& Parameters)
{
    const FIntPoint Resolution(128, 128);
    const int32 PixelCount = Resolution.X * Resolution.Y;
    const FString Signature(TEXT("MemoryStage2"));
    FWetClothingTransparencyLayerData Layer;
    Layer.LayerGuid = FGuid::NewGuid();
    Layer.TargetSurface.OuterMaterialSlotIndex = 9;
    TArray<FColor> PackedReveal;
    PackedReveal.Init(FColor(32, 64, 96, 128), PixelCount);
    TArray<uint8> ValidHit;
    ValidHit.Init(255, PixelCount);
    TArray<uint8> Coverage;
    Coverage.Init(255, PixelCount);
    TArray<uint16> Islands;
    Islands.Init(1, PixelCount);
    AddCanonicalArtifacts(
        Layer, Signature, Resolution, PackedReveal, ValidHit, Coverage, Islands);
    const FDWCTransparencySourcePayload Identity =
        MakeCanonicalIdentity(Layer, Signature, Resolution);

    FDWCTransparencySourcePayload Restored;
    FString Error;
    uint64 FirstBytes = 0;
    uint64 PeakBytes = 0;
    for (int32 Iteration = 0; Iteration < 8; ++Iteration)
    {
        TestTrue(
            TEXT("Repeated affected restore succeeds."),
            FDWCTransparencyAffectedStage4Rebake::RestoreCanonicalArtifacts(
                Layer, Identity, Restored, Error));
        const uint64 Bytes = Restored.GetAllocatedBytes();
        PeakBytes = FMath::Max(PeakBytes, Bytes);
        if (Iteration == 0)
        {
            FirstBytes = Bytes;
        }
        TestEqual(
            TEXT("Repeated restore replaces the prior payload instead of accumulating buffers."),
            Bytes,
            FirstBytes);
    }

    // Canonical Stage 2 now retains color, alpha/hit data, and one packed
    // Reveal Surface payload (normal RG, metallic B, coverage A).
    const uint64 PerPixelUpperBound = 24;
    const uint64 FixedAllowance = 64ull * 1024ull;
    TestTrue(
        TEXT("Canonical affected source remains within the packed per-pixel memory contract."),
        PeakBytes <= static_cast<uint64>(PixelCount) * PerPixelUpperBound + FixedAllowance);
    const uint64 Projected4KBytes =
        4096ull * 4096ull * PerPixelUpperBound + FixedAllowance;
    TestTrue(
        TEXT("One restored 4K canonical source remains below the 512 MiB scheduler envelope."),
        Projected4KBytes < 512ull * 1024ull * 1024ull);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyAffectedStage4SequentialLifetimeTest,
    "DWC.Transparency.Pipeline.AffectedStage4.SequentialLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyAffectedStage4SequentialLifetimeTest::RunTest(const FString& Parameters)
{
    const FGuid First = FGuid::NewGuid();
    const FGuid Second = FGuid::NewGuid();
    const FGuid Third = FGuid::NewGuid();
    FDWCTransparencyAffectedRebakeSequence Sequence;
    Sequence.Initialize({First, Second, Third});

    FGuid ActiveLayer;
    TestTrue(TEXT("The first affected layer can begin."), Sequence.TryBeginNext(ActiveLayer));
    TestEqual(TEXT("Affected layers preserve deterministic order."), ActiveLayer, First);
    Sequence.SetActivePayloadBytes(96ull * 1024ull * 1024ull);
    TestFalse(
        TEXT("A second full-resolution payload cannot begin while one is retained."),
        Sequence.TryBeginNext(ActiveLayer));
    TestEqual(TEXT("Only one active payload is accounted."),
        Sequence.GetActivePayloadBytes(), 96ull * 1024ull * 1024ull);

    Sequence.CompleteActive();
    TestEqual(TEXT("Completing a job releases its retained payload accounting."),
        Sequence.GetActivePayloadBytes(), 0ull);
    TestTrue(TEXT("The next layer begins only after release."), Sequence.TryBeginNext(ActiveLayer));
    TestEqual(TEXT("The second layer follows the first."), ActiveLayer, Second);
    Sequence.SetActivePayloadBytes(128ull * 1024ull * 1024ull);
    Sequence.CompleteActive();
    TestTrue(TEXT("The final layer can begin."), Sequence.TryBeginNext(ActiveLayer));
    TestEqual(TEXT("The final layer preserves order."), ActiveLayer, Third);
    Sequence.SetActivePayloadBytes(64ull * 1024ull * 1024ull);
    Sequence.CompleteActive();

    TestTrue(TEXT("The affected sequence completes after every payload is released."),
        Sequence.IsComplete());
    TestEqual(TEXT("The high-water mark is one largest payload, not the sum of the batch."),
        Sequence.GetPeakPayloadBytes(), 128ull * 1024ull * 1024ull);

    Sequence.Initialize({First, Second});
    TestTrue(TEXT("A canceled sequence can own one active payload."),
        Sequence.TryBeginNext(ActiveLayer));
    Sequence.SetActivePayloadBytes(32ull * 1024ull * 1024ull);
    Sequence.DiscardRemaining();
    TestTrue(TEXT("Cancellation releases active accounting and discards pending layers."),
        Sequence.IsComplete());
    TestEqual(TEXT("Cancellation leaves no retained payload bytes."),
        Sequence.GetActivePayloadBytes(), 0ull);
    return true;
}

#endif
