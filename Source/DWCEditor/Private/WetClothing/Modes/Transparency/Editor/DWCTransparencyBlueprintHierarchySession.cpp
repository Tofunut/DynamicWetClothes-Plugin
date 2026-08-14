//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/Editor/DWCTransparencyBlueprintHierarchySession.h"

#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"

namespace
{
    const FDWCTransparencyBlueprintMeshComponentMetadata* FindHierarchyComponent(
        const FDWCTransparencyBlueprintHierarchyMetadata& Hierarchy,
        const FName ComponentName)
    {
        return Hierarchy.MeshComponents.FindByPredicate(
            [ComponentName](const FDWCTransparencyBlueprintMeshComponentMetadata& Candidate)
            {
                return Candidate.ComponentName == ComponentName;
            });
    }

    bool DoesExpectedMeshMatch(
        const FWetClothingTransparencyBlueprintComponentBinding& Binding,
        const FDWCTransparencyBlueprintMeshComponentMetadata& Component)
    {
        const FSoftObjectPath ExpectedPath = Binding.ExpectedSkeletalMesh.ToSoftObjectPath();
        return ExpectedPath.IsNull() || Component.SkeletalMeshPath == ExpectedPath;
    }

    bool IsTargetMesh(
        const FDWCTransparencyBlueprintMeshComponentMetadata& Component,
        const USkeletalMesh* RuntimeMesh,
        const USkeletalMesh* SourceMesh)
    {
        const FSoftObjectPath RuntimePath(RuntimeMesh);
        const FSoftObjectPath SourcePath(SourceMesh);
        return !Component.SkeletalMeshPath.IsNull() &&
            (Component.SkeletalMeshPath == RuntimePath || Component.SkeletalMeshPath == SourcePath);
    }
}

FDWCTransparencyBlueprintHierarchySession::~FDWCTransparencyBlueprintHierarchySession()
{
    CancelPendingRequest();
}

void FDWCTransparencyBlueprintHierarchySession::Request(
    const FGuid& LayerGuid,
    const TSoftClassPtr<AActor>& BlueprintClass,
    const bool bForceReload)
{
    check(IsInGameThread());
    const FSoftObjectPath ClassPath = BlueprintClass.ToSoftObjectPath();
    if (!LayerGuid.IsValid() || ClassPath.IsNull())
    {
        Reset();
        return;
    }

    if (!bForceReload && Snapshot.Matches(LayerGuid, ClassPath) &&
        Snapshot.State != EDWCTransparencyBlueprintHierarchyState::Unloaded)
    {
        return;
    }

    ++RequestGeneration;
    CancelPendingRequest();
    Snapshot = FDWCTransparencyBlueprintHierarchySnapshot();
    Snapshot.LayerGuid = LayerGuid;
    Snapshot.BlueprintClassPath = ClassPath;
    PublishState(EDWCTransparencyBlueprintHierarchyState::Loading);

    if (BlueprintClass.Get() != nullptr)
    {
        CompleteRequest(RequestGeneration);
        return;
    }

    const TWeakPtr<FDWCTransparencyBlueprintHierarchySession> WeakThis = AsShared();
    const uint64 CapturedGeneration = RequestGeneration;
    PendingLoad = UAssetManager::GetStreamableManager().RequestAsyncLoad(
        ClassPath,
        FStreamableDelegate::CreateLambda([WeakThis, CapturedGeneration]()
        {
            if (const TSharedPtr<FDWCTransparencyBlueprintHierarchySession> Pinned = WeakThis.Pin())
            {
                Pinned->CompleteRequest(CapturedGeneration);
            }
        }));
    if (!PendingLoad.IsValid())
    {
        PublishState(
            EDWCTransparencyBlueprintHierarchyState::Error,
            FString::Printf(TEXT("Could not start loading Source Blueprint '%s'."), *ClassPath.ToString()));
    }
}

void FDWCTransparencyBlueprintHierarchySession::Reset()
{
    check(IsInGameThread());
    if (!PendingLoad.IsValid() &&
        Snapshot.State == EDWCTransparencyBlueprintHierarchyState::Unloaded &&
        !Snapshot.LayerGuid.IsValid() &&
        Snapshot.BlueprintClassPath.IsNull() &&
        Snapshot.Hierarchy.MeshComponents.IsEmpty())
    {
        return;
    }
    ++RequestGeneration;
    CancelPendingRequest();
    Snapshot = FDWCTransparencyBlueprintHierarchySnapshot();
    Snapshot.Revision = ++SnapshotRevision;
    Changed.Broadcast();
}

void FDWCTransparencyBlueprintHierarchySession::CancelPendingRequest()
{
    if (PendingLoad.IsValid())
    {
        PendingLoad->CancelHandle();
        PendingLoad.Reset();
    }
}

void FDWCTransparencyBlueprintHierarchySession::CompleteRequest(const uint64 CapturedGeneration)
{
    check(IsInGameThread());
    if (CapturedGeneration != RequestGeneration ||
        Snapshot.State != EDWCTransparencyBlueprintHierarchyState::Loading)
    {
        return;
    }
    PendingLoad.Reset();

    UClass* LoadedClass = Cast<UClass>(Snapshot.BlueprintClassPath.ResolveObject());
    if (LoadedClass == nullptr || !LoadedClass->IsChildOf(AActor::StaticClass()))
    {
        PublishState(
            EDWCTransparencyBlueprintHierarchyState::Error,
            FString::Printf(
                TEXT("Source Blueprint '%s' could not be loaded."),
                *Snapshot.BlueprintClassPath.ToString()));
        return;
    }
    Snapshot.LoadedClass = LoadedClass;

    FDWCTransparencyBlueprintHierarchyMetadata Hierarchy;
    FString Error;
    if (!FDWCTransparencyProjectionSourceProvider::BuildBlueprintHierarchyMetadata(
            LoadedClass,
            Hierarchy,
            Error))
    {
        PublishState(EDWCTransparencyBlueprintHierarchyState::Error, MoveTemp(Error));
        return;
    }

    Snapshot.Hierarchy = MoveTemp(Hierarchy);
    PublishState(EDWCTransparencyBlueprintHierarchyState::Ready);
}

void FDWCTransparencyBlueprintHierarchySession::PublishState(
    const EDWCTransparencyBlueprintHierarchyState NewState,
    FString Error)
{
    Snapshot.State = NewState;
    Snapshot.Error = MoveTemp(Error);
    Snapshot.Revision = ++SnapshotRevision;
    Changed.Broadcast();
}

FDWCTransparencyType2Readiness FDWCTransparencyBlueprintHierarchySession::EvaluateReadiness(
    const UWetClothingAsset& Asset,
    const FWetClothingTransparencyLayerData& Layer,
    const FDWCTransparencyBlueprintHierarchySnapshot& HierarchySnapshot)
{
    return EvaluateReadiness(
        Asset.GetRuntimeSkeletalMesh(),
        Asset.GetSourceSkeletalMesh(),
        Layer,
        HierarchySnapshot);
}

FDWCTransparencyType2Readiness FDWCTransparencyBlueprintHierarchySession::EvaluateReadiness(
    const USkeletalMesh* RuntimeMesh,
    const USkeletalMesh* SourceMesh,
    const FWetClothingTransparencyLayerData& Layer,
    const FDWCTransparencyBlueprintHierarchySnapshot& HierarchySnapshot)
{
    FDWCTransparencyType2Readiness Result;
    if (Layer.SourceType != EDWCTransparencySourceType::OtherSkeletalMeshComponents)
    {
        Result.DisabledReason = TEXT("The selected Transparency Target Part is not Type 2.");
        return Result;
    }

    const FSoftObjectPath ClassPath = Layer.BlueprintSource.BlueprintClass.ToSoftObjectPath();
    if (ClassPath.IsNull())
    {
        Result.DisabledReason = TEXT("Assign a Source Blueprint.");
        return Result;
    }
    if (!HierarchySnapshot.Matches(Layer.LayerGuid, ClassPath))
    {
        Result.DisabledReason = TEXT("Loading the Source Blueprint Skeletal Mesh hierarchy...");
        return Result;
    }
    if (HierarchySnapshot.State == EDWCTransparencyBlueprintHierarchyState::Loading)
    {
        Result.DisabledReason = TEXT("Loading the Source Blueprint Skeletal Mesh hierarchy...");
        return Result;
    }
    if (HierarchySnapshot.State == EDWCTransparencyBlueprintHierarchyState::Error)
    {
        Result.DisabledReason = HierarchySnapshot.Error.IsEmpty()
            ? TEXT("The Source Blueprint Skeletal Mesh hierarchy could not be loaded.")
            : HierarchySnapshot.Error;
        return Result;
    }
    if (HierarchySnapshot.State != EDWCTransparencyBlueprintHierarchyState::Ready)
    {
        Result.DisabledReason = TEXT("The Source Blueprint Skeletal Mesh hierarchy is not ready.");
        return Result;
    }

    const FWetClothingTransparencyBlueprintSource& Config = Layer.BlueprintSource;
    int32 TargetCandidateCount = 0;
    for (const FDWCTransparencyBlueprintMeshComponentMetadata& Component :
         HierarchySnapshot.Hierarchy.MeshComponents)
    {
        TargetCandidateCount += IsTargetMesh(Component, RuntimeMesh, SourceMesh) ? 1 : 0;
    }
    if (TargetCandidateCount == 0)
    {
        Result.DisabledReason =
            TEXT("The selected Blueprint does not contain the WCA target Skeletal Mesh.");
        return Result;
    }

    if (!Config.TargetComponent.IsBound())
    {
        Result.DisabledReason = TEXT("Select the Blueprint component that contains the target DWC Skeletal Mesh.");
        return Result;
    }

    const FDWCTransparencyBlueprintMeshComponentMetadata* Target = FindHierarchyComponent(
        HierarchySnapshot.Hierarchy,
        Config.TargetComponent.ComponentName);
    if (Target == nullptr)
    {
        Result.DisabledReason = FString::Printf(
            TEXT("Blueprint Target Component '%s' no longer exists."),
            *Config.TargetComponent.ComponentName.ToString());
        return Result;
    }
    if (!DoesExpectedMeshMatch(Config.TargetComponent, *Target))
    {
        Result.DisabledReason = FString::Printf(
            TEXT("Blueprint Target Component '%s' no longer uses the configured Skeletal Mesh."),
            *Config.TargetComponent.ComponentName.ToString());
        return Result;
    }
    if (!IsTargetMesh(*Target, RuntimeMesh, SourceMesh))
    {
        Result.DisabledReason = TEXT("The selected Blueprint Target Component does not use the WCA target Skeletal Mesh.");
        return Result;
    }
    Result.bTargetResolved = true;

    if (Config.SourcePriority.IsEmpty())
    {
        Result.DisabledReason = TEXT("Select at least one Blueprint Skeletal Mesh Component for raycast.");
        return Result;
    }

    TSet<FName> SeenComponents;
    bool bHasRevealSource = false;
    for (const FWetClothingTransparencyBlueprintComponentBinding& Source : Config.SourcePriority)
    {
        if (!Source.IsBound())
        {
            Result.DisabledReason = TEXT("A Blueprint Raycast Source Component is not selected.");
            return Result;
        }
        if (Source.ComponentName == Config.TargetComponent.ComponentName)
        {
            Result.DisabledReason = TEXT("Blueprint Target Component cannot also be a Raycast Source Component.");
            return Result;
        }
        if (SeenComponents.Contains(Source.ComponentName))
        {
            Result.DisabledReason = FString::Printf(
                TEXT("Blueprint Raycast Source Component '%s' is listed more than once."),
                *Source.ComponentName.ToString());
            return Result;
        }
        SeenComponents.Add(Source.ComponentName);

        const FDWCTransparencyBlueprintMeshComponentMetadata* Component = FindHierarchyComponent(
            HierarchySnapshot.Hierarchy,
            Source.ComponentName);
        if (Component == nullptr)
        {
            Result.DisabledReason = FString::Printf(
                TEXT("Blueprint Raycast Source Component '%s' no longer exists."),
                *Source.ComponentName.ToString());
            return Result;
        }
        if (!DoesExpectedMeshMatch(Source, *Component))
        {
            Result.DisabledReason = FString::Printf(
                TEXT("Blueprint Raycast Source Component '%s' no longer uses the configured Skeletal Mesh."),
                *Source.ComponentName.ToString());
            return Result;
        }
        bHasRevealSource |= Source.Role == EDWCTransparencyBlueprintSourceRole::RevealSource;
    }
    if (!bHasRevealSource)
    {
        Result.DisabledReason = TEXT("Select at least one Blueprint Raycast Source that provides reveal color.");
        return Result;
    }

    Result.bReady = true;
    Result.DisabledReason.Reset();
    return Result;
}

FDWCTransparencyType2BindingReconcileResult
FDWCTransparencyBlueprintHierarchySession::ReconcileBindings(
    const USkeletalMesh* RuntimeMesh,
    const USkeletalMesh* SourceMesh,
    const FWetClothingTransparencyLayerData& Layer,
    const FDWCTransparencyBlueprintHierarchySnapshot& HierarchySnapshot,
    FWetClothingTransparencyBlueprintSource& InOutSource)
{
    FDWCTransparencyType2BindingReconcileResult Result;
    const FSoftObjectPath ClassPath = Layer.BlueprintSource.BlueprintClass.ToSoftObjectPath();
    if (!HierarchySnapshot.IsReadyFor(Layer.LayerGuid, ClassPath))
    {
        Result.Status = TEXT("The Source Blueprint Skeletal Mesh hierarchy is not ready.");
        return Result;
    }

    const FDWCTransparencyBlueprintHierarchyMetadata& Hierarchy = HierarchySnapshot.Hierarchy;
    const FDWCTransparencyBlueprintMeshComponentMetadata* CurrentTarget =
        FindHierarchyComponent(Hierarchy, InOutSource.TargetComponent.ComponentName);
    const bool bCurrentTargetValid = CurrentTarget != nullptr &&
        DoesExpectedMeshMatch(InOutSource.TargetComponent, *CurrentTarget) &&
        IsTargetMesh(*CurrentTarget, RuntimeMesh, SourceMesh);

    if (!bCurrentTargetValid)
    {
        TArray<const FDWCTransparencyBlueprintMeshComponentMetadata*> Candidates;
        for (const FDWCTransparencyBlueprintMeshComponentMetadata& Component : Hierarchy.MeshComponents)
        {
            if (IsTargetMesh(Component, RuntimeMesh, SourceMesh))
            {
                Candidates.Add(&Component);
            }
        }

        if (Candidates.Num() == 1)
        {
            const FDWCTransparencyBlueprintMeshComponentMetadata& Candidate = *Candidates[0];
            InOutSource.TargetComponent.ComponentName = Candidate.ComponentName;
            InOutSource.TargetComponent.ExpectedSkeletalMesh =
                TSoftObjectPtr<USkeletalMesh>(Candidate.SkeletalMeshPath);
            Result.bChanged = true;
            CurrentTarget = &Candidate;
        }
        else
        {
            Result.bTargetAmbiguous = Candidates.Num() > 1;
            Result.Status = Candidates.IsEmpty()
                ? TEXT("The selected Blueprint does not contain the WCA target Skeletal Mesh.")
                : TEXT("Select the Blueprint component that contains the target DWC Skeletal Mesh.");
            return Result;
        }
    }
    else if (InOutSource.TargetComponent.ExpectedSkeletalMesh.IsNull())
    {
        InOutSource.TargetComponent.ExpectedSkeletalMesh =
            TSoftObjectPtr<USkeletalMesh>(CurrentTarget->SkeletalMeshPath);
        Result.bChanged = true;
    }

    const FName TargetName = InOutSource.TargetComponent.ComponentName;
    const int32 RemovedTargetSources = InOutSource.SourcePriority.RemoveAll(
        [TargetName](const FWetClothingTransparencyBlueprintComponentBinding& Source)
        {
            return Source.ComponentName == TargetName;
        });
    Result.bChanged |= RemovedTargetSources > 0;

    for (FWetClothingTransparencyBlueprintComponentBinding& Source : InOutSource.SourcePriority)
    {
        if (!Source.IsBound() || !Source.ExpectedSkeletalMesh.IsNull())
        {
            continue;
        }
        if (const FDWCTransparencyBlueprintMeshComponentMetadata* Component =
                FindHierarchyComponent(Hierarchy, Source.ComponentName))
        {
            Source.ExpectedSkeletalMesh =
                TSoftObjectPtr<USkeletalMesh>(Component->SkeletalMeshPath);
            Result.bChanged = true;
        }
    }

    Result.bTargetResolved = true;
    Result.Status = TEXT("Blueprint target binding is current.");
    return Result;
}

void FDWCTransparencyBlueprintHierarchySession::AddReferencedObjects(FReferenceCollector& Collector)
{
    Collector.AddReferencedObject(Snapshot.LoadedClass);
}
