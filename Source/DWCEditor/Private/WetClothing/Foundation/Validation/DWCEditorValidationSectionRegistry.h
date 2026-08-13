// Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationTypes.h"

enum class EWCAValidationSection : uint8
{
    Asset,
    DataUV,
    WetPart,
    RuntimeData,
    GeneratedMaterials,
    RenderProfileData,
    WrinkleMaps,
    TransparencyMaps,
    FailureDetails
};

enum class EDWCValidationSectionGroup : uint8
{
    Asset,
    Authoring,
    Runtime,
    GeneratedAssets,
    Internal
};

enum class EDWCValidationPresentationState : uint8
{
    Neutral,
    Success,
    Info,
    Warning,
    Error
};

struct FDWCValidationSectionDescriptor
{
    EWCAValidationSection Section = EWCAValidationSection::Asset;
    EDWCValidationSectionGroup Group = EDWCValidationSectionGroup::Asset;
    int32 StableOrder = 0;
    FText Title;
    FText NotApplicableReason;
    FName IconName;
    bool bUseDWCStyle = false;
    TArray<FText> Checks;
};

class FDWCEditorValidationSectionRegistry
{
  public:
    static TConstArrayView<FDWCValidationSectionDescriptor> GetSections();
    static const FDWCValidationSectionDescriptor* Find(EWCAValidationSection Section);
    static EWCAValidationSection MapDomain(EDWCEditorValidationDomain Domain);
    static EWCAValidationSection MapAction(EDWCEditorBuildAction Action);
    static int32 GetStatePriority(EDWCEditorValidationOverallState State);
    static EDWCValidationPresentationState MapPresentationState(
        EDWCEditorValidationOverallState State);
    static FText GetStateLabel(EDWCEditorValidationOverallState State);
    static FText GetStateDescription(EDWCEditorValidationOverallState State);
    static FText GetGroupTitle(EDWCValidationSectionGroup Group);
};
