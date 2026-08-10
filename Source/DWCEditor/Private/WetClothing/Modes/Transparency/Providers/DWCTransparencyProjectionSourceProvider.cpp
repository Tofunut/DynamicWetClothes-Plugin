// Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/Providers/DWCTransparencyProjectionSourceProvider.h"

#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "PreviewScene.h"

namespace
{
    FString MakeBlueprintHierarchySignature(
        const TSubclassOf<AActor> BlueprintClass,
        const TArray<FDWCTransparencyBlueprintMeshComponent>& Components)
    {
        FString Signature = FString::Printf(
            TEXT("DWCTransparencyBlueprintHierarchy_v1|Class=%s"),
            *GetPathNameSafe(BlueprintClass.Get()));
        for (const FDWCTransparencyBlueprintMeshComponent& Component : Components)
        {
            const FString MeshSignature = UWetClothingAsset::BuildMeshContentSignature(
                Component.SkeletalMesh, 0, 0);
            Signature += FString::Printf(
                TEXT("|%s:Parent=%s:Mesh=%s"),
                *Component.ComponentName.ToString(),
                *Component.ParentComponentName.ToString(),
                *MeshSignature);
            Signature += FString::Printf(
                TEXT(":Path=%s:Depth=%d"),
                *Component.DisplayPath,
                Component.HierarchyDepth);
            for (const UMaterialInterface* Material : Component.Materials)
            {
                Signature += FString::Printf(
                    TEXT(":Material=%s"),
                    Material != nullptr
                        ? *Material->GetLightingGuid().ToString(EGuidFormats::Digits)
                        : TEXT("None"));
            }
            const FTransform& Transform = Component.BakeTransform;
            Signature += FString::Printf(
                TEXT(":Transform{T=%.9g,%.9g,%.9g;R=%.9g,%.9g,%.9g,%.9g;S=%.9g,%.9g,%.9g}"),
                Transform.GetTranslation().X,
                Transform.GetTranslation().Y,
                Transform.GetTranslation().Z,
                Transform.GetRotation().X,
                Transform.GetRotation().Y,
                Transform.GetRotation().Z,
                Transform.GetRotation().W,
                Transform.GetScale3D().X,
                Transform.GetScale3D().Y,
                Transform.GetScale3D().Z);
        }
        return Signature;
    }

    const FDWCTransparencyBlueprintMeshComponent* FindComponent(
        const FDWCTransparencyBlueprintHierarchy& Hierarchy,
        const FWetClothingTransparencyBlueprintComponentBinding& Binding,
        FString& OutError)
    {
        if (!Binding.IsBound())
        {
            OutError = TEXT("Select a Blueprint Target Component before generating the transparency source.");
            return nullptr;
        }

        const FDWCTransparencyBlueprintMeshComponent* Component =
            Hierarchy.MeshComponents.FindByPredicate(
                [&Binding](const FDWCTransparencyBlueprintMeshComponent& Candidate)
                {
                    return Candidate.ComponentName == Binding.ComponentName;
                });
        if (Component == nullptr)
        {
            OutError = FString::Printf(
                TEXT("Blueprint component '%s' no longer exists."),
                *Binding.ComponentName.ToString());
            return nullptr;
        }

        USkeletalMesh* ExpectedMesh = Binding.ExpectedSkeletalMesh.LoadSynchronous();
        if (ExpectedMesh != nullptr && Component->SkeletalMesh != ExpectedMesh)
        {
            OutError = FString::Printf(
                TEXT("Blueprint component '%s' no longer uses the configured Skeletal Mesh."),
                *Binding.ComponentName.ToString());
            return nullptr;
        }
        if (Component->SkeletalMesh == nullptr)
        {
            OutError = FString::Printf(
                TEXT("Blueprint component '%s' has no Skeletal Mesh."),
                *Binding.ComponentName.ToString());
            return nullptr;
        }
        return Component;
    }

    FDWCBakeResolvedLayer MakeBlueprintLayer(
        const FDWCTransparencyBlueprintMeshComponent& Component,
        const FWetClothingTransparencyBlueprintComponentBinding& Binding,
        const int32 Priority,
        const float MaxRevealDistance)
    {
        FDWCBakeResolvedLayer Layer;
        Layer.LayerId = Component.ComponentName;
        Layer.LayerOrder = Priority;
        Layer.ComponentDisplayName = Component.ComponentName;
        Layer.ComponentPath = Component.DisplayPath;
        Layer.SkeletalMesh = Component.SkeletalMesh;
        Layer.Materials = Component.Materials;
        Layer.BakeTransform = Component.BakeTransform;
        Layer.bCanBeRevealSource = Binding.Role == EDWCTransparencyBlueprintSourceRole::RevealSource;
        Layer.bCanBeWetOuterLayer = false;
        Layer.bBlocksReveal = Binding.Role == EDWCTransparencyBlueprintSourceRole::BlockerOnly;
        Layer.MaxRevealDistance = MaxRevealDistance;
        Layer.SourceUVChannel = FMath::Max(Binding.SourceUVChannel, 0);
        return Layer;
    }

    FString MakeBlueprintProviderSignature(
        const FDWCTransparencyBlueprintHierarchy& Hierarchy,
        const FWetClothingTransparencyBlueprintSource& Config)
    {
        FString Signature = FString::Printf(
            TEXT("%s|Target=%s"), *Hierarchy.BuildSignature, *Config.TargetComponent.ComponentName.ToString());
        for (const FWetClothingTransparencyBlueprintComponentBinding& Source : Config.SourcePriority)
        {
            const FDWCTransparencyBlueprintMeshComponent* Component =
                Hierarchy.MeshComponents.FindByPredicate(
                    [&Source](const FDWCTransparencyBlueprintMeshComponent& Candidate)
                    {
                        return Candidate.ComponentName == Source.ComponentName;
                    });
            const FString SourceMeshSignature = Component != nullptr
                ? UWetClothingAsset::BuildMeshContentSignature(
                    Component->SkeletalMesh,
                    0,
                    FMath::Max(Source.SourceUVChannel, 0))
                : TEXT("Missing");
            Signature += FString::Printf(
                TEXT("|Source=%s:Expected=%s:UV=%d:Role=%d:Mesh=%s"),
                *Source.ComponentName.ToString(),
                *Source.ExpectedSkeletalMesh.ToSoftObjectPath().ToString(),
                Source.SourceUVChannel,
                static_cast<int32>(Source.Role),
                *SourceMeshSignature);
        }
        return Signature;
    }
}

bool FDWCTransparencyProjectionSourceProvider::BuildBlueprintHierarchy(
    const TSubclassOf<AActor> BlueprintClass,
    FDWCTransparencyBlueprintHierarchy& OutHierarchy,
    FString& OutError)
{
    check(IsInGameThread());
    OutHierarchy = FDWCTransparencyBlueprintHierarchy();
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
    SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
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

    TArray<USkeletalMeshComponent*> MeshComponents;
    PreviewActor->GetComponents<USkeletalMeshComponent>(MeshComponents);
    for (USkeletalMeshComponent* MeshComponent : MeshComponents)
    {
        if (MeshComponent == nullptr || MeshComponent->GetSkeletalMeshAsset() == nullptr)
        {
            continue;
        }

        FDWCTransparencyBlueprintMeshComponent& Component =
            OutHierarchy.MeshComponents.AddDefaulted_GetRef();
        Component.ComponentName = MeshComponent->GetFName();
        Component.ParentComponentName = MeshComponent->GetAttachParent() != nullptr
            ? MeshComponent->GetAttachParent()->GetFName()
            : NAME_None;
        TArray<FString> HierarchyNames;
        HierarchyNames.Add(MeshComponent->GetName());
        for (USceneComponent* Parent = MeshComponent->GetAttachParent();
             Parent != nullptr && Component.HierarchyDepth < 32;
             Parent = Parent->GetAttachParent())
        {
            HierarchyNames.Insert(Parent->GetName(), 0);
            ++Component.HierarchyDepth;
        }
        Component.DisplayPath = FString::Join(HierarchyNames, TEXT(" / "));
        Component.SkeletalMesh = MeshComponent->GetSkeletalMeshAsset();
        Component.BakeTransform = MeshComponent->GetComponentTransform().GetRelativeTransform(
            PreviewActor->GetActorTransform());
        const int32 MaterialCount = MeshComponent->GetNumMaterials();
        Component.Materials.Reserve(MaterialCount);
        for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
        {
            Component.Materials.Add(MeshComponent->GetMaterial(MaterialIndex));
        }
    }
    OutHierarchy.MeshComponents.Sort(
        [](const FDWCTransparencyBlueprintMeshComponent& Left,
            const FDWCTransparencyBlueprintMeshComponent& Right)
        {
            return Left.ComponentName.LexicalLess(Right.ComponentName);
        });
    if (OutHierarchy.MeshComponents.IsEmpty())
    {
        OutError = FString::Printf(
            TEXT("Blueprint '%s' contains no Skeletal Mesh Components."),
            *GetNameSafe(BlueprintClass.Get()));
        return false;
    }
    OutHierarchy.BuildSignature = MakeBlueprintHierarchySignature(
        BlueprintClass, OutHierarchy.MeshComponents);
    return true;
}

namespace
{

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
        const FWetClothingTransparencyExternalMeshEntry& Entry,
        const int32 Priority,
        const float MaxRevealDistance)
    {
        FDWCBakeResolvedLayer Layer;
        Layer.LayerId = Entry.SourceGuid.IsValid()
            ? FName(*FString::Printf(TEXT("External_%s"), *Entry.SourceGuid.ToString(EGuidFormats::Digits)))
            : FName(*FString::Printf(TEXT("External_%s_P%d"), *Mesh.GetName(), Priority));
        Layer.LayerOrder = Priority;
        Layer.ComponentDisplayName = Mesh.GetFName();
        Layer.ComponentPath = Mesh.GetPathName();
        Layer.SkeletalMesh = &Mesh;
        Layer.BakeTransform = Entry.BakeTransform;
        Layer.bCanBeRevealSource = Entry.Role == EDWCTransparencyBlueprintSourceRole::RevealSource;
        Layer.bCanBeWetOuterLayer = false;
        Layer.bBlocksReveal = Entry.Role == EDWCTransparencyBlueprintSourceRole::BlockerOnly;
        Layer.MaxRevealDistance = MaxRevealDistance;
        Layer.SourceUVChannel = FMath::Max(Entry.SourceUVChannel, 0);
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

    const FWetClothingTransparencyBlueprintSource& Config = Layer.BlueprintSource;
    TSubclassOf<AActor> BlueprintClass = Config.BlueprintClass.LoadSynchronous();
    FDWCTransparencyBlueprintHierarchy Hierarchy;
    if (!BuildBlueprintHierarchy(BlueprintClass, Hierarchy, OutError))
    {
        return false;
    }

    USkeletalMesh* RuntimeMesh = Asset.GetRuntimeSkeletalMesh();
    USkeletalMesh* SourceMesh = Asset.GetSourceSkeletalMesh();
    const FDWCTransparencyBlueprintMeshComponent* OuterComponent =
        FindComponent(Hierarchy, Config.TargetComponent, OutError);
    if (OuterComponent == nullptr)
    {
        return false;
    }
    if (OuterComponent->SkeletalMesh != RuntimeMesh && OuterComponent->SkeletalMesh != SourceMesh)
    {
        OutError = TEXT("The selected Blueprint Target Component does not use the WCA target Skeletal Mesh.");
        return false;
    }
    OutSources.OuterBakeTransform = OuterComponent->BakeTransform;

    int32 Priority = 0;
    TSet<FName> AddedComponents;
    for (const FWetClothingTransparencyBlueprintComponentBinding& Binding : Config.SourcePriority)
    {
        const FDWCTransparencyBlueprintMeshComponent* Component =
            FindComponent(Hierarchy, Binding, OutError);
        if (Component == nullptr)
        {
            return false;
        }
        if (Component->ComponentName == OuterComponent->ComponentName)
        {
            OutError = TEXT("The Blueprint Target Component cannot also be a raycast source.");
            return false;
        }
        if (AddedComponents.Contains(Component->ComponentName))
        {
            OutError = FString::Printf(
                TEXT("Blueprint source component '%s' is listed more than once."),
                *Component->ComponentName.ToString());
            return false;
        }
        AddedComponents.Add(Component->ComponentName);

        const int32 ComponentPriority = Priority++;
        const FDWCBakeResolvedLayer Resolved = MakeBlueprintLayer(
            *Component, Binding, ComponentPriority, Layer.RaySettings.MaxRayDistance);
        for (int32 SlotIndex = 0; SlotIndex < Resolved.Materials.Num(); ++SlotIndex)
        {
            AddSource(Resolved, SlotIndex, ComponentPriority, OutSources);
        }
    }
    if (!OutSources.Sources.ContainsByPredicate(
            [](const FDWCTransparencyProjectionSource& Source)
            {
                return Source.Layer.bCanBeRevealSource;
            }))
    {
        OutError = TEXT("Select at least one Blueprint Reveal Source with usable material surfaces.");
        return false;
    }
    OutSources.ProviderSignature = MakeBlueprintProviderSignature(Hierarchy, Config);
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
    const FWetClothingTransparencyExternalMeshSource& Config = Layer.ExternalMeshSource;
    TArray<FWetClothingTransparencyExternalMeshEntry> LegacyEntries;
    const TArray<FWetClothingTransparencyExternalMeshEntry>* Entries = &Config.SourcePriority;
    const bool bUsingLegacyEntries = Entries->IsEmpty() && Config.SkeletalMesh != nullptr;
    if (bUsingLegacyEntries)
    {
        if (Config.SourceSlotPriority.IsEmpty())
        {
            FWetClothingTransparencyExternalMeshEntry& Entry = LegacyEntries.AddDefaulted_GetRef();
            Entry.SkeletalMesh = Config.SkeletalMesh;
            Entry.BakeTransform = Config.BakeTransform;
        }
        else
        {
            for (const FWetClothingTransparencyInnerSlot& LegacySlot : Config.SourceSlotPriority)
            {
                FWetClothingTransparencyExternalMeshEntry& Entry = LegacyEntries.AddDefaulted_GetRef();
                Entry.SkeletalMesh = Config.SkeletalMesh;
                Entry.BakeTransform = Config.BakeTransform;
                Entry.SourceUVChannel = LegacySlot.SourceUVChannel;
            }
        }
        Entries = &LegacyEntries;
    }
    if (Entries->IsEmpty())
    {
        OutError = TEXT("Add at least one External Skeletal Mesh raycast source before generating the transparency map.");
        return false;
    }

    FString Signature = bUsingLegacyEntries
        ? TEXT("DWCTransparencyExternalSources_v2|Legacy=1")
        : TEXT("DWCTransparencyExternalSources_v2");
    for (int32 Priority = 0; Priority < Entries->Num(); ++Priority)
    {
        const FWetClothingTransparencyExternalMeshEntry& Entry = (*Entries)[Priority];
        USkeletalMesh* Mesh = Entry.SkeletalMesh;
        if (Mesh == nullptr)
        {
            OutError = FString::Printf(TEXT("External raycast source %d has no Skeletal Mesh."), Priority + 1);
            return false;
        }

        const FDWCBakeResolvedLayer Resolved = MakeExternalLayer(
            *Mesh, Entry, Priority, Layer.RaySettings.MaxRayDistance);
        const int32 SourceCountBefore = OutSources.Sources.Num();
        const FWetClothingTransparencyInnerSlot* LegacySlot = bUsingLegacyEntries &&
            Config.SourceSlotPriority.IsValidIndex(Priority)
                ? &Config.SourceSlotPriority[Priority]
                : nullptr;
        if (LegacySlot != nullptr)
        {
            if (!Resolved.Materials.IsValidIndex(LegacySlot->MaterialSlotIndex))
            {
                OutError = FString::Printf(
                    TEXT("External raycast source '%s' references missing material slot %d."),
                    *Mesh->GetName(), LegacySlot->MaterialSlotIndex);
                return false;
            }
            AddSource(Resolved, LegacySlot->MaterialSlotIndex, Priority, OutSources);
        }
        else
        {
            for (int32 SlotIndex = 0; SlotIndex < Resolved.Materials.Num(); ++SlotIndex)
            {
                AddSource(Resolved, SlotIndex, Priority, OutSources);
            }
        }
        if (OutSources.Sources.Num() == SourceCountBefore &&
            Entry.Role == EDWCTransparencyBlueprintSourceRole::RevealSource)
        {
            OutSources.Warnings.Add(FString::Printf(
                TEXT("External source '%s' has no usable reveal material surfaces."),
                *Mesh->GetName()));
        }

        const FTransform& Transform = Entry.BakeTransform;
        Signature += FString::Printf(
            TEXT("|%d:%s:Mesh=%s:UV=%d:Role=%d:T=%.9g,%.9g,%.9g:R=%.9g,%.9g,%.9g,%.9g:S=%.9g,%.9g,%.9g"),
            Priority,
            *Entry.SourceGuid.ToString(EGuidFormats::Digits),
            *UWetClothingAsset::BuildMeshContentSignature(Mesh, 0, Resolved.SourceUVChannel),
            Resolved.SourceUVChannel,
            static_cast<int32>(Entry.Role),
            Transform.GetTranslation().X,
            Transform.GetTranslation().Y,
            Transform.GetTranslation().Z,
            Transform.GetRotation().X,
            Transform.GetRotation().Y,
            Transform.GetRotation().Z,
            Transform.GetRotation().W,
            Transform.GetScale3D().X,
            Transform.GetScale3D().Y,
            Transform.GetScale3D().Z);
    }
    if (!OutSources.Sources.ContainsByPredicate(
            [](const FDWCTransparencyProjectionSource& Source)
            {
                return Source.Layer.bCanBeRevealSource;
            }))
    {
        OutError = TEXT("Select at least one External Skeletal Mesh reveal source with usable material surfaces.");
        return false;
    }
    OutSources.ProviderSignature = MoveTemp(Signature);
    return true;
}
