// Copyright 2026 Team Tofunut. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"
#include "WetClothing/Foundation/Diagnostics/DWCEditorAuthoringPayloadDiagnostics.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationSnapshot.h"
#include "WetClothing/WCAEditor/WCAValidationReport.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorAuthoringPayloadSnapshotTest,
    "DWC.Editor.Foundation.Diagnostics.AuthoringPayloadSnapshot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorAuthoringPayloadSnapshotTest::RunTest(const FString&)
{
    UWetClothingAsset* Asset = NewObject<UWetClothingAsset>(GetTransientPackage());
    UTexture2D* Texture = NewObject<UTexture2D>(GetTransientPackage());

    FWetClothingAuthoredMaterialSlot& Slot =
        Asset->Authored.PartData.EditableWetPartData.MaterialSlots.AddDefaulted_GetRef();
    FWetClothingWetPartEntry& Part = Slot.WetPartEntries.AddDefaulted_GetRef();
    Part.DisplayName = TEXT("Diagnostic wet part");
    Part.AssignedUVIslandIDs = {1, 3, 5};

#if WITH_EDITORONLY_DATA
    FDWCEditorUVTopologyDescriptor& Topology =
        Asset->Derived.Inline.OriginalUVTopologyDescriptors.AddDefaulted_GetRef();
    Topology.BuildSignature = TEXT("DiagnosticTopology");
    Topology.IslandCount = 1;
    Topology.TriangleReferenceCount = 4;
#endif

    FWetWrinklePatchPlacement& Patch =
        Asset->Authored.WrinkleData.EditablePatches.AddDefaulted_GetRef();
    Patch.DisplayName = TEXT("Diagnostic patch");
    Patch.WrinkleNormalTexture = Texture;
    FWetProceduralRidgeStroke& Ridge =
        Asset->Authored.WrinkleData.EditableProceduralRidgeStrokes.AddDefaulted_GetRef();
    Ridge.Points.AddDefaulted(2);

    FWetClothingTransparencyLayerData& Layer =
        Asset->Authored.TransparencyData.TransparencyLayers.AddDefaulted_GetRef();
    Layer.LayerGuid = FGuid::NewGuid();
    UDWCTransparencyLayerStrokeHistory* StrokeHistory =
        Asset->EnsureTransparencyLayerStrokeHistory(Layer.LayerGuid);
    TestNotNull(TEXT("Per-layer stroke history is created"), StrokeHistory);
    if (StrokeHistory == nullptr)
    {
        return false;
    }
    FDWCTransparencyBrushStroke& AlphaStroke = StrokeHistory->AlphaStrokes.AddDefaulted_GetRef();
    AlphaStroke.DisplayName = TEXT("Diagnostic alpha stroke");
    for (int32 SampleIndex = 0; SampleIndex < 3; ++SampleIndex)
    {
        AlphaStroke.AddSample(FDWCTransparencyBrushSample());
    }
    FDWCTransparencyRevealColorStroke& RevealStroke =
        StrokeHistory->RevealColorStrokes.AddDefaulted_GetRef();
    for (int32 SampleIndex = 0; SampleIndex < 5; ++SampleIndex)
    {
        RevealStroke.AddSample(FDWCTransparencyBrushSample());
    }
#if WITH_EDITORONLY_DATA
    FDWCTransparencyTempArtifactReference& Artifact =
        Layer.EditorStageCache.Artifacts.AddDefaulted_GetRef();
    Artifact.BuildSignature = TEXT("DiagnosticArtifact");
    Artifact.Texture = Texture;
#endif

    const FDWCEditorAuthoringPayloadSnapshot Snapshot =
        FDWCEditorAuthoringPayloadDiagnostics::CaptureAssetSnapshot(*Asset);
    TestEqual(TEXT("Wet Part entries are counted"), Snapshot.WetPartCount, 1);
#if WITH_EDITORONLY_DATA
    TestEqual(TEXT("Topology records are counted"), Snapshot.OriginalUVTopologyCount, 1);
    TestEqual(TEXT("Topology triangle references are counted"), Snapshot.OriginalUVTriangleReferenceCount, 4);
    TestEqual(TEXT("Stage artifacts are counted"), Snapshot.TransparencyStageArtifactCount, 1);
#endif
    TestEqual(TEXT("Wrinkle patches are counted"), Snapshot.WrinklePatchCount, 1);
    TestTrue(TEXT("Wrinkle source is retained as a soft reference"), Snapshot.UniqueSoftObjectReferenceCount > 0);
    TestEqual(TEXT("Authoring textures do not create hard object references"), Snapshot.UniqueHardObjectReferenceCount, 0);
    TestEqual(TEXT("Ridge points are counted"), Snapshot.RidgePointCount, 2);
    TestEqual(TEXT("Alpha strokes are counted"), Snapshot.TransparencyAlphaStrokeCount, 1);
    TestEqual(TEXT("Reveal strokes are counted"), Snapshot.TransparencyRevealStrokeCount, 1);
    TestEqual(TEXT("Transparency samples are counted"), Snapshot.TransparencySampleCount, 8);
    TestTrue(TEXT("Dynamic payload has a non-zero retained size"), Snapshot.GetDirectPayloadBytes() > 0);
    TestTrue(TEXT("Referenced textures are deduplicated"), Snapshot.UniqueTextureReferenceCount == 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyCompactStrokeMemoryRegressionTest,
    "DWC.Editor.Foundation.Diagnostics.CompactStrokeMemoryRegression",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyCompactStrokeMemoryRegressionTest::RunTest(const FString&)
{
    constexpr int32 SampleCount = 4096;
    FDWCTransparencyBrushStroke LegacyStroke;
    LegacyStroke.Samples.Reserve(SampleCount);
    for (int32 Index = 0; Index < SampleCount; ++Index)
    {
        FDWCTransparencyBrushSample& Sample = LegacyStroke.Samples.AddDefaulted_GetRef();
        Sample.PositionUV = FVector2D(
            static_cast<double>(Index % 64) / 63.0,
            static_cast<double>(Index / 64) / 63.0);
        Sample.UVIslandID = Index % 7;
        Sample.RadiusUV = 0.02f;
        Sample.Strength = 0.75f;
    }

    FDWCTransparencyBrushStroke CompactStroke = LegacyStroke;
    const uint64 LegacyBytes = LegacyStroke.GetSampleAllocatedSize();
    TestTrue(TEXT("The large legacy stroke compacts."), CompactStroke.CompactLegacySamples());
    const uint64 CompactBytes = CompactStroke.GetSampleAllocatedSize();

    TestEqual(TEXT("Compaction preserves every authored sample."),
        CompactStroke.GetSampleCount(), SampleCount);
    TestTrue(TEXT("Compaction releases the legacy allocation."),
        CompactStroke.Samples.IsEmpty() && CompactStroke.Samples.GetAllocatedSize() == 0);
    TestTrue(TEXT("Compact samples retain less memory than legacy samples."),
        CompactBytes < LegacyBytes);
    TestTrue(TEXT("Compact storage retains at most three quarters of the legacy sample bytes."),
        CompactBytes * 4ull <= LegacyBytes * 3ull);

    TArray<FDWCTransparencyBrushSample> DecodedSamples;
    CompactStroke.DecodeSamples(DecodedSamples);
    TestEqual(TEXT("Decoding restores the complete sample sequence."),
        DecodedSamples.Num(), SampleCount);
    TestEqual(TEXT("Decoded island identity is preserved."),
        DecodedSamples.Last().UVIslandID, LegacyStroke.Samples.Last().UVIslandID);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorAuthoringOperationScopeTest,
    "DWC.Editor.Foundation.Diagnostics.AuthoringOperationScope",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorAuthoringOperationScopeTest::RunTest(const FString&)
{
    IConsoleVariable* Enable = IConsoleManager::Get().FindConsoleVariable(
        TEXT("dwc.Editor.AuthoringPayload.Enable"));
    TestNotNull(TEXT("Authoring payload enable CVar is registered"), Enable);
    if (Enable == nullptr)
    {
        return false;
    }

    const int32 PreviousValue = Enable->GetInt();
    Enable->Set(1, ECVF_SetByCode);
    FDWCEditorAuthoringPayloadDiagnostics::Reset();

    UWetClothingAsset* Asset = NewObject<UWetClothingAsset>(GetTransientPackage());
    UTexture2D* Texture = NewObject<UTexture2D>(GetTransientPackage());
    {
        FDWCEditorAuthoringOperationScope Scope(TEXT("DiagnosticOperation"), Asset);
        FDWCEditorAuthoringPayloadDiagnostics::RecordExplicitLoad(Texture, TEXT("AutomationTest"));
        Asset->Authored.WrinkleData.EditablePatches.AddDefaulted();
    }

    const TArray<FDWCEditorAuthoringOperationRecord> Operations =
        FDWCEditorAuthoringPayloadDiagnostics::GetRecentOperations();
    TestEqual(TEXT("One completed operation is retained"), Operations.Num(), 1);
    if (!Operations.IsEmpty())
    {
        TestEqual(TEXT("Operation name is retained"), Operations[0].Name, FString(TEXT("DiagnosticOperation")));
        TestEqual(TEXT("Explicit load belongs to the operation"), Operations[0].ObservedLoadCount, 1);
        TestTrue(TEXT("Payload delta is captured"), Operations[0].PayloadBytesAfter >= Operations[0].PayloadBytesBefore);
    }

    FDWCEditorAuthoringPayloadDiagnostics::Reset();
    Enable->Set(PreviousValue, ECVF_SetByCode);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorOriginalUVTopologyLazyBulkTest,
    "DWC.Editor.Foundation.Diagnostics.OriginalUVTopologyLazyBulk",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorOriginalUVTopologyLazyBulkTest::RunTest(const FString&)
{
    const FProperty* DescriptorProperty = FindFProperty<FProperty>(
        FWCADerivedInlineData::StaticStruct(),
        GET_MEMBER_NAME_CHECKED(FWCADerivedInlineData, OriginalUVTopologyDescriptors));
    const FProperty* LegacyTopologyProperty = FindFProperty<FProperty>(
        FWCADerivedInlineData::StaticStruct(),
        GET_MEMBER_NAME_CHECKED(FWCADerivedInlineData, OriginalUVTopologies));
    TestNotNull(TEXT("The lazy topology descriptor property exists in editor builds."), DescriptorProperty);
    TestNotNull(TEXT("The legacy topology migration property exists in editor builds."), LegacyTopologyProperty);
    if (DescriptorProperty != nullptr)
    {
        TestTrue(TEXT("Topology descriptors are excluded from cooked WCA data."),
            DescriptorProperty->HasAnyPropertyFlags(CPF_EditorOnly));
    }
    if (LegacyTopologyProperty != nullptr)
    {
        TestTrue(TEXT("Legacy topology migration data is excluded from cooked WCA data."),
            LegacyTopologyProperty->HasAnyPropertyFlags(CPF_EditorOnly));
    }

    UWetClothingAsset* Asset = NewObject<UWetClothingAsset>(GetTransientPackage());
    USkeletalMesh* RuntimeMesh = NewObject<USkeletalMesh>(GetTransientPackage());

    TArray<FDWCDataUVLODMetadata> Metadata;
    FDWCDataUVLODMetadata& LODMetadata = Metadata.AddDefaulted_GetRef();
    LODMetadata.LODIndex = 0;

    TArray<FDWCEditorUVTopologyData> Topologies;
    FDWCEditorUVTopologyData& Topology = Topologies.AddDefaulted_GetRef();
    Topology.bIsValid = true;
    Topology.LODIndex = 0;
    Topology.UVChannelIndex = 0;
    Topology.BuildSignature = TEXT("LazyBulkTopology");
    FDWCOriginalUVIslandTopology& Island = Topology.Islands.AddDefaulted_GetRef();
    Island.MaterialSlotIndex = 3;
    Island.IslandID = 17;
    Island.TriangleIndices = {4, 8, 15, 16, 23, 42};
    Island.UVBounds = FBox2D(FVector2D(0.1, 0.2), FVector2D(0.7, 0.9));
    Island.UVArea = 0.35;

    FString ErrorMessage;
    TestTrue(
        TEXT("Initial topology layout commits to editor bulk"),
        Asset->CommitInitialDataUVLayout(
            RuntimeMesh,
            1,
            MoveTemp(Metadata),
            MoveTemp(Topologies),
            &ErrorMessage));
    TestTrue(TEXT("Topology commit reports no error"), ErrorMessage.IsEmpty());
    TestNotNull(
        TEXT("Descriptor remains eagerly available"),
        Asset->FindOriginalUVTopologyDescriptorForLOD(0));
    TestTrue(
        TEXT("Serialized topology bulk is retained"),
        Asset->GetSerializedOriginalUVTopologyBytesForEditor() > 0);
    TestTrue(
        TEXT("Committed topology initially has a decoded resident cache"),
        Asset->GetResidentOriginalUVTopologyBytesForEditor() > 0);

    Asset->ReleaseLoadedOriginalUVTopologiesForEditor();
    TestEqual(
        TEXT("Releasing the decoded topology drops resident bytes"),
        Asset->GetResidentOriginalUVTopologyBytesForEditor(),
        0ull);
    TestNotNull(
        TEXT("Releasing decoded topology preserves metadata"),
        Asset->FindOriginalUVTopologyDescriptorForLOD(0));

    const FWCAEditorValidationSnapshot MetadataSnapshot =
        BuildWCAValidationSnapshot(*Asset, EWCAValidationMode::MetadataOnly);
    TestEqual(
        TEXT("Routine validation retains metadata-only access"),
        MetadataSnapshot.Access,
        EDWCEditorValidationAccess::MetadataOnly);
    TestEqual(
        TEXT("Metadata-only validation does not decode lazy topology bulk"),
        Asset->GetResidentOriginalUVTopologyBytesForEditor(),
        0ull);

    FDWCEditorUVTopologyHandle Handle =
        Asset->AcquireOriginalUVTopologyForLOD(0, &ErrorMessage);
    TestTrue(TEXT("Topology can be lazily decoded again"), Handle.IsValid());
    TestTrue(TEXT("Lazy topology load reports no error"), ErrorMessage.IsEmpty());
    if (Handle.IsValid())
    {
        TestEqual(TEXT("Lazy topology preserves the island count"), Handle->Islands.Num(), 1);
        TestEqual(
            TEXT("Lazy topology preserves triangle references"),
            Handle->Islands[0].TriangleIndices.Num(),
            6);
        TestEqual(
            TEXT("Lazy topology preserves island identity"),
            Handle->Islands[0].IslandID,
            17);
    }
    TestEqual(
        TEXT("An outstanding topology handle prevents pressure reclaim"),
        Asset->GetReclaimableOriginalUVTopologyBytesForEditor(),
        0ull);
    Handle.Reset();
    TestTrue(
        TEXT("The decoded topology becomes reclaimable after its handle is released"),
        Asset->GetReclaimableOriginalUVTopologyBytesForEditor() > 0);
    TestTrue(
        TEXT("Pressure reclaim releases decoded topology bytes"),
        Asset->ReclaimOriginalUVTopologyBytesForEditor() > 0);
    TestEqual(
        TEXT("Pressure reclaim leaves no decoded topology resident"),
        Asset->GetResidentOriginalUVTopologyBytesForEditor(),
        0ull);
    return true;
}

#endif
