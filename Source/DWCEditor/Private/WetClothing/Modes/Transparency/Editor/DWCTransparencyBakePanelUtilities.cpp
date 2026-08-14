// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/Modes/Transparency/Editor/DWCTransparencyBakePanelUtilities.h"

#include "DataAssets/WetClothingAsset.h"

#define LOCTEXT_NAMESPACE "DWCTransparencyBakePanelUtilities"

namespace UE::DWCEditor::TransparencyPanel
{
namespace
{
constexpr float DefaultBrushSizeCm = 8.0f;
constexpr float DefaultBrushRadiusUV = 0.0677f;
constexpr float UVPerCm = DefaultBrushRadiusUV / DefaultBrushSizeCm;
}

int32 ResolveDataUVChannel(const UWetClothingAsset* Asset)
{
    return Asset != nullptr && Asset->HasValidDataUVForLOD(0)
        ? Asset->GetDWCDataUVChannelIndex()
        : INDEX_NONE;
}

const TCHAR* GetStrokeModeLabel(const EDWCTransparencyBrushMode Mode)
{
    switch (Mode)
    {
    case EDWCTransparencyBrushMode::Erase:
        return TEXT("Erase");
    case EDWCTransparencyBrushMode::SetValue:
        return TEXT("Set");
    case EDWCTransparencyBrushMode::Smooth:
        return TEXT("Smooth");
    case EDWCTransparencyBrushMode::ResetToAuto:
        return TEXT("Reset");
    case EDWCTransparencyBrushMode::Apply:
    default:
        return TEXT("Apply");
    }
}

const TCHAR* GetRevealColorStrokeModeLabel(const EDWCTransparencyRevealColorBrushMode Mode)
{
    switch (Mode)
    {
    case EDWCTransparencyRevealColorBrushMode::EraseToBase:
        return TEXT("Erase to Base");
    case EDWCTransparencyRevealColorBrushMode::Smooth:
        return TEXT("Smooth");
    case EDWCTransparencyRevealColorBrushMode::Paint:
    default:
        return TEXT("Paint");
    }
}

FText GetSourceTypeLabel(const EDWCTransparencySourceType SourceType)
{
    switch (SourceType)
    {
    case EDWCTransparencySourceType::OtherSkeletalMeshComponents:
        return LOCTEXT("TransparencySourceTypeMultipleMeshes", "Blueprint / Multiple Skeletal Meshes");
    case EDWCTransparencySourceType::ManualColorOrTexture:
        return LOCTEXT("TransparencySourceTypeManualColor", "No Inner Mesh / Base Color");
    case EDWCTransparencySourceType::ExternalSkeletalMesh:
        return LOCTEXT("TransparencySourceTypeExternalMesh", "External Skeletal Mesh");
    case EDWCTransparencySourceType::SameMeshMaterialSlots:
    default:
        return LOCTEXT("TransparencySourceTypeSameMesh", "Single Skeletal Mesh / Inner Material Slots");
    }
}

FText GetBlueprintSourceRoleLabel(const EDWCTransparencyBlueprintSourceRole Role)
{
    return Role == EDWCTransparencyBlueprintSourceRole::BlockerOnly
        ? LOCTEXT("BlueprintSourceRoleBlocker", "Blocker Only")
        : LOCTEXT("BlueprintSourceRoleReveal", "Reveal Source");
}

const FWetClothingBakedTransparencyMap* FindExactBakedMap(
    const UWetClothingAsset* Asset,
    const FWetClothingTransparencyLayerData* Layer)
{
    if (Asset == nullptr || Layer == nullptr)
    {
        return nullptr;
    }

    return Layer->BakedMaps.FindByPredicate(
        [Layer](const FWetClothingBakedTransparencyMap& Candidate)
        {
            return Candidate.MaterialSlotIndex == Layer->TargetSurface.OuterMaterialSlotIndex &&
                   Candidate.TransparencyMap != nullptr;
        });
}

float RadiusUVToSizeCm(const float RadiusUV)
{
    return FMath::Clamp(RadiusUV / UVPerCm, MinBrushSizeCm, MaxBrushSizeCm);
}

float SizeCmToRadiusUV(const float SizeCm)
{
    return FMath::Clamp(SizeCm, MinBrushSizeCm, MaxBrushSizeCm) * UVPerCm;
}

FText FormatBrushSizeCm(const float SizeCm)
{
    FNumberFormattingOptions Options;
    Options.MinimumFractionalDigits = 0;
    Options.MaximumFractionalDigits = SizeCm < 10.0f ? 1 : 0;
    return FText::Format(
        LOCTEXT("TransparencyBrushSizeCmFormat", "{0} cm"),
        FText::AsNumber(SizeCm, &Options));
}
}

#undef LOCTEXT_NAMESPACE
