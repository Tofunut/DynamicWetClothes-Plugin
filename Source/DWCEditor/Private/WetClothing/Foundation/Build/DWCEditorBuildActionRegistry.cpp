//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Build/DWCEditorBuildActionRegistry.h"

#define LOCTEXT_NAMESPACE "DWCEditorBuildActionRegistry"

namespace
{
    FDWCEditorBuildActionDependency Dependency(
        const EDWCEditorBuildAction Action,
        const EDWCEditorBuildDependencyKind Kind)
    {
        return {Action, Kind};
    }

    const TArray<FDWCEditorBuildActionDescriptor>& BuildDescriptors()
    {
        static const TArray<FDWCEditorBuildActionDescriptor> Descriptors = {
            {
                EDWCEditorBuildAction::InitializeDataUV,
                TEXT("InitializeDataUV"),
                LOCTEXT("InitializeDataUV", "Initialize DWC Data UV"),
                LOCTEXT("InitializeDataUVDescription", "Generate the shared DWC data UV layout used by derived texture outputs."),
                TEXT("Setup"), NAME_None, TEXT("Icons.Wrench"), 100, {}, false,
                EDWCEditorBuildSurfaceMode::WetPart
            },
            {
                EDWCEditorBuildAction::BuildCPURuntimeData,
                TEXT("BuildCPURuntimeData"),
                LOCTEXT("BuildCPURuntimeData", "Build CPU Runtime Data"),
                LOCTEXT("BuildCPURuntimeDataDescription", "Build CPU vertex wetness simulation data."),
                TEXT("Runtime"), NAME_None, TEXT("Icons.Build"), 200,
                {Dependency(EDWCEditorBuildAction::InitializeDataUV, EDWCEditorBuildDependencyKind::HardPrerequisite)},
                true, EDWCEditorBuildSurfaceMode::WetPart
            },
            {
                EDWCEditorBuildAction::BuildGPURuntimeData,
                TEXT("BuildGPURuntimeData"),
                LOCTEXT("BuildGPURuntimeData", "Build GPU Runtime Data"),
                LOCTEXT("BuildGPURuntimeDataDescription", "Build GPU simulation payload and lookup textures."),
                TEXT("Runtime"), NAME_None, TEXT("Icons.Build"), 300,
                {Dependency(EDWCEditorBuildAction::InitializeDataUV, EDWCEditorBuildDependencyKind::HardPrerequisite)},
                true, EDWCEditorBuildSurfaceMode::WetPart
            },
            {
                EDWCEditorBuildAction::BakeRenderProfileData,
                TEXT("BakeRenderProfileData"),
                LOCTEXT("BakeRenderProfileData", "Bake Render Profile Data"),
                LOCTEXT("BakeRenderProfileDataDescription", "Bake wet-part render profile lookup textures."),
                TEXT("Rendering"), NAME_None, TEXT("Icons.Save"), 400,
                {Dependency(EDWCEditorBuildAction::InitializeDataUV, EDWCEditorBuildDependencyKind::HardPrerequisite)},
                true, EDWCEditorBuildSurfaceMode::WetPart
            },
            {
                EDWCEditorBuildAction::GenerateMaterials,
                TEXT("GenerateMaterials"),
                LOCTEXT("GenerateMaterials", "Generate Materials"),
                LOCTEXT("GenerateMaterialsDescription", "Generate or refresh DWC runtime materials for wettable slots."),
                TEXT("Rendering"), NAME_None, TEXT("Icons.Refresh"), 500,
                {
                    Dependency(EDWCEditorBuildAction::InitializeDataUV, EDWCEditorBuildDependencyKind::HardPrerequisite),
                    Dependency(EDWCEditorBuildAction::BakeRenderProfileData, EDWCEditorBuildDependencyKind::OrderingOnly)
                },
                true, EDWCEditorBuildSurfaceMode::WetPart
            },
            {
                EDWCEditorBuildAction::BakeWrinkleTextures,
                TEXT("BakeWrinkleTextures"),
                LOCTEXT("BakeWrinkleTextures", "Bake Wrinkle Textures"),
                LOCTEXT("BakeWrinkleTexturesDescription", "Bake authored wrinkle normal and coverage textures."),
                TEXT("Rendering"), NAME_None, TEXT("Icons.Save"), 600,
                {
                    Dependency(EDWCEditorBuildAction::InitializeDataUV, EDWCEditorBuildDependencyKind::HardPrerequisite),
                    Dependency(EDWCEditorBuildAction::GenerateMaterials, EDWCEditorBuildDependencyKind::OrderingOnly)
                },
                true, EDWCEditorBuildSurfaceMode::Wrinkle
            },
            {
                EDWCEditorBuildAction::BakeTransparencyTextures,
                TEXT("BakeTransparencyTextures"),
                LOCTEXT("BakeTransparencyTextures", "Bake Transparency Textures"),
                LOCTEXT("BakeTransparencyTexturesDescription", "Bake the complete authored transparency result."),
                TEXT("Rendering"), NAME_None, TEXT("Icons.Save"), 700,
                {
                    Dependency(EDWCEditorBuildAction::InitializeDataUV, EDWCEditorBuildDependencyKind::HardPrerequisite),
                    Dependency(EDWCEditorBuildAction::BakeWrinkleTextures, EDWCEditorBuildDependencyKind::OptionalInput)
                },
                true, EDWCEditorBuildSurfaceMode::Transparency
            },
            {
                EDWCEditorBuildAction::RebakeAffectedTransparencyMaps,
                TEXT("RebakeAffectedTransparencyMaps"),
                LOCTEXT("RebakeAffectedTransparencyMaps", "Rebake Affected Transparency Maps"),
                LOCTEXT("RebakeAffectedTransparencyMapsDescription", "Rebuild only transparency outputs invalidated by wrinkle coverage."),
                TEXT("Rendering"), NAME_None, TEXT("Icons.Refresh"), 800,
                {Dependency(EDWCEditorBuildAction::BakeWrinkleTextures, EDWCEditorBuildDependencyKind::HardPrerequisite)},
                true, EDWCEditorBuildSurfaceMode::Transparency
            },
            {
                EDWCEditorBuildAction::SaveAsset,
                TEXT("SaveAsset"),
                LOCTEXT("SaveAsset", "Save Asset"),
                LOCTEXT("SaveAssetDescription", "Save the WCA and completed generated output references."),
                TEXT("Finalize"), NAME_None, TEXT("Icons.Save"), 900,
                {
                    Dependency(EDWCEditorBuildAction::BuildCPURuntimeData, EDWCEditorBuildDependencyKind::OrderingOnly),
                    Dependency(EDWCEditorBuildAction::BuildGPURuntimeData, EDWCEditorBuildDependencyKind::OrderingOnly),
                    Dependency(EDWCEditorBuildAction::BakeRenderProfileData, EDWCEditorBuildDependencyKind::OrderingOnly),
                    Dependency(EDWCEditorBuildAction::GenerateMaterials, EDWCEditorBuildDependencyKind::OrderingOnly),
                    Dependency(EDWCEditorBuildAction::BakeWrinkleTextures, EDWCEditorBuildDependencyKind::OrderingOnly),
                    Dependency(EDWCEditorBuildAction::BakeTransparencyTextures, EDWCEditorBuildDependencyKind::OrderingOnly),
                    Dependency(EDWCEditorBuildAction::RebakeAffectedTransparencyMaps, EDWCEditorBuildDependencyKind::OrderingOnly)
                },
                false,
                EDWCEditorBuildSurfaceMode::Any
            }
        };
        return Descriptors;
    }

    bool VisitForCycle(
        const EDWCEditorBuildAction Action,
        TMap<EDWCEditorBuildAction, uint8>& Marks,
        FString& OutError)
    {
        uint8& Mark = Marks.FindOrAdd(Action);
        if (Mark == 1)
        {
            OutError = TEXT("The build action dependency graph contains a cycle.");
            return false;
        }
        if (Mark == 2)
        {
            return true;
        }

        Mark = 1;
        const FDWCEditorBuildActionDescriptor* Descriptor = FDWCEditorBuildActionRegistry::Find(Action);
        if (Descriptor == nullptr)
        {
            OutError = TEXT("The build action dependency graph references an unregistered action.");
            return false;
        }
        for (const FDWCEditorBuildActionDependency& Item : Descriptor->Dependencies)
        {
            if (!VisitForCycle(Item.Action, Marks, OutError))
            {
                return false;
            }
        }
        Mark = 2;
        return true;
    }
}

TConstArrayView<FDWCEditorBuildActionDescriptor> FDWCEditorBuildActionRegistry::GetDescriptors()
{
    return BuildDescriptors();
}

const FDWCEditorBuildActionDescriptor* FDWCEditorBuildActionRegistry::Find(const EDWCEditorBuildAction Action)
{
    return BuildDescriptors().FindByPredicate(
        [Action](const FDWCEditorBuildActionDescriptor& Descriptor)
        {
            return Descriptor.Action == Action;
        });
}

bool FDWCEditorBuildActionRegistry::Validate(FString& OutError)
{
    OutError.Reset();
    const TConstArrayView<FDWCEditorBuildActionDescriptor> Descriptors = GetDescriptors();
    if (Descriptors.Num() != static_cast<int32>(EDWCEditorBuildAction::Count))
    {
        OutError = TEXT("The build action registry does not contain every action exactly once.");
        return false;
    }

    TSet<EDWCEditorBuildAction> Actions;
    TSet<FName> Names;
    TSet<int32> Orders;
    for (const FDWCEditorBuildActionDescriptor& Descriptor : Descriptors)
    {
        if (Descriptor.StableName.IsNone() || Actions.Contains(Descriptor.Action) ||
            Names.Contains(Descriptor.StableName) || Orders.Contains(Descriptor.StableOrder))
        {
            OutError = TEXT("The build action registry contains a duplicate or incomplete descriptor.");
            return false;
        }
        Actions.Add(Descriptor.Action);
        Names.Add(Descriptor.StableName);
        Orders.Add(Descriptor.StableOrder);
    }

    TMap<EDWCEditorBuildAction, uint8> Marks;
    for (const FDWCEditorBuildActionDescriptor& Descriptor : Descriptors)
    {
        if (!VisitForCycle(Descriptor.Action, Marks, OutError))
        {
            return false;
        }
    }
    return true;
}

#undef LOCTEXT_NAMESPACE
