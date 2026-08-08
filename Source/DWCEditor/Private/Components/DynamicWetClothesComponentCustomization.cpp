// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "Components/DynamicWetClothesComponentCustomization.h"

#include "Async/Async.h"
#include "Components/DynamicWetClothesComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DataAssets/WetClothingAsset.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "IDetailGroup.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DynamicWetClothesComponentCustomization"

namespace
{
    UBlueprint* FindOwningBlueprint(const UDynamicWetClothesComponent* DWC)
    {
        if (DWC == nullptr)
        {
            return nullptr;
        }
        if (UBlueprint* Blueprint = DWC->GetTypedOuter<UBlueprint>())
        {
            return Blueprint;
        }
        if (const AActor* Owner = DWC->GetOwner())
        {
            return Cast<UBlueprint>(Owner->GetClass()->ClassGeneratedBy);
        }
        if (const UClass* OuterClass = DWC->GetTypedOuter<UClass>())
        {
            return Cast<UBlueprint>(OuterClass->ClassGeneratedBy);
        }
        return nullptr;
    }

    void CollectCandidateMeshes(const UDynamicWetClothesComponent* DWC, TArray<USkeletalMeshComponent*>& OutCandidates)
    {
        OutCandidates.Reset();
        if (DWC == nullptr)
        {
            return;
        }

        auto AddOwnerMeshes = [&OutCandidates](AActor* Owner)
        {
            if (Owner == nullptr)
            {
                return;
            }
            TArray<USkeletalMeshComponent*> OwnerMeshes;
            Owner->GetComponents<USkeletalMeshComponent>(OwnerMeshes);
            for (USkeletalMeshComponent* Mesh : OwnerMeshes)
            {
                OutCandidates.AddUnique(Mesh);
            }
        };

        if (!DWC->IsTemplate())
        {
            AddOwnerMeshes(DWC->GetOwner());
            return;
        }

        UBlueprint* Blueprint = FindOwningBlueprint(DWC);
        if (Blueprint != nullptr && Blueprint->SimpleConstructionScript != nullptr)
        {
            for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
            {
                if (Node != nullptr)
                {
                    if (USkeletalMeshComponent* MeshTemplate = Cast<USkeletalMeshComponent>(Node->ComponentTemplate))
                    {
                        OutCandidates.AddUnique(MeshTemplate);
                    }
                }
            }
        }

        if (Blueprint != nullptr && Blueprint->GeneratedClass != nullptr)
        {
            AddOwnerMeshes(Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject()));
        }
        AddOwnerMeshes(DWC->GetOwner());
    }

    FText MeshPathText(const UObject* Object)
    {
        return Object != nullptr ? FText::FromString(Object->GetPathName()) : LOCTEXT("None", "None");
    }

    TSharedPtr<IPropertyHandle> GetSettingsChild(
        const TSharedRef<IPropertyHandle>& SettingsHandle,
        const FName                        PropertyName)
    {
        return SettingsHandle->GetChildHandle(PropertyName);
    }

    void AddDirectProperty(
        IDetailCategoryBuilder&            Category,
        const TSharedRef<IPropertyHandle>& Handle)
    {
        Category.AddProperty(Handle);
    }

    void AddSettingsProperty(
        IDetailCategoryBuilder&            Category,
        const TSharedPtr<IPropertyHandle>& Handle)
    {
        if (Handle.IsValid() && Handle->IsValidHandle())
        {
            Category.AddProperty(Handle.ToSharedRef());
        }
    }

    void AddSettingsProperty(
        IDetailGroup&                      Group,
        const TSharedPtr<IPropertyHandle>& Handle)
    {
        if (Handle.IsValid() && Handle->IsValidHandle())
        {
            Group.AddPropertyRow(Handle.ToSharedRef());
        }
    }
} // namespace

TSharedRef<IDetailCustomization> FDynamicWetClothesComponentCustomization::MakeInstance()
{
    return MakeShared<FDynamicWetClothesComponentCustomization>();
}

FDynamicWetClothesComponentCustomization::~FDynamicWetClothesComponentCustomization()
{
    if (ObjectPropertyChangedHandle.IsValid())
    {
        FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(ObjectPropertyChangedHandle);
    }
}

void FDynamicWetClothesComponentCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
    TArray<TWeakObjectPtr<UObject>> Objects;
    DetailBuilder.GetObjectsBeingCustomized(Objects);
    if (Objects.Num() != 1)
    {
        return;
    }

    UDynamicWetClothesComponent* DWC = Cast<UDynamicWetClothesComponent>(Objects[0].Get());
    Component = DWC;
    PropertyUtilities = DetailBuilder.GetPropertyUtilities();
    if (DWC == nullptr)
    {
        return;
    }

    if (!ObjectPropertyChangedHandle.IsValid())
    {
        ObjectPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(
            this,
            &FDynamicWetClothesComponentCustomization::HandleObjectPropertyChanged);
    }
    RebuildBindingStatus();

    const TSharedRef<IPropertyHandle> AssetHandle = DetailBuilder.GetProperty(
        GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, WetClothingAsset));
    const TSharedRef<IPropertyHandle> SettingsHandle = DetailBuilder.GetProperty(
        GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, WetnessSettings));
    const TSharedRef<IPropertyHandle> SimulationModeHandle = DetailBuilder.GetProperty(
        GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, SimulationMode));
    const TSharedRef<IPropertyHandle> MaxContactsHandle = DetailBuilder.GetProperty(
        GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, MaxWetContactsPerFrame));
    const TSharedRef<IPropertyHandle> WetPartDebugHandle = DetailBuilder.GetProperty(
        GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, bShowWetPartDebugColors));
    const TSharedRef<IPropertyHandle> SurfaceDebugHandle = DetailBuilder.GetProperty(
        GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, bShowSurfaceWaterDebugColors));

    AssetHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateRaw(this, &FDynamicWetClothesComponentCustomization::RequestRefresh));
    SimulationModeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateRaw(this, &FDynamicWetClothesComponentCustomization::RequestRefresh));

    // The struct is intentionally not expanded automatically. Only the supported shipping controls are re-added below.
    DetailBuilder.HideProperty(SettingsHandle);

    IDetailCategoryBuilder& SetupCategory = DetailBuilder.EditCategory(
        TEXT("Setup"), LOCTEXT("SetupCategory", "Setup"), ECategoryPriority::Important);
    SetupCategory.SetSortOrder(0);
    AddDirectProperty(SetupCategory, AssetHandle);

    if (GetBindingWarningVisibility() == EVisibility::Visible)
    {
        SetupCategory.AddCustomRow(LOCTEXT("MeshValidationFilter", "Wet Clothing Asset Mesh Validation"))
            .WholeRowContent()
                [SNew(SBorder)
                     .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
                     .Padding(8.0f)
                         [SNew(SHorizontalBox) + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.0f, 1.0f, 8.0f, 0.0f)[SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.WarningWithColor")))] + SHorizontalBox::Slot().FillWidth(1.0f)[SNew(STextBlock).AutoWrapText(true).Text(this, &FDynamicWetClothesComponentCustomization::GetBindingWarningText)]]];
    }

    IDetailCategoryBuilder& SimulationCategory = DetailBuilder.EditCategory(
        TEXT("Simulation"), LOCTEXT("SimulationCategory", "Simulation"), ECategoryPriority::Important);
    SimulationCategory.SetSortOrder(10);
    AddDirectProperty(SimulationCategory, SimulationModeHandle);
    AddSettingsProperty(SimulationCategory, GetSettingsChild(SettingsHandle, GET_MEMBER_NAME_CHECKED(FWetClothingSettings, WetnessUpdateInterval)));
    AddSettingsProperty(SimulationCategory, GetSettingsChild(SettingsHandle, GET_MEMBER_NAME_CHECKED(FWetClothingSettings, MaxWetness)));
    AddSettingsProperty(SimulationCategory, GetSettingsChild(SettingsHandle, GET_MEMBER_NAME_CHECKED(FWetClothingSettings, DryRateScale)));
    AddSettingsProperty(SimulationCategory, GetSettingsChild(SettingsHandle, GET_MEMBER_NAME_CHECKED(FWetClothingSettings, WetnessDryHoldDuration)));

    const bool bGPU = DWC->SimulationMode == EDWCSimulationMode::WetnessMapGPU;
    if (!bGPU)
    {
        AddSettingsProperty(SimulationCategory, GetSettingsChild(SettingsHandle, GET_MEMBER_NAME_CHECKED(FWetClothingSettings, CapillaryImmediateAbsorptionFraction)));
        AddSettingsProperty(SimulationCategory, GetSettingsChild(SettingsHandle, GET_MEMBER_NAME_CHECKED(FWetClothingSettings, CrossWetPartSpreadScale)));
    }

    IDetailCategoryBuilder& InputCategory = DetailBuilder.EditCategory(
        TEXT("Input"), LOCTEXT("InputCategory", "Input"), ECategoryPriority::Important);
    InputCategory.SetSortOrder(20);
    IDetailGroup& ContactGroup = InputCategory.AddGroup(TEXT("DWCContactInput"), LOCTEXT("ContactGroup", "Contact"), false, true);
    AddSettingsProperty(ContactGroup, GetSettingsChild(SettingsHandle, GET_MEMBER_NAME_CHECKED(FWetClothingSettings, WetContactBackfaceDepthTolerance)));
    AddSettingsProperty(ContactGroup, GetSettingsChild(SettingsHandle, GET_MEMBER_NAME_CHECKED(FWetClothingSettings, WetContactBackfaceDepthRadiusScale)));

    IDetailGroup& AreaGroup = InputCategory.AddGroup(TEXT("DWCAreaInput"), LOCTEXT("AreaGroup", "Area"), false, true);
    AddSettingsProperty(AreaGroup, GetSettingsChild(SettingsHandle, GET_MEMBER_NAME_CHECKED(FWetClothingSettings, AreaExposureMin)));
    AddSettingsProperty(AreaGroup, GetSettingsChild(SettingsHandle, GET_MEMBER_NAME_CHECKED(FWetClothingSettings, AreaExposureMax)));
    AddSettingsProperty(AreaGroup, GetSettingsChild(SettingsHandle, GET_MEMBER_NAME_CHECKED(FWetClothingSettings, AreaExposureMinInfluence)));

    IDetailCategoryBuilder& PerformanceCategory = DetailBuilder.EditCategory(
        TEXT("Performance"), LOCTEXT("PerformanceCategory", "Performance"), ECategoryPriority::Default);
    PerformanceCategory.SetSortOrder(30);
    AddDirectProperty(PerformanceCategory, MaxContactsHandle);
    if (!bGPU)
    {
        AddSettingsProperty(PerformanceCategory, GetSettingsChild(SettingsHandle, GET_MEMBER_NAME_CHECKED(FWetClothingSettings, MaxPendingWetnessVerticesPerUpdate)));
    }
    AddSettingsProperty(PerformanceCategory, GetSettingsChild(SettingsHandle, GET_MEMBER_NAME_CHECKED(FWetClothingSettings, MinPendingWetnessAmount)));
    const TSharedPtr<IPropertyHandle> RenderIntervalHandle = GetSettingsChild(
        SettingsHandle, GET_MEMBER_NAME_CHECKED(FWetClothingSettings, WetnessRenderUpdateInterval));
    if (!bGPU)
    {
        IDetailCategoryBuilder& CPURenderingCategory = DetailBuilder.EditCategory(
            TEXT("CPU Rendering"), LOCTEXT("CPURenderingCategory", "CPU Rendering"), ECategoryPriority::Default);
        CPURenderingCategory.SetSortOrder(40);
        AddSettingsProperty(CPURenderingCategory, RenderIntervalHandle);
    }

    IDetailCategoryBuilder& DebugCategory = DetailBuilder.EditCategory(
        TEXT("Debug"), LOCTEXT("DebugCategory", "Debug"), ECategoryPriority::Default);
    DebugCategory.SetSortOrder(50);
    AddDirectProperty(DebugCategory, WetPartDebugHandle);
    if (bGPU)
    {
        AddDirectProperty(DebugCategory, SurfaceDebugHandle);
    }
    else
    {
        DetailBuilder.HideProperty(SurfaceDebugHandle);
    }

    // Hide implementation-oriented categories from the public Details panel.
    DetailBuilder.HideCategory(TEXT("Wetness"));
    DetailBuilder.HideCategory(TEXT("Wetness|Visual"));
    DetailBuilder.HideCategory(TEXT("Wetness|Wrinkle"));
    DetailBuilder.HideCategory(TEXT("Wetness|Transparency"));
    DetailBuilder.HideCategory(TEXT("Wetness|Surface"));
    DetailBuilder.HideCategory(TEXT("Wetness|Contact"));
}

void FDynamicWetClothesComponentCustomization::RebuildBindingStatus()
{
    bHasBindingStatus = false;
    CachedBindingStatus = FBindingStatus();

    UDynamicWetClothesComponent* DWC = Component.Get();
    if (DWC == nullptr || DWC->WetClothingAsset == nullptr)
    {
        return;
    }

    UWetClothingAsset* Asset = DWC->WetClothingAsset;
    CachedBindingStatus.Asset = Asset;
    bHasBindingStatus = true;

    if (!Asset->IsCurrentAssetDataVersion())
    {
        CachedBindingStatus.State = EBindingState::UnsupportedAssetVersion;
        return;
    }

    USkeletalMesh* SourceMesh = Asset->GetSourceSkeletalMesh();
    USkeletalMesh* RequiredMesh = Asset->GetDWCSkeletalMesh();
    CachedBindingStatus.SourceMesh = SourceMesh;
    CachedBindingStatus.RequiredMesh = RequiredMesh;

    if (SourceMesh == nullptr)
    {
        CachedBindingStatus.State = EBindingState::MissingSourceMesh;
        return;
    }
    if (RequiredMesh == nullptr)
    {
        CachedBindingStatus.State = EBindingState::MissingDWCMesh;
        return;
    }

    TArray<USkeletalMeshComponent*> Candidates;
    CollectCandidateMeshes(DWC, Candidates);

    USkeletalMeshComponent* SourceCandidate = nullptr;
    for (USkeletalMeshComponent* Candidate : Candidates)
    {
        if (Candidate == nullptr)
        {
            continue;
        }

        USkeletalMesh* CurrentMesh = Candidate->GetSkeletalMeshAsset();
        if (CurrentMesh == RequiredMesh)
        {
            CachedBindingStatus.MeshComponent = Candidate;
            CachedBindingStatus.CurrentMesh = CurrentMesh;
            CachedBindingStatus.State = EBindingState::Ready;
            return;
        }
        if (SourceCandidate == nullptr && CurrentMesh == SourceMesh)
        {
            SourceCandidate = Candidate;
        }
    }

    if (SourceCandidate != nullptr)
    {
        CachedBindingStatus.MeshComponent = SourceCandidate;
        CachedBindingStatus.CurrentMesh = SourceMesh;
        CachedBindingStatus.State = EBindingState::SourceMeshInUse;
        return;
    }

    CachedBindingStatus.State = EBindingState::NoMatchingComponent;
}

void FDynamicWetClothesComponentCustomization::RequestRefresh()
{
    RebuildBindingStatus();
    if (const TSharedPtr<IPropertyUtilities> Utilities = PropertyUtilities.Pin())
    {
        Utilities->ForceRefresh();
    }
}

void FDynamicWetClothesComponentCustomization::HandleObjectPropertyChanged(
    UObject*               ChangedObject,
    FPropertyChangedEvent& PropertyChangedEvent)
{
    UDynamicWetClothesComponent* DWC = Component.Get();
    if (DWC == nullptr || ChangedObject == nullptr || ChangedObject == DWC)
    {
        return;
    }

    bool bAffectsBinding = ChangedObject == DWC->WetClothingAsset;
    if (!bAffectsBinding)
    {
        if (USkeletalMeshComponent* ChangedMeshComponent = Cast<USkeletalMeshComponent>(ChangedObject))
        {
            TArray<USkeletalMeshComponent*> Candidates;
            CollectCandidateMeshes(DWC, Candidates);
            bAffectsBinding = Candidates.Contains(ChangedMeshComponent);
        }
    }
    if (!bAffectsBinding && ChangedObject == FindOwningBlueprint(DWC))
    {
        bAffectsBinding = true;
    }

    if (bAffectsBinding)
    {
        const TWeakPtr<IPropertyUtilities> WeakUtilities = PropertyUtilities;
        AsyncTask(ENamedThreads::GameThread, [WeakUtilities]()
                  {
            if (const TSharedPtr<IPropertyUtilities> Utilities = WeakUtilities.Pin())
            {
                Utilities->ForceRefresh();
            } });
    }
}

FText FDynamicWetClothesComponentCustomization::GetBindingWarningText() const
{
    if (!bHasBindingStatus)
    {
        return FText::GetEmpty();
    }

    switch (CachedBindingStatus.State)
    {
    case EBindingState::Ready:
        return FText::GetEmpty();
    case EBindingState::SourceMeshInUse:
        return FText::Format(
            LOCTEXT("SourceMeshInUse", "The assigned Skeletal Mesh does not match this Wet Clothing Asset. Assign the generated DWC mesh '{0}' to the target SkeletalMeshComponent."),
            MeshPathText(CachedBindingStatus.RequiredMesh.Get()));
    case EBindingState::UnsupportedAssetVersion:
        return FText::Format(
            LOCTEXT("UnsupportedAssetVersion", "This Wet Clothing Asset uses an unsupported schema version. Recreate or regenerate it with schema {0}."),
            FText::AsNumber(UWetClothingAsset::CurrentAssetDataVersion));
    case EBindingState::NoMatchingComponent:
        return FText::Format(
            LOCTEXT("NoMatch", "No SkeletalMeshComponent uses the source mesh '{0}' or required DWC mesh '{1}'."),
            MeshPathText(CachedBindingStatus.SourceMesh.Get()),
            MeshPathText(CachedBindingStatus.RequiredMesh.Get()));
    case EBindingState::MissingSourceMesh:
        return LOCTEXT("MissingSource", "This Wet Clothing Asset has no Source Skeletal Mesh.");
    case EBindingState::MissingDWCMesh:
        return LOCTEXT("MissingDWC", "Required DWC mesh is missing. Rebuild the Wet Clothing Asset.");
    default:
        return FText::GetEmpty();
    }
}

EVisibility FDynamicWetClothesComponentCustomization::GetBindingWarningVisibility() const
{
    return bHasBindingStatus && CachedBindingStatus.State != EBindingState::Ready
               ? EVisibility::Visible
               : EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE
