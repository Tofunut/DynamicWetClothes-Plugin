// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/Foundation/Authoring/DWCEditorPropertyImpactRouter.h"

#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetClothingAssetSetupData.h"
#include "DataAssets/WetClothingPartData.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "DataAssets/WetClothingWrinkleData.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogDWCEditorPropertyImpact, Log, All);

namespace
{
    bool IsOwner(const FProperty* Property, const UStruct* ExpectedOwner)
    {
        return Property != nullptr && Property->GetOwnerStruct() == ExpectedOwner;
    }

    bool IsPartOwner(const FProperty* Property)
    {
        const UStruct* Owner = Property != nullptr ? Property->GetOwnerStruct() : nullptr;
        return Owner == FWetClothingPartData::StaticStruct() ||
            Owner == FWetClothingEditableWetPartData::StaticStruct() ||
            Owner == FWetClothingAuthoredMaterialSlot::StaticStruct() ||
            Owner == FWetClothingWetPartEntry::StaticStruct() ||
            Owner == FWetPartProfileAssignment::StaticStruct() ||
            Owner == FWetPartSurfaceWaterSettings::StaticStruct();
    }

    bool IsWrinkleOwner(const FProperty* Property)
    {
        const UStruct* Owner = Property != nullptr ? Property->GetOwnerStruct() : nullptr;
        return Owner == FWetClothingWrinkleData::StaticStruct() ||
            Owner == FWetWrinklePatchPlacement::StaticStruct() ||
            Owner == FWetProceduralRidgeStroke::StaticStruct() ||
            Owner == FWetProceduralRidgeStrokePoint::StaticStruct() ||
            Owner == FWetProceduralRidgeEndpoint::StaticStruct() ||
            Owner == FWetProceduralRidgeFlareSettings::StaticStruct() ||
            Owner == FWetProceduralRidgeVariationSettings::StaticStruct() ||
            Owner == FWetWrinkleBakeSettings::StaticStruct() ||
            Owner == FWetWrinkleRuntimeNormalSource::StaticStruct() ||
            Owner == FWetWrinkleCoverageExtractionSettings::StaticStruct();
    }

    bool IsTransparencyOwner(const FProperty* Property)
    {
        const UStruct* Owner = Property != nullptr ? Property->GetOwnerStruct() : nullptr;
        return Owner == FWetClothingTransparencyData::StaticStruct() ||
            Owner == FWetClothingTransparencyLayerData::StaticStruct() ||
            Owner == FWetClothingTransparencyTargetSurface::StaticStruct() ||
            Owner == FWetClothingTransparencyRaySettings::StaticStruct() ||
            Owner == FWetClothingTransparencySameMeshSource::StaticStruct() ||
            Owner == FWetClothingTransparencyBlueprintSource::StaticStruct() ||
            Owner == FWetClothingTransparencyBlueprintComponentBinding::StaticStruct() ||
            Owner == FWetClothingTransparencyExternalMeshSource::StaticStruct() ||
            Owner == FWetClothingTransparencyExternalMeshEntry::StaticStruct() ||
            Owner == FWetClothingTransparencyManualColorSource::StaticStruct() ||
            Owner == FWetClothingTransparencyInnerSlot::StaticStruct() ||
            Owner == FDWCTransparencyBrushStroke::StaticStruct() ||
            Owner == FDWCTransparencyRevealColorStroke::StaticStruct();
    }

    bool IsSetupOwner(const FProperty* Property)
    {
        const UStruct* Owner = Property != nullptr ? Property->GetOwnerStruct() : nullptr;
        return Owner == FWCAMetadata::StaticStruct() ||
            Owner == FDWCWetClothingAssetSetupSettings::StaticStruct();
    }

    bool IsDerivedOwner(const FProperty* Property)
    {
        const UStruct* Owner = Property != nullptr ? Property->GetOwnerStruct() : nullptr;
        return Owner == FWCADerivedData::StaticStruct() ||
            Owner == FWCADerivedInlineData::StaticStruct() ||
            Owner == FWCADerivedBulkData::StaticStruct() ||
            Owner == FDWCAssetBakeState::StaticStruct() ||
            Owner == FDWCTriangleValidationSummary::StaticStruct() ||
            Owner == FDWCDataUVLODMetadata::StaticStruct();
    }

    FDWCEditorPropertyImpactRoute MakePartRoute()
    {
        FDWCEditorPropertyImpactRoute Route;
        Route.bRelevant = true;
        Route.bKnownProperty = true;
        Route.bApplyCommittedImpact = true;
        Route.Change.Domain = EDWCEditorAuthoringDomain::Part;
        Route.Change.Impact = EDWCEditorAuthoringImpact::AssetDirty |
            EDWCEditorAuthoringImpact::ElementList |
            EDWCEditorAuthoringImpact::Preview |
            EDWCEditorAuthoringImpact::HitTopology |
            EDWCEditorAuthoringImpact::Details |
            EDWCEditorAuthoringImpact::PartSlotPresentation;
        Route.Change.InvalidatedBakeOutputMask = DWCBakeOutput::All;
        return Route;
    }

    FDWCEditorPropertyImpactRoute MakeWrinkleRoute()
    {
        FDWCEditorPropertyImpactRoute Route;
        Route.bRelevant = true;
        Route.bKnownProperty = true;
        Route.bApplyCommittedImpact = true;
        Route.Change.Domain = EDWCEditorAuthoringDomain::Wrinkle;
        Route.Change.Impact = EDWCEditorAuthoringImpact::AssetDirty |
            EDWCEditorAuthoringImpact::ElementList |
            EDWCEditorAuthoringImpact::Preview |
            EDWCEditorAuthoringImpact::Details |
            EDWCEditorAuthoringImpact::WrinkleBake;
        return Route;
    }

    FDWCEditorPropertyImpactRoute MakeTransparencyRoute()
    {
        FDWCEditorPropertyImpactRoute Route;
        Route.bRelevant = true;
        Route.bKnownProperty = true;
        Route.bApplyCommittedImpact = true;
        Route.Change.Domain = EDWCEditorAuthoringDomain::Transparency;
        Route.Change.Impact = EDWCEditorAuthoringImpact::AssetDirty |
            EDWCEditorAuthoringImpact::ElementList |
            EDWCEditorAuthoringImpact::Preview |
            EDWCEditorAuthoringImpact::Details |
            EDWCEditorAuthoringImpact::TransparencyAutoBake |
            EDWCEditorAuthoringImpact::TransparencyFinalBake;
        return Route;
    }

    FDWCEditorPropertyImpactRoute MakeDerivedRoute()
    {
        FDWCEditorPropertyImpactRoute Route;
        Route.bRelevant = true;
        Route.bKnownProperty = true;
        Route.Change.bAuthoringDataChanged = false;
        Route.Change.Impact = EDWCEditorAuthoringImpact::Details |
            EDWCEditorAuthoringImpact::RuntimeBinding;
        return Route;
    }
}

FDWCEditorPropertyImpactRoute FDWCEditorPropertyImpactRouter::Route(
    const UWetClothingAsset&,
    const FPropertyChangedEvent& Event)
{
    const FProperty* Property = Event.Property;
    const FProperty* MemberProperty = Event.MemberProperty;
    if (IsPartOwner(Property) || IsPartOwner(MemberProperty) ||
        IsOwner(Property, FWCAAuthoredData::StaticStruct()) &&
            Property->GetFName() == GET_MEMBER_NAME_CHECKED(FWCAAuthoredData, PartData))
    {
        return MakePartRoute();
    }
    if (IsWrinkleOwner(Property) || IsWrinkleOwner(MemberProperty) ||
        IsOwner(Property, FWCAAuthoredData::StaticStruct()) &&
            Property->GetFName() == GET_MEMBER_NAME_CHECKED(FWCAAuthoredData, WrinkleData))
    {
        return MakeWrinkleRoute();
    }
    if (IsTransparencyOwner(Property) || IsTransparencyOwner(MemberProperty) ||
        IsOwner(Property, FWCAAuthoredData::StaticStruct()) &&
            Property->GetFName() == GET_MEMBER_NAME_CHECKED(FWCAAuthoredData, TransparencyData))
    {
        return MakeTransparencyRoute();
    }
    if (IsSetupOwner(Property) || IsSetupOwner(MemberProperty))
    {
        return MakePartRoute();
    }
    if (IsDerivedOwner(Property) || IsDerivedOwner(MemberProperty))
    {
        return MakeDerivedRoute();
    }

    FDWCEditorPropertyImpactRoute Route;
    Route.bRelevant = true;
    Route.bRequestFullRefresh = true;
    Route.Change.bAuthoringDataChanged = false;
    Route.Change.Impact = EDWCEditorAuthoringImpact::ElementList |
        EDWCEditorAuthoringImpact::Preview |
        EDWCEditorAuthoringImpact::HitTopology |
        EDWCEditorAuthoringImpact::Details |
        EDWCEditorAuthoringImpact::PartSlotPresentation;
    UE_LOG(
        LogDWCEditorPropertyImpact,
        Verbose,
        TEXT("Falling back to a full WCA editor refresh for property '%s' (member '%s')."),
        *GetNameSafe(Property),
        *GetNameSafe(MemberProperty));
    return Route;
}
