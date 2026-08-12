//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class UWetClothingAsset;

enum class EDWCEditorBuildAction : uint8
{
    SaveAsset,
    InitializeDataUV,
    BuildCPURuntimeData,
    BuildGPURuntimeData,
    BakeRenderProfileData,
    GenerateMaterials,
    BakeWrinkleTextures,
    BakeTransparencyTextures,
    RebakeAffectedTransparencyMaps,
    Count
};

enum class EDWCEditorBuildDependencyKind : uint8
{
    HardPrerequisite,
    OrderingOnly,
    OptionalInput
};

enum class EDWCEditorBuildActionState : uint8
{
    Unavailable,
    Blocked,
    UpToDate,
    Required,
    Running,
    Failed
};

enum class EDWCEditorBuildSurfaceMode : uint8
{
    Any,
    WetPart,
    Wrinkle,
    Transparency
};

enum class EDWCEditorBuildPlanPolicy : uint8
{
    AllRequired,
    ValidationSuggested,
    ExplicitActions
};

struct FDWCEditorBuildActionDependency
{
    EDWCEditorBuildAction Action = EDWCEditorBuildAction::SaveAsset;
    EDWCEditorBuildDependencyKind Kind = EDWCEditorBuildDependencyKind::HardPrerequisite;
};

struct FDWCEditorBuildActionDescriptor
{
    EDWCEditorBuildAction Action = EDWCEditorBuildAction::SaveAsset;
    FName StableName;
    FText DisplayName;
    FText Description;
    FName MenuSection;
    FName IconStyleSetName;
    FName IconName;
    int32 StableOrder = 0;
    TArray<FDWCEditorBuildActionDependency> Dependencies;
    bool bShowInRuntimeMenu = true;
    EDWCEditorBuildSurfaceMode VisibleSurfaceMode = EDWCEditorBuildSurfaceMode::Any;
};

struct FDWCEditorBuildActionStatus
{
    EDWCEditorBuildAction Action = EDWCEditorBuildAction::SaveAsset;
    EDWCEditorBuildActionState State = EDWCEditorBuildActionState::Unavailable;
    FString Reason;
    TArray<EDWCEditorBuildAction> BlockingActions;
    TArray<int32> MaterialSlotIndices;
    TArray<FGuid> LayerGuids;

    bool RequiresExecution() const
    {
        return State == EDWCEditorBuildActionState::Required ||
               State == EDWCEditorBuildActionState::Failed;
    }

    bool IsExecutable() const
    {
        return RequiresExecution() && BlockingActions.IsEmpty();
    }
};

/**
 * Value-only input captured on the game thread. Expensive service checks are
 * supplied explicitly rather than being hidden inside status evaluation.
 */
struct FDWCEditorBuildEvaluationInput
{
    bool bHasAsset = false;
    bool bAssetDirty = false;
    bool bHasRuntimeMesh = false;
    bool bHasWettableSlots = false;
    bool bHasValidDataUV = false;
    bool bCPUBackendEnabled = false;
    bool bGPUBackendEnabled = false;
    bool bHasWrinkleContent = false;
    bool bHasTransparencyContent = false;
    bool bWrinkleTargetStateProvided = false;
    bool bTransparencyTargetStateProvided = false;

    EDWCEditorBuildActionState DataUVState = EDWCEditorBuildActionState::Unavailable;
    EDWCEditorBuildActionState CPURuntimeDataState = EDWCEditorBuildActionState::Unavailable;
    EDWCEditorBuildActionState GPURuntimeDataState = EDWCEditorBuildActionState::Unavailable;
    EDWCEditorBuildActionState GPUMapsState = EDWCEditorBuildActionState::Unavailable;
    EDWCEditorBuildActionState WrinkleTexturesState = EDWCEditorBuildActionState::Unavailable;
    EDWCEditorBuildActionState TransparencyTexturesState = EDWCEditorBuildActionState::Unavailable;

    /** Results owned by their existing services and copied into this snapshot. */
    EDWCEditorBuildActionState RenderProfileState = EDWCEditorBuildActionState::Unavailable;
    EDWCEditorBuildActionState GeneratedMaterialsState = EDWCEditorBuildActionState::Unavailable;
    EDWCEditorBuildActionState AffectedTransparencyState = EDWCEditorBuildActionState::Unavailable;
    FString RenderProfileReason;
    FString GeneratedMaterialsReason;
    FString WrinkleTexturesReason;
    FString TransparencyTexturesReason;
    FString AffectedTransparencyReason;

    EDWCEditorBuildSurfaceMode SurfaceMode = EDWCEditorBuildSurfaceMode::Any;
    TSet<EDWCEditorBuildAction> RunningActions;
    TArray<int32> AffectedMaterialSlotIndices;
    TArray<FGuid> AffectedLayerGuids;
    TArray<int32> TransparencyMaterialSlotIndices;
    TArray<FGuid> TransparencyLayerGuids;
    TArray<int32> WrinkleMaterialSlotIndices;
};

struct FDWCEditorBuildStatusSnapshot
{
    TMap<EDWCEditorBuildAction, FDWCEditorBuildActionStatus> Actions;

    const FDWCEditorBuildActionStatus* Find(const EDWCEditorBuildAction Action) const
    {
        return Actions.Find(Action);
    }
};

struct FDWCEditorBuildPlanStep
{
    EDWCEditorBuildAction Action = EDWCEditorBuildAction::SaveAsset;
    TArray<int32> MaterialSlotIndices;
    TArray<FGuid> LayerGuids;
    TArray<FName> SourceDiagnosticCodes;
    bool bExplicitlyRequested = false;
};

struct FDWCEditorBuildPlan
{
    TArray<FDWCEditorBuildPlanStep> Steps;
    TArray<EDWCEditorBuildAction> BlockedActions;
    TArray<FString> Diagnostics;
    TArray<FName> ManualDiagnosticCodes;
    EDWCEditorBuildPlanPolicy Policy = EDWCEditorBuildPlanPolicy::AllRequired;

    bool IsExecutable() const
    {
        return BlockedActions.IsEmpty();
    }
};
