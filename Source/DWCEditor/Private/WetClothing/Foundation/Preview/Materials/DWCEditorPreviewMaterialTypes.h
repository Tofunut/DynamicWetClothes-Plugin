// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UMaterial;
class UMaterialInstanceConstant;
class UMaterialInstanceDynamic;
class UMaterialInterface;
struct FDWCSurfaceGraphBuildResult;

enum class EDWCEditorPreviewMaterialFeature : uint8
{
    None = 0,
    Wrinkle = 1 << 0,
    Transparency = 1 << 1,
};
ENUM_CLASS_FLAGS(EDWCEditorPreviewMaterialFeature);

enum class EDWCEditorPreviewMaterialState : uint8
{
    Ready,
    Compiling,
    Failed
};

/**
 * Adds mode-specific nodes after the common DWC surface graph has been built.
 * FeatureMask and FeatureSchemaVersion must change whenever the callback changes
 * the graph contract so cached graphs cannot be reused with incompatible nodes.
 */
using FDWCEditorPreviewGraphExtension =
    TFunction<bool(UMaterial*, const FDWCSurfaceGraphBuildResult&, FString&)>;

struct FDWCEditorPreviewMaterialRequest
{
    UMaterialInterface* SourceMaterial = nullptr;

    /** Stable owner for a slot MID, normally the active WCA or preview session. */
    UObject* SlotOwner = nullptr;
    UObject* MIDOuter = nullptr;
    int32    MaterialSlotIndex = INDEX_NONE;

    int32 DWCDataUVChannelIndex = INDEX_NONE;
    int32 SurfaceWaterNormalUVChannelIndex = 0;

    EDWCEditorPreviewMaterialFeature FeatureMask = EDWCEditorPreviewMaterialFeature::None;
    uint32                           FeatureSchemaVersion = 1;

    /** Increment when the source interface's uniform/static overrides change in place. */
    uint32                          SourceParameterRevision = 0;
    FDWCEditorPreviewGraphExtension ExtendGraph;
};

struct FDWCEditorPreviewMaterialResult
{
    EDWCEditorPreviewMaterialState State = EDWCEditorPreviewMaterialState::Failed;
    bool                           bSucceeded = false;
    bool                           bPending = false;
    bool                           bGraphCacheHit = false;
    bool                           bParentCacheHit = false;
    bool                           bMIDCacheHit = false;

    UMaterial*                 TransientBaseMaterial = nullptr;
    UMaterialInstanceConstant* TransientParent = nullptr;
    UMaterialInstanceDynamic*  PreviewMID = nullptr;
    FString                    Message;
};

struct FDWCEditorPreviewMaterialCacheStats
{
    int32  GraphEntryCount = 0;
    int32  ParentEntryCount = 0;
    int32  SlotMIDEntryCount = 0;
    int32  FailedGraphEntryCount = 0;
    int32  PendingGraphEntryCount = 0;
    int32  FailedParentEntryCount = 0;
    uint64 EstimatedContainerBytes = 0;

    uint64 GraphHitCount = 0;
    uint64 GraphMissCount = 0;
    uint64 ParentHitCount = 0;
    uint64 ParentMissCount = 0;
    uint64 MIDHitCount = 0;
    uint64 MIDMissCount = 0;

    uint64 GraphBuildCount = 0;
    uint64 GraphCompileRequestCount = 0;
    uint64 GraphCompileCompleteCount = 0;
    uint64 GraphCompileFailureCount = 0;
    uint64 ParentBuildCount = 0;
    uint64 MIDBuildCount = 0;
    uint64 SourceInvalidationCount = 0;
    uint64 SlotInvalidationCount = 0;
    uint64 SlotMIDPruneCount = 0;
    uint64 ParentPruneCount = 0;
    uint64 GraphPruneCount = 0;
    uint64 ResetCount = 0;

    double TotalGraphBuildMilliseconds = 0.0;
    double MaxGraphBuildMilliseconds = 0.0;
    double TotalParentBuildMilliseconds = 0.0;
    double MaxParentBuildMilliseconds = 0.0;
    double TotalMIDBuildMilliseconds = 0.0;
    double MaxMIDBuildMilliseconds = 0.0;
};
