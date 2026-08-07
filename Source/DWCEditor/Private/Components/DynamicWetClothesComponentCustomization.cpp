//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "Components/DynamicWetClothesComponentCustomization.h"

#include "Components/DynamicWetClothesComponent.h"
#include "Async/Async.h"
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
#include "IPropertyUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
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

        // Include inherited/default components even when the current Blueprint also has SCS nodes.
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

}

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

    Component = Cast<UDynamicWetClothesComponent>(Objects[0].Get());
    PropertyUtilities = DetailBuilder.GetPropertyUtilities();
    if (!ObjectPropertyChangedHandle.IsValid())
    {
        ObjectPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(
            this,
            &FDynamicWetClothesComponentCustomization::HandleObjectPropertyChanged);
    }
    RebuildBindingStatuses();

    const TSharedRef<IPropertyHandle> AssetsHandle = DetailBuilder.GetProperty(
        GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, WetClothingAssets));
    AssetsHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateRaw(this, &FDynamicWetClothesComponentCustomization::RequestRefresh));
    AssetsHandle->SetOnChildPropertyValueChanged(FSimpleDelegate::CreateRaw(this, &FDynamicWetClothesComponentCustomization::RequestRefresh));
    if (const TSharedPtr<IPropertyHandleArray> ArrayHandle = AssetsHandle->AsArray())
    {
        ArrayHandle->SetOnNumElementsChanged(FSimpleDelegate::CreateRaw(this, &FDynamicWetClothesComponentCustomization::RequestRefresh));
    }

    IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(TEXT("Wetness"));
    Category.AddCustomRow(LOCTEXT("DWCBindingHeaderFilter", "DWC Mesh Bindings"))
    .WholeRowContent()
    [
        SNew(STextBlock)
        .Text(LOCTEXT("DWCBindingHeader", "Wet Clothing Asset Bindings"))
        .Font(FAppStyle::GetFontStyle(TEXT("DetailsView.CategoryFontStyle")))
    ];

    if (CachedBindingStatuses.IsEmpty())
    {
        Category.AddCustomRow(LOCTEXT("NoWCAFilter", "No Wet Clothing Assets"))
        .WholeRowContent()
        [
            SNew(STextBlock)
            .AutoWrapText(true)
            .Text(LOCTEXT("NoWCA", "Add one or more Wet Clothing Assets. Each matching SkeletalMeshComponent will become a runtime receiver."))
        ];
        return;
    }

    for (int32 BindingIndex = 0; BindingIndex < CachedBindingStatuses.Num(); ++BindingIndex)
    {
        Category.AddCustomRow(FText::FromString(FString::Printf(TEXT("DWC Binding %d"), BindingIndex)))
        .WholeRowContent()
        [
            SNew(SBorder)
            .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
            .Padding(8.0f)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .VAlign(VAlign_Top)
                    .Padding(0.0f, 1.0f, 8.0f, 0.0f)
                    [
                        SNew(SImage)
                        .Image(FAppStyle::GetBrush(TEXT("Icons.WarningWithColor")))
                        .Visibility(this, &FDynamicWetClothesComponentCustomization::GetBindingWarningVisibility, BindingIndex)
                    ]
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    [
                        SNew(STextBlock)
                        .AutoWrapText(true)
                        .Text(this, &FDynamicWetClothesComponentCustomization::GetBindingText, BindingIndex)
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .VAlign(VAlign_Center)
                    .Padding(8.0f, 0.0f, 0.0f, 0.0f)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("Apply", "Apply"))
                        .Visibility(this, &FDynamicWetClothesComponentCustomization::GetBindingApplyVisibility, BindingIndex)
                        .IsEnabled(this, &FDynamicWetClothesComponentCustomization::CanApplyBinding, BindingIndex)
                        .OnClicked(this, &FDynamicWetClothesComponentCustomization::HandleApplyBinding, BindingIndex)
                    ]
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(24.0f, 4.0f, 0.0f, 0.0f)
                [
                    SNew(STextBlock)
                    .Text(this, &FDynamicWetClothesComponentCustomization::GetBindingStateText, BindingIndex)
                    .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                ]
            ]
        ];
    }

    Category.AddCustomRow(LOCTEXT("ApplyAllFilter", "Apply All DWC Meshes"))
    .WholeRowContent()
    [
        SNew(SButton)
        .Text(LOCTEXT("ApplyAll", "Apply All"))
        .ToolTipText(LOCTEXT("ApplyAllTooltip", "Replace every unambiguous Source Skeletal Mesh reference with the DWC Skeletal Mesh required by its WCA."))
        .IsEnabled(this, &FDynamicWetClothesComponentCustomization::CanApplyAll)
        .OnClicked(this, &FDynamicWetClothesComponentCustomization::HandleApplyAll)
    ];
}

void FDynamicWetClothesComponentCustomization::RebuildBindingStatuses()
{
    CachedBindingStatuses.Reset();
    UDynamicWetClothesComponent* DWC = Component.Get();
    if (DWC == nullptr)
    {
        return;
    }

    TArray<USkeletalMeshComponent*> Candidates;
    CollectCandidateMeshes(DWC, Candidates);
    TSet<UWetClothingAsset*> SeenAssets;

    for (UWetClothingAsset* Asset : DWC->WetClothingAssets)
    {
        if (Asset == nullptr)
        {
            FBindingStatus& Status = CachedBindingStatuses.AddDefaulted_GetRef();
            Status.State = EBindingState::MissingAsset;
            continue;
        }

        if (SeenAssets.Contains(Asset))
        {
            FBindingStatus& Status = CachedBindingStatuses.AddDefaulted_GetRef();
            Status.Asset = Asset;
            Status.State = EBindingState::DuplicateAsset;
            continue;
        }
        SeenAssets.Add(Asset);

        if (!Asset->IsCurrentAssetDataVersion())
        {
            FBindingStatus& Status = CachedBindingStatuses.AddDefaulted_GetRef();
            Status.Asset = Asset;
            Status.State = EBindingState::UnsupportedAssetVersion;
            continue;
        }

        USkeletalMesh* SourceMesh = Asset->GetSourceSkeletalMesh();
        USkeletalMesh* RequiredMesh = Asset->GetDWCSkeletalMesh();
        if (SourceMesh == nullptr)
        {
            FBindingStatus& Status = CachedBindingStatuses.AddDefaulted_GetRef();
            Status.Asset = Asset;
            Status.RequiredMesh = RequiredMesh;
            Status.State = EBindingState::MissingSourceMesh;
            continue;
        }
        if (RequiredMesh == nullptr)
        {
            FBindingStatus& Status = CachedBindingStatuses.AddDefaulted_GetRef();
            Status.Asset = Asset;
            Status.SourceMesh = SourceMesh;
            Status.State = EBindingState::MissingDWCMesh;
            continue;
        }

        bool bAddedMatch = false;
        for (USkeletalMeshComponent* Candidate : Candidates)
        {
            if (Candidate == nullptr)
            {
                continue;
            }
            USkeletalMesh* CurrentMesh = Candidate->GetSkeletalMeshAsset();
            if (CurrentMesh != RequiredMesh && CurrentMesh != SourceMesh)
            {
                continue;
            }

            FBindingStatus& Status = CachedBindingStatuses.AddDefaulted_GetRef();
            Status.Asset = Asset;
            Status.MeshComponent = Candidate;
            Status.CurrentMesh = CurrentMesh;
            Status.SourceMesh = SourceMesh;
            Status.RequiredMesh = RequiredMesh;
            Status.State = CurrentMesh == RequiredMesh ? EBindingState::Ready : EBindingState::NeedsApply;
            bAddedMatch = true;
        }

        if (!bAddedMatch)
        {
            FBindingStatus& Status = CachedBindingStatuses.AddDefaulted_GetRef();
            Status.Asset = Asset;
            Status.SourceMesh = SourceMesh;
            Status.RequiredMesh = RequiredMesh;
            Status.State = EBindingState::NoMatchingComponent;
        }
    }

    TMap<USkeletalMeshComponent*, UWetClothingAsset*> FirstClaim;
    TSet<USkeletalMeshComponent*> ConflictingComponents;
    for (const FBindingStatus& Status : CachedBindingStatuses)
    {
        USkeletalMeshComponent* MeshComponent = Status.MeshComponent.Get();
        UWetClothingAsset* Asset = Status.Asset.Get();
        if (MeshComponent == nullptr || Asset == nullptr ||
            (Status.State != EBindingState::Ready && Status.State != EBindingState::NeedsApply))
        {
            continue;
        }

        if (UWetClothingAsset** Existing = FirstClaim.Find(MeshComponent))
        {
            if (*Existing != Asset)
            {
                ConflictingComponents.Add(MeshComponent);
            }
        }
        else
        {
            FirstClaim.Add(MeshComponent, Asset);
        }
    }

    for (FBindingStatus& Status : CachedBindingStatuses)
    {
        if (ConflictingComponents.Contains(Status.MeshComponent.Get()))
        {
            Status.State = EBindingState::ConflictingAsset;
        }
    }
}

void FDynamicWetClothesComponentCustomization::RequestRefresh()
{
    RebuildBindingStatuses();
    if (const TSharedPtr<IPropertyUtilities> Utilities = PropertyUtilities.Pin())
    {
        Utilities->ForceRefresh();
    }
}

void FDynamicWetClothesComponentCustomization::HandleObjectPropertyChanged(
    UObject* ChangedObject,
    FPropertyChangedEvent& PropertyChangedEvent)
{
    UDynamicWetClothesComponent* DWC = Component.Get();
    if (DWC == nullptr || ChangedObject == nullptr || ChangedObject == DWC || bApplyingBinding)
    {
        return;
    }

    bool bAffectsBindings = DWC->WetClothingAssets.ContainsByPredicate(
        [ChangedObject](const TObjectPtr<UWetClothingAsset>& Asset)
        {
            return Asset.Get() == ChangedObject;
        });

    if (!bAffectsBindings)
    {
        for (const FBindingStatus& Status : CachedBindingStatuses)
        {
            if (Status.MeshComponent.Get() == ChangedObject)
            {
                bAffectsBindings = true;
                break;
            }
        }
    }

    if (!bAffectsBindings && ChangedObject == FindOwningBlueprint(DWC))
    {
        bAffectsBindings = true;
    }

    if (bAffectsBindings)
    {
        const TWeakPtr<IPropertyUtilities> WeakUtilities = PropertyUtilities;
        AsyncTask(ENamedThreads::GameThread, [WeakUtilities]()
        {
            if (const TSharedPtr<IPropertyUtilities> Utilities = WeakUtilities.Pin())
            {
                Utilities->ForceRefresh();
            }
        });
    }
}

FText FDynamicWetClothesComponentCustomization::GetBindingText(const int32 BindingIndex) const
{
    if (!CachedBindingStatuses.IsValidIndex(BindingIndex))
    {
        return FText::GetEmpty();
    }
    const FBindingStatus& Status = CachedBindingStatuses[BindingIndex];
    return FText::Format(
        LOCTEXT("BindingText", "WCA: {0}\nComponent: {1}"),
        MeshPathText(Status.Asset.Get()),
        Status.MeshComponent.IsValid() ? MeshPathText(Status.MeshComponent.Get()) : LOCTEXT("NoMatchingComponentName", "Not found"));
}

FText FDynamicWetClothesComponentCustomization::GetBindingStateText(const int32 BindingIndex) const
{
    if (!CachedBindingStatuses.IsValidIndex(BindingIndex))
    {
        return FText::GetEmpty();
    }
    const FBindingStatus& Status = CachedBindingStatuses[BindingIndex];
    switch (Status.State)
    {
    case EBindingState::Ready:
        return FText::Format(LOCTEXT("Ready", "Ready: {0}"), MeshPathText(Status.RequiredMesh.Get()));
    case EBindingState::NeedsApply:
        return FText::Format(LOCTEXT("NeedsApply", "Current: {0}\nRequired: {1}"), MeshPathText(Status.CurrentMesh.Get()), MeshPathText(Status.RequiredMesh.Get()));
    case EBindingState::MissingAsset:
        return LOCTEXT("MissingAsset", "This array entry has no Wet Clothing Asset assigned.");
    case EBindingState::UnsupportedAssetVersion:
        return FText::Format(
            LOCTEXT("UnsupportedAssetVersion", "Unsupported WCA schema version. Current plugin version requires schema {0}. Recreate or regenerate this asset."),
            FText::AsNumber(UWetClothingAsset::CurrentAssetDataVersion));
    case EBindingState::NoMatchingComponent:
        return FText::Format(LOCTEXT("NoMatch", "No component uses Source '{0}' or DWC mesh '{1}'."), MeshPathText(Status.SourceMesh.Get()), MeshPathText(Status.RequiredMesh.Get()));
    case EBindingState::MissingSourceMesh:
        return LOCTEXT("MissingSource", "The WCA has no Source Skeletal Mesh.");
    case EBindingState::MissingDWCMesh:
        return LOCTEXT("MissingDWC", "The WCA has no generated DWC Skeletal Mesh.");
    case EBindingState::DuplicateAsset:
        return LOCTEXT("Duplicate", "This WCA is registered more than once. Duplicate entries are ignored.");
    case EBindingState::ConflictingAsset:
        return LOCTEXT("Conflict", "Multiple WCA assets target this same SkeletalMeshComponent. Remove the conflicting entry before applying.");
    default:
        return FText::GetEmpty();
    }
}

EVisibility FDynamicWetClothesComponentCustomization::GetBindingWarningVisibility(const int32 BindingIndex) const
{
    return CachedBindingStatuses.IsValidIndex(BindingIndex) && CachedBindingStatuses[BindingIndex].State != EBindingState::Ready
        ? EVisibility::Visible
        : EVisibility::Collapsed;
}

EVisibility FDynamicWetClothesComponentCustomization::GetBindingApplyVisibility(const int32 BindingIndex) const
{
    return CanApplyBinding(BindingIndex) ? EVisibility::Visible : EVisibility::Collapsed;
}

bool FDynamicWetClothesComponentCustomization::CanApplyBinding(const int32 BindingIndex) const
{
    return CachedBindingStatuses.IsValidIndex(BindingIndex) &&
           CachedBindingStatuses[BindingIndex].State == EBindingState::NeedsApply &&
           CachedBindingStatuses[BindingIndex].MeshComponent.IsValid() &&
           CachedBindingStatuses[BindingIndex].RequiredMesh.IsValid();
}

bool FDynamicWetClothesComponentCustomization::CanApplyAll() const
{
    for (int32 Index = 0; Index < CachedBindingStatuses.Num(); ++Index)
    {
        if (CanApplyBinding(Index))
        {
            return true;
        }
    }
    return false;
}

bool FDynamicWetClothesComponentCustomization::ApplyBinding(const int32 BindingIndex, const bool bUseTransaction)
{
    if (!CanApplyBinding(BindingIndex))
    {
        return false;
    }

    FBindingStatus& Status = CachedBindingStatuses[BindingIndex];
    USkeletalMeshComponent* Target = Status.MeshComponent.Get();
    USkeletalMesh* Required = Status.RequiredMesh.Get();
    UDynamicWetClothesComponent* DWC = Component.Get();
    if (Target == nullptr || Required == nullptr || DWC == nullptr)
    {
        return false;
    }

    TUniquePtr<FScopedTransaction> Transaction;
    if (bUseTransaction)
    {
        Transaction = MakeUnique<FScopedTransaction>(LOCTEXT("ApplyRuntimeMeshTransaction", "Apply DWC Skeletal Mesh"));
    }

    TGuardValue<bool> ApplyingGuard(bApplyingBinding, true);
    UBlueprint* OwningBlueprint = DWC->IsTemplate() ? FindOwningBlueprint(DWC) : nullptr;
    if (OwningBlueprint != nullptr)
    {
        OwningBlueprint->Modify();
    }
    Target->Modify();
    Target->SetSkeletalMeshAsset(Required);
    Target->PostEditChange();
    Target->MarkPackageDirty();
    if (OwningBlueprint != nullptr)
    {
        FBlueprintEditorUtils::MarkBlueprintAsModified(OwningBlueprint);
    }
    return true;
}

FReply FDynamicWetClothesComponentCustomization::HandleApplyBinding(const int32 BindingIndex)
{
    ApplyBinding(BindingIndex, true);
    RequestRefresh();
    return FReply::Handled();
}

FReply FDynamicWetClothesComponentCustomization::HandleApplyAll()
{
    const FScopedTransaction Transaction(LOCTEXT("ApplyAllRuntimeMeshesTransaction", "Apply All DWC Skeletal Meshes"));
    for (int32 Index = 0; Index < CachedBindingStatuses.Num(); ++Index)
    {
        ApplyBinding(Index, false);
    }
    RequestRefresh();
    return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
