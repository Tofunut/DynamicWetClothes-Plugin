//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "SWCAEditorPanel.h"

#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingRenderProfileBakeService.h"
#include "WetClothing/DerivedAssets/Textures/Wrinkle/WetWrinkleBakeService.h"
#include "WetClothing/Foundation/Bake/DWCEditorBakeCoordinator.h"
#include "WetClothing/Foundation/Build/DWCEditorBuildOperationManager.h"
#include "WetClothing/Foundation/Build/DWCEditorExclusiveBuildCoordinator.h"
#include "WetClothing/Foundation/Build/DWCTransparencyBuildTargetResolver.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobScheduler.h"
#include "WetClothing/Foundation/Resources/DWCEditorResourceBroker.h"

bool SWCAEditorPanel::HasPendingVisualBakeTasks(FString* OutSummary) const
{
    if (IsShuttingDown())
    {
        if (OutSummary != nullptr)
        {
            *OutSummary = TEXT("The WCA editor is closing.");
        }
        return false;
    }

    FString PartSummary;
    const bool bHasPendingTasks =
        FWetClothingRenderProfileBakeService::HasPendingVisualBakeTasks(
            WetClothingAsset.Get(),
            &PartSummary);
    if (OutSummary != nullptr)
    {
        *OutSummary = bHasPendingTasks
            ? MoveTemp(PartSummary)
            : TEXT("Render Profile Lookup Texture is up to date.");
    }
    return bHasPendingTasks;
}

bool SWCAEditorPanel::BakeWetVisualAssets(FString& OutSummary, bool* OutHadWarnings)
{
    if (IsShuttingDown())
    {
        OutSummary = TEXT("The WCA editor is closing.");
        return false;
    }
    return FWetClothingRenderProfileBakeService::BakeRenderProfileDataAndUpdateMaterials(
        WetClothingAsset.Get(),
        OutSummary,
        OutHadWarnings);
}

bool SWCAEditorPanel::BakePendingVisualAssets(FString& OutSummary, bool* OutHadWarnings)
{
    if (IsShuttingDown())
    {
        OutSummary = TEXT("The WCA editor is closing.");
        return false;
    }
    if (OutHadWarnings != nullptr)
    {
        *OutHadWarnings = false;
    }

    FString PendingSummary;
    if (!FWetClothingRenderProfileBakeService::HasPendingVisualBakeTasks(
            WetClothingAsset.Get(),
            &PendingSummary))
    {
        OutSummary = TEXT("Render Profile Lookup Texture is up to date.");
        return true;
    }

    return FWetClothingRenderProfileBakeService::BakeRenderProfileDataAndUpdateMaterials(
        WetClothingAsset.Get(),
        OutSummary,
        OutHadWarnings);
}

bool SWCAEditorPanel::BakeAllWrinkleMaps(FString& OutSummary, bool* OutHadWarnings)
{
    if (IsShuttingDown() || !SpatialQueryService.IsValid() || !SurfacePatchProjectionCache.IsValid())
    {
        OutSummary = TEXT("The editor spatial query service is unavailable.");
        return false;
    }
    return FWetWrinkleBakeService::BakeAllWrinkleMaps(
        WetClothingAsset.Get(),
        SpatialQueryService.ToSharedRef(),
        SurfacePatchProjectionCache.ToSharedRef(),
        OutSummary,
        OutHadWarnings);
}

bool SWCAEditorPanel::RequestBakeAllWrinkleMaps(
    TFunction<void(const FDWCEditorBakeBatchResult&)> Completion,
    FString* OutError)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (IsShuttingDown() || Asset == nullptr || !BakeCoordinator.IsValid())
    {
        if (OutError != nullptr)
        {
            *OutError = TEXT("The asynchronous bake service is unavailable.");
        }
        return false;
    }

    TArray<int32> MaterialSlots;
    FWetWrinkleBakeService::CollectBakeMaterialSlots(*Asset, MaterialSlots);
    return BakeCoordinator->RequestWrinkleBake(
        MoveTemp(MaterialSlots),
        true,
        MoveTemp(Completion),
        OutError);
}

bool SWCAEditorPanel::RequestBakeAllTransparencyMaps(
    TFunction<void(const FDWCEditorBakeBatchResult&)> Completion,
    FString* OutError)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (IsShuttingDown() || Asset == nullptr || !BakeCoordinator.IsValid())
    {
        if (OutError != nullptr)
        {
            *OutError = TEXT("The asynchronous bake service is unavailable.");
        }
        return false;
    }

    TArray<FGuid> LayerGuids;
    const FDWCTransparencyBuildTargetSnapshot Targets =
        FDWCTransparencyBuildTargetResolver::Resolve(
            *Asset,
            EDWCEditorValidationAccess::ExactPayload);
    Targets.CollectLayerGuids(EDWCTransparencyBuildRequirement::FullBake, LayerGuids);
    if (LayerGuids.IsEmpty())
    {
        if (OutError != nullptr)
        {
            *OutError = Targets.HasEnabledLayers()
                ? TEXT("No enabled Transparency Target Part requires a full bake.")
                : TEXT("No enabled Transparency Target Parts require runtime output.");
        }
        return false;
    }

    return BakeCoordinator->RequestTransparencyBake(
        MoveTemp(LayerGuids),
        true,
        MoveTemp(Completion),
        OutError);
}

bool SWCAEditorPanel::RequestRebakeAffectedTransparencyMaps(
    TFunction<void(const FDWCEditorBakeBatchResult&)> Completion,
    FString* OutError)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (IsShuttingDown() || Asset == nullptr || !BakeCoordinator.IsValid())
    {
        if (OutError != nullptr)
        {
            *OutError = TEXT("The asynchronous bake service is unavailable.");
        }
        return false;
    }

    const FDWCTransparencyBuildTargetSnapshot Targets =
        FDWCTransparencyBuildTargetResolver::Resolve(
            *Asset,
            EDWCEditorValidationAccess::ExactPayload);
    TArray<int32> MaterialSlots;
    for (const FDWCTransparencyBuildTarget& Target : Targets.Targets)
    {
        if (Target.Requirement == EDWCTransparencyBuildRequirement::AffectedStage4 &&
            Target.IsBuildable())
        {
            MaterialSlots.AddUnique(Target.MaterialSlotIndex);
        }
    }
    if (MaterialSlots.IsEmpty())
    {
        if (OutError != nullptr)
        {
            *OutError = TEXT("No Transparency Stage 4 outputs require an affected wrinkle-only rebake.");
        }
        return false;
    }

    return BakeCoordinator->RequestAffectedTransparencyStage4Rebake(
        MoveTemp(MaterialSlots),
        true,
        MoveTemp(Completion),
        OutError);
}

bool SWCAEditorPanel::IsWrinkleBakeActive() const
{
    return BakeCoordinator.IsValid() && BakeCoordinator->IsWrinkleBakeActive();
}

EDWCEditorTransparencyBakeKind SWCAEditorPanel::GetActiveTransparencyBakeKind() const
{
    return BakeCoordinator.IsValid()
        ? BakeCoordinator->GetActiveTransparencyBakeKind()
        : EDWCEditorTransparencyBakeKind::None;
}

TSet<EDWCEditorBuildAction> SWCAEditorPanel::GetRunningBuildActions() const
{
    return BuildOperationManager.IsValid()
        ? BuildOperationManager->GetRunningActions()
        : TSet<EDWCEditorBuildAction>();
}

bool SWCAEditorPanel::CanStartBuildAction(FString* OutReason) const
{
    if (OutReason != nullptr)
    {
        OutReason->Reset();
    }
    if (IsShuttingDown())
    {
        if (OutReason != nullptr)
        {
            *OutReason = TEXT("The WCA editor is closing.");
        }
        return false;
    }
    if (IsExclusiveBuildActive())
    {
        if (OutReason != nullptr)
        {
            *OutReason = TEXT("An exclusive WCA Build is already in progress.");
        }
        return false;
    }
    if (BuildOperationManager.IsValid() && !BuildOperationManager->GetRunningActions().IsEmpty())
    {
        if (OutReason != nullptr)
        {
            *OutReason = TEXT("A WCA Build action is already in progress.");
        }
        return false;
    }
    if (WorkerJobScheduler.IsValid() &&
        WorkerJobScheduler->HasOutstandingWorkClass(EDWCEditorWorkClass::UserBuild))
    {
        if (OutReason != nullptr)
        {
            *OutReason = TEXT("A WCA Build worker job is already in progress.");
        }
        return false;
    }
    return !ResourceBroker.IsValid() || ResourceBroker->CanAdmitWork(
        ResourceBrokerSessionId,
        EDWCEditorWorkClass::UserBuild,
        FGuid(),
        OutReason);
}

bool SWCAEditorPanel::IsExclusiveBuildActive() const
{
    return ExclusiveBuildCoordinator.IsValid() && ExclusiveBuildCoordinator->IsActive();
}

bool SWCAEditorPanel::RequestExclusiveBuild(
    const FString& DebugName,
    TFunction<void()> Work,
    FString* OutError)
{
    check(IsInGameThread());
    if (OutError != nullptr)
    {
        OutError->Reset();
    }
    if (!Work)
    {
        if (OutError != nullptr)
        {
            *OutError = TEXT("The exclusive Build has no work callback.");
        }
        return false;
    }
    if (!ExclusiveBuildCoordinator.IsValid())
    {
        if (OutError != nullptr)
        {
            *OutError = TEXT("The WCA editor Build resource services are unavailable.");
        }
        return false;
    }
    if (!CanStartBuildAction(OutError))
    {
        return false;
    }

    if (!ExclusiveBuildCoordinator->Request(DebugName, MoveTemp(Work), OutError))
    {
        return false;
    }
    if (OnStatusChanged.IsBound())
    {
        OnStatusChanged.Execute();
    }
    return true;
}

void SWCAEditorPanel::HandleExclusiveBuildBarrierChanged(const bool bActive)
{
    check(IsInGameThread());
    if (IsShuttingDown())
    {
        return;
    }
    SetHostLifecycleBlocker(EDWCEditorHostLifecycleBlocker::ExclusiveBuild, bActive);
    if (OnStatusChanged.IsBound())
    {
        OnStatusChanged.Execute();
    }
}
