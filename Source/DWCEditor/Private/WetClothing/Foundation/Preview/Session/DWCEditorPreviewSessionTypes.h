#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Preview/Diagnostics/DWCEditorPreviewDiagnostics.h"
#include "WetClothing/Foundation/Preview/Materials/DWCEditorPreviewMaterialTypes.h"
#include "WetClothing/Foundation/Preview/Orchestration/DWCEditorPreviewLayerStack.h"
#include "WetClothing/Foundation/Preview/Slots/DWCEditorPreviewSlotState.h"

class UMaterialInstanceDynamic;

enum class EDWCEditorPreviewMaterialScope : uint8
{
    None,
    SingleSlot,
    AllWettableSlots
};

using FDWCEditorPreviewMIDInitializer =
    TFunction<void(int32, UMaterialInstanceDynamic&)>;

struct FDWCEditorPreviewSessionConfig
{
    FString DiagnosticLabel;
    EDWCEditorPreviewMaterialFeature FeatureMask = EDWCEditorPreviewMaterialFeature::None;
    uint32 FeatureSchemaVersion = 1;
    int32 SurfaceWaterNormalUVChannelIndex = 0;
    float InitialPreviewWetness = 1.0f;
    bool bObserveRelevantObjectChanges = true;

    FDWCEditorPreviewGraphExtension ExtendGraph;
    FDWCEditorPreviewMIDInitializer InitializeMID;
    FDWCEditorPreviewMemoryCollector CollectMemoryStats;
    FDWCEditorPreviewOperationCollector CollectOperationStats;
    FDWCEditorPreviewDiagnosticResetter ResetDiagnosticCounters;
};

struct FDWCEditorPreviewSessionSlot
{
    FDWCEditorPreviewSlotState Eligibility;
    TWeakObjectPtr<UMaterialInstanceDynamic> PreviewMID;
    uint64 LastMaterialUseSerial = 0;
    bool bActiveInPreviewScope = false;
    bool bMaterialBuildFailed = false;
    FString MaterialBuildError;
    FDWCEditorPreviewParameterSet DesiredLayerParameters;
    FDWCEditorPreviewParameterSet AppliedLayerParameters;
};

struct FDWCEditorPreviewSessionMaterialResult
{
    bool bSucceeded = false;
    bool bCreated = false;
    UMaterialInstanceDynamic* PreviewMID = nullptr;
    UMaterialInterface* FallbackMaterial = nullptr;
    FString Message;
};

DECLARE_MULTICAST_DELEGATE(FDWCEditorPreviewSessionSlotsChanged);
DECLARE_MULTICAST_DELEGATE_TwoParams(
    FDWCEditorPreviewSessionMaterialReady,
    int32,
    UMaterialInstanceDynamic*);
