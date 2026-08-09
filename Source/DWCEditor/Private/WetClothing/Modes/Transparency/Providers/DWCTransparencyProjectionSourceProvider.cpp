// Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/Providers/DWCTransparencyProjectionSourceProvider.h"

#include "Components/DWCBakeComponent.h"
#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "PreviewScene.h"

namespace
{
    bool BuildBlueprintSnapshot(
        const TSubclassOf<AActor> BlueprintClass,
        FDWCBakeSnapshot& OutSnapshot,
        FString& OutError)
    {
        OutSnapshot = FDWCBakeSnapshot();
        OutError.Reset();
        if (BlueprintClass == nullptr ||
            BlueprintClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
        {
            OutError = TEXT("Assign a usable Blueprint class before generating the transparency source.");
            return false;
        }

        FPreviewScene PreviewScene(
            FPreviewScene::ConstructionValues()
                .SetCreateDefaultLighting(false)
                .SetCreatePhysicsScene(false)
                .SetTransactional(false));
        UWorld* PreviewWorld = PreviewScene.GetWorld();
        if (PreviewWorld == nullptr)
        {
            OutError = TEXT("Failed to create the transparency source preview world.");
            return false;
        }

        FActorSpawnParameters SpawnParameters;
        SpawnParameters.Name = MakeUniqueObjectName(
            PreviewWorld, BlueprintClass, TEXT("DWC_TransparencySourcePreviewActor"));
        SpawnParameters.ObjectFlags = RF_Transient;
        SpawnParameters.SpawnCollisionHandlingOverride =
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        SpawnParameters.bTemporaryEditorActor = true;

        AActor* PreviewActor = PreviewWorld->SpawnActor<AActor>(
            BlueprintClass, FTransform::Identity, SpawnParameters);
        if (PreviewActor == nullptr)
        {
            OutError = FString::Printf(
                TEXT("Failed to instantiate transparency source Blueprint '%s'."),
                *GetNameSafe(BlueprintClass.Get()));
            return false;
        }

        TArray<UDWCBakeComponent*> BakeComponents;
        PreviewActor->GetComponents<UDWCBakeComponent>(BakeComponents);
        if (BakeComponents.Num() != 1 || BakeComponents[0] == nullptr)
        {
            OutError = FString::Printf(
                TEXT("Blueprint '%s' must contain exactly one DWC Bake Component."),
                *GetNameSafe(BlueprintClass.Get()));
            return false;
        }
        if (!BakeComponents[0]->BuildBakeSnapshot(OutSnapshot))
        {
            OutError = FString::Printf(
                TEXT("Failed to resolve the DWC Bake Component layers in Blueprint '%s'."),
                *GetNameSafe(BlueprintClass.Get()));
            return false;
        }
        return true;
    }

    FName MakeProviderLayerId(const FName BaseId, const int32 SlotIndex, const int32 Priority)
    {
        return FName(*FString::Printf(
            TEXT("DWCTransparency_%s_Slot%d_P%d"), *BaseId.ToString(), SlotIndex, Priority));
    }

    void AddSource(
        const FDWCBakeResolvedLayer& Resolved,
        const int32 SlotIndex,
        const int32 Priority,
        FDWCTransparencyProjectionSourceSet& OutSources)
    {
        if (Resolved.SkeletalMesh == nullptr || !Resolved.Materials.IsValidIndex(SlotIndex) ||
            (Resolved.bCanBeRevealSource && Resolved.Materials[SlotIndex] == nullptr))
        {
            return;
        }

        FDWCTransparencyProjectionSource& Source = OutSources.Sources.AddDefaulted_GetRef();
        Source.Layer = Resolved;
        Source.Layer.LayerId = MakeProviderLayerId(Resolved.LayerId, SlotIndex, Priority);
        Source.Layer.LayerOrder = Priority;
        Source.MaterialSlotIndex = SlotIndex;
        Source.PriorityIndex = Priority;
        Source.MaterialSlotName = Resolved.SkeletalMesh->GetMaterials().IsValidIndex(SlotIndex)
            ? Resolved.SkeletalMesh->GetMaterials()[SlotIndex].MaterialSlotName
            : FName(*FString::Printf(TEXT("Slot_%d"), SlotIndex));
        Source.EffectiveMaterial = Resolved.Materials[SlotIndex];
    }

    FDWCBakeResolvedLayer MakeExternalLayer(
        USkeletalMesh& Mesh,
        const FTransform& Transform,
        const float MaxRevealDistance)
    {
        FDWCBakeResolvedLayer Layer;
        Layer.LayerId = TEXT("ExternalMesh");
        Layer.ComponentDisplayName = Mesh.GetFName();
        Layer.ComponentPath = Mesh.GetPathName();
        Layer.SkeletalMesh = &Mesh;
        Layer.BakeTransform = Transform;
        Layer.bCanBeRevealSource = true;
        Layer.bCanBeWetOuterLayer = false;
        Layer.MaxRevealDistance = MaxRevealDistance;
        for (const FSkeletalMaterial& Material : Mesh.GetMaterials())
        {
            Layer.Materials.Add(Material.MaterialInterface);
        }
        return Layer;
    }
}

bool FDWCTransparencyProjectionSourceProvider::BuildBlueprintSources(
    const UWetClothingAsset& Asset,
    const FWetClothingTransparencyLayerData& Layer,
    FDWCTransparencyProjectionSourceSet& OutSources,
    FString& OutError)
{
    check(IsInGameThread());
    OutSources = FDWCTransparencyProjectionSourceSet();
    OutError.Reset();

    TSubclassOf<AActor> BlueprintClass =
        Asset.Authored.TransparencyData.SourceBlueprintClass.LoadSynchronous();
    FDWCBakeSnapshot Snapshot;
    if (!BuildBlueprintSnapshot(BlueprintClass, Snapshot, OutError))
    {
        return false;
    }

    USkeletalMesh* RuntimeMesh = Asset.GetRuntimeSkeletalMesh();
    USkeletalMesh* SourceMesh = Asset.GetSourceSkeletalMesh();
    const FDWCBakeResolvedLayer* OuterLayer = Snapshot.Layers.FindByPredicate(
        [RuntimeMesh, SourceMesh](const FDWCBakeResolvedLayer& Candidate)
        {
            return Candidate.bCanBeWetOuterLayer &&
                (Candidate.SkeletalMesh == RuntimeMesh || Candidate.SkeletalMesh == SourceMesh);
        });
    if (OuterLayer == nullptr)
    {
        OutError = TEXT("The Blueprint DWC Bake Component has no wet outer layer matching the WCA skeletal mesh.");
        return false;
    }
    OutSources.OuterBakeTransform = OuterLayer->BakeTransform;

    TArray<const FDWCBakeResolvedLayer*> OrderedLayers;
    for (const FDWCBakeResolvedLayer& Candidate : Snapshot.Layers)
    {
        if ((Candidate.bCanBeRevealSource || Candidate.bBlocksReveal) &&
            Candidate.SkeletalMesh != nullptr)
        {
            OrderedLayers.Add(&Candidate);
        }
    }
    OrderedLayers.StableSort([](const FDWCBakeResolvedLayer& A, const FDWCBakeResolvedLayer& B)
    {
        return A.LayerOrder < B.LayerOrder;
    });

    int32 Priority = 0;
    for (const FDWCBakeResolvedLayer* Resolved : OrderedLayers)
    {
        for (int32 SlotIndex = 0; SlotIndex < Resolved->Materials.Num(); ++SlotIndex)
        {
            if (Resolved == OuterLayer && SlotIndex == Layer.TargetSurface.OuterMaterialSlotIndex)
            {
                continue;
            }
            const int32 Before = OutSources.Sources.Num();
            AddSource(*Resolved, SlotIndex, Priority, OutSources);
            Priority += OutSources.Sources.Num() != Before ? 1 : 0;
        }
    }
    if (!OutSources.Sources.ContainsByPredicate(
            [](const FDWCTransparencyProjectionSource& Source)
            {
                return Source.Layer.bCanBeRevealSource;
            }))
    {
        OutError = TEXT("The Blueprint snapshot contains no reveal-source material surfaces with usable materials.");
        return false;
    }
    OutSources.ProviderSignature = Snapshot.BuildSignature;
    return true;
}

bool FDWCTransparencyProjectionSourceProvider::BuildExternalMeshSources(
    const UWetClothingAsset&,
    const FWetClothingTransparencyLayerData& Layer,
    FDWCTransparencyProjectionSourceSet& OutSources,
    FString& OutError)
{
    check(IsInGameThread());
    OutSources = FDWCTransparencyProjectionSourceSet();
    OutError.Reset();
    USkeletalMesh* Mesh = Layer.ExternalMeshSource.SkeletalMesh;
    if (Mesh == nullptr)
    {
        OutError = TEXT("Assign an External Skeletal Mesh before generating the transparency map.");
        return false;
    }

    const FDWCBakeResolvedLayer Resolved = MakeExternalLayer(
        *Mesh, Layer.ExternalMeshSource.BakeTransform, Layer.RaySettings.MaxRayDistance);
    const TArray<FWetClothingTransparencyInnerSlot>& ConfiguredSlots =
        Layer.ExternalMeshSource.SourceSlotPriority;
    int32 Priority = 0;
    if (ConfiguredSlots.IsEmpty())
    {
        for (int32 SlotIndex = 0; SlotIndex < Resolved.Materials.Num(); ++SlotIndex)
        {
            const int32 Before = OutSources.Sources.Num();
            AddSource(Resolved, SlotIndex, Priority, OutSources);
            Priority += OutSources.Sources.Num() != Before ? 1 : 0;
        }
    }
    else
    {
        for (const FWetClothingTransparencyInnerSlot& Slot : ConfiguredSlots)
        {
            FDWCBakeResolvedLayer SlotLayer = Resolved;
            SlotLayer.SourceUVChannel = Slot.SourceUVChannel;
            const int32 Before = OutSources.Sources.Num();
            AddSource(SlotLayer, Slot.MaterialSlotIndex, Priority, OutSources);
            Priority += OutSources.Sources.Num() != Before ? 1 : 0;
        }
    }
    if (OutSources.Sources.IsEmpty())
    {
        OutError = TEXT("The External Skeletal Mesh contains no usable source material surfaces.");
        return false;
    }
    OutSources.ProviderSignature = FString::Printf(
        TEXT("%s|%s"), *Mesh->GetPathName(), *Layer.ExternalMeshSource.BakeTransform.ToHumanReadableString());
    return true;
}
