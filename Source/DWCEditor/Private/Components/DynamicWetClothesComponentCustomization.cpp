#include "Components/DynamicWetClothesComponentCustomization.h"

#include "Components/DynamicWetClothesComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"
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

    void CollectOwnerTargetCandidates(
        const UDynamicWetClothesComponent* DWC,
        TArray<USkeletalMeshComponent*>& OutCandidates)
    {
        if (DWC == nullptr)
        {
            return;
        }

        if (AActor* Owner = DWC->GetOwner())
        {
            TArray<USkeletalMeshComponent*> OwnerMeshes;
            Owner->GetComponents<USkeletalMeshComponent>(OwnerMeshes);
            for (USkeletalMeshComponent* Mesh : OwnerMeshes)
            {
                OutCandidates.AddUnique(Mesh);
            }
        }
    }

    void CollectBlueprintTemplateTargetCandidates(
        const UDynamicWetClothesComponent* DWC,
        TArray<USkeletalMeshComponent*>& OutCandidates)
    {
        if (DWC == nullptr || !DWC->IsTemplate())
        {
            return;
        }

        UBlueprint* Blueprint = FindOwningBlueprint(DWC);
        if (Blueprint == nullptr)
        {
            return;
        }

        if (Blueprint->SimpleConstructionScript != nullptr)
        {
            for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
            {
                if (Node == nullptr)
                {
                    continue;
                }

                if (USkeletalMeshComponent* MeshTemplate = Cast<USkeletalMeshComponent>(Node->ComponentTemplate))
                {
                    OutCandidates.AddUnique(MeshTemplate);
                }
            }
        }

        if (UClass* GeneratedClass = Blueprint->GeneratedClass)
        {
            if (AActor* DefaultActor = Cast<AActor>(GeneratedClass->GetDefaultObject()))
            {
                TArray<USkeletalMeshComponent*> DefaultActorMeshes;
                DefaultActor->GetComponents<USkeletalMeshComponent>(DefaultActorMeshes);
                for (USkeletalMeshComponent* Mesh : DefaultActorMeshes)
                {
                    OutCandidates.AddUnique(Mesh);
                }
            }
        }
    }

    void CollectCustomizationTargetCandidates(
        const UDynamicWetClothesComponent* DWC,
        TArray<USkeletalMeshComponent*>& OutCandidates)
    {
        if (DWC == nullptr)
        {
            return;
        }

        if (DWC->IsTemplate())
        {
            CollectBlueprintTemplateTargetCandidates(DWC, OutCandidates);
            CollectOwnerTargetCandidates(DWC, OutCandidates);
            return;
        }

        CollectOwnerTargetCandidates(DWC, OutCandidates);
    }

    FText MakeTargetComponentText(const USkeletalMeshComponent* Target)
    {
        return FText::Format(
            LOCTEXT("TargetComponentFormat", "{0} -> {1}"),
            FText::FromString(GetNameSafe(Target)),
            FText::FromString(GetNameSafe(Target != nullptr ? Target->GetSkeletalMeshAsset() : nullptr)));
    }

    FText MakeCandidateListText(const UDynamicWetClothesComponent* DWC)
    {
        TArray<USkeletalMeshComponent*> Candidates;
        CollectCustomizationTargetCandidates(DWC, Candidates);
        if (Candidates.IsEmpty())
        {
            return LOCTEXT("NoCandidateComponents", "None");
        }

        TArray<FString> CandidateTexts;
        CandidateTexts.Reserve(Candidates.Num());
        for (const USkeletalMeshComponent* Candidate : Candidates)
        {
            CandidateTexts.Add(FText::Format(
                LOCTEXT("CandidateComponentFormat", "{0} -> {1}"),
                FText::FromString(GetNameSafe(Candidate)),
                FText::FromString(GetNameSafe(Candidate != nullptr ? Candidate->GetSkeletalMeshAsset() : nullptr))).ToString());
        }

        return FText::FromString(FString::Join(CandidateTexts, TEXT(", ")));
    }

    USkeletalMeshComponent* SelectAutomaticTargetFromCandidates(
        const UDynamicWetClothesComponent* DWC,
        const TArray<USkeletalMeshComponent*>& Candidates)
    {
        USkeletalMeshComponent* FirstMesh = nullptr;
        USkeletalMeshComponent* FirstValidMesh = nullptr;
        USkeletalMesh* RequiredRuntimeMesh = DWC != nullptr ? DWC->GetRequiredRuntimeSkeletalMesh() : nullptr;

        for (USkeletalMeshComponent* Mesh : Candidates)
        {
            if (Mesh == nullptr)
            {
                continue;
            }

            if (FirstMesh == nullptr)
            {
                FirstMesh = Mesh;
            }
            if (FirstValidMesh == nullptr && Mesh->GetSkeletalMeshAsset() != nullptr)
            {
                FirstValidMesh = Mesh;
            }
            if (RequiredRuntimeMesh != nullptr && Mesh->GetSkeletalMeshAsset() == RequiredRuntimeMesh)
            {
                return Mesh;
            }
        }

        return FirstValidMesh != nullptr ? FirstValidMesh : FirstMesh;
    }

    USkeletalMeshComponent* ResolveBlueprintTemplateTarget(
        const UDynamicWetClothesComponent* DWC,
        UBlueprint** OutOwningBlueprint = nullptr)
    {
        if (OutOwningBlueprint != nullptr)
        {
            *OutOwningBlueprint = nullptr;
        }
        if (DWC == nullptr || !DWC->IsTemplate())
        {
            return nullptr;
        }

        UBlueprint* Blueprint = FindOwningBlueprint(DWC);
        if (OutOwningBlueprint != nullptr)
        {
            *OutOwningBlueprint = Blueprint;
        }

        TArray<USkeletalMeshComponent*> Candidates;
        CollectCustomizationTargetCandidates(DWC, Candidates);

        if (!DWC->TargetSkeletalMeshName.IsNone())
        {
            if (Blueprint != nullptr && Blueprint->SimpleConstructionScript != nullptr)
            {
                for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
                {
                    if (Node == nullptr)
                    {
                        continue;
                    }

                    USkeletalMeshComponent* MeshTemplate = Cast<USkeletalMeshComponent>(Node->ComponentTemplate);
                    if (MeshTemplate == nullptr)
                    {
                        continue;
                    }

                    if (Node->GetVariableName() == DWC->TargetSkeletalMeshName ||
                        MeshTemplate->GetFName() == DWC->TargetSkeletalMeshName)
                    {
                        return MeshTemplate;
                    }
                }
            }

            for (USkeletalMeshComponent* Mesh : Candidates)
            {
                if (Mesh != nullptr && Mesh->GetFName() == DWC->TargetSkeletalMeshName)
                {
                    return Mesh;
                }
            }
            return nullptr;
        }

        return SelectAutomaticTargetFromCandidates(DWC, Candidates);
    }

    USkeletalMeshComponent* ResolveCustomizationTarget(
        const UDynamicWetClothesComponent* DWC,
        UBlueprint** OutOwningBlueprint = nullptr)
    {
        if (OutOwningBlueprint != nullptr)
        {
            *OutOwningBlueprint = nullptr;
        }
        if (DWC == nullptr)
        {
            return nullptr;
        }

        if (DWC->IsTemplate())
        {
            return ResolveBlueprintTemplateTarget(DWC, OutOwningBlueprint);
        }
        return DWC->GetResolvedTargetSkeletalMeshComponent();
    }
}

TSharedRef<IDetailCustomization> FDynamicWetClothesComponentCustomization::MakeInstance()
{
    return MakeShared<FDynamicWetClothesComponentCustomization>();
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
    IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(TEXT("Wetness"));
    Category.AddCustomRow(LOCTEXT("RuntimeMeshWarningFilter", "DWC Runtime Mesh Warning"))
    .Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateRaw(
        this,
        &FDynamicWetClothesComponentCustomization::GetRuntimeMeshWarningVisibility)))
    .WholeRowContent()
    [
        SNew(SBorder)
        .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
        .BorderBackgroundColor(FLinearColor(1.0f, 0.66f, 0.08f, 0.16f))
        .Padding(8.0f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Top)
            .Padding(0.0f, 1.0f, 8.0f, 0.0f)
            [
                SNew(SImage)
                .Image(FAppStyle::GetBrush(TEXT("Icons.WarningWithColor")))
            ]
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .AutoWrapText(true)
                .Text(this, &FDynamicWetClothesComponentCustomization::GetRuntimeMeshWarningText)
            ]
        ]
    ];

    Category.AddCustomRow(LOCTEXT("RuntimeMeshFilter", "DWC Data UV"))
    .WholeRowContent()
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(2.0f)
        [
            SNew(STextBlock)
            .AutoWrapText(true)
            .Text(this, &FDynamicWetClothesComponentCustomization::GetStatusText)
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(2.0f, 6.0f, 2.0f, 2.0f)
        [
            SNew(SButton)
            .Text(this, &FDynamicWetClothesComponentCustomization::GetApplyButtonText)
            .ToolTipText(LOCTEXT(
                "ApplyTooltip",
                "Changes only the target SkeletalMeshComponent's mesh reference. The Source and generated Data UV payloads are not overwritten."))
            .IsEnabled(this, &FDynamicWetClothesComponentCustomization::CanApplyRuntimeMesh)
            .OnClicked(this, &FDynamicWetClothesComponentCustomization::HandleApplyRuntimeMesh)
        ]
    ];
}

FText FDynamicWetClothesComponentCustomization::GetStatusText() const
{
    const UDynamicWetClothesComponent* DWC = Component.Get();
    if (DWC == nullptr || DWC->WetClothingAsset == nullptr)
    {
        return LOCTEXT("NoAsset", "Assign a Wet Clothing Asset.");
    }
    USkeletalMeshComponent* Target = ResolveCustomizationTarget(DWC);
    USkeletalMesh* Required = DWC->GetRequiredRuntimeSkeletalMesh();
    if (Required == nullptr)
    {
        return LOCTEXT("NoGeneratedDataUV", "Wet Clothing Asset has no Runtime Mesh.");
    }
    if (Target == nullptr)
    {
        return FText::Format(
            LOCTEXT(
                "NoTarget",
                "Target SkeletalMeshComponent not found. Required: {0}."),
            FText::FromString(GetNameSafe(Required)));
    }
    if (Target->GetSkeletalMeshAsset() == Required)
    {
        return FText::Format(
            LOCTEXT("Ready", "Ready: {0}"),
            MakeTargetComponentText(Target));
    }
    return FText::Format(
        LOCTEXT("Mismatch", "Target: {0}\nRequired: {1}"),
        MakeTargetComponentText(Target),
        FText::FromString(GetNameSafe(Required)));
}

FText FDynamicWetClothesComponentCustomization::GetRuntimeMeshWarningText() const
{
    const UDynamicWetClothesComponent* DWC = Component.Get();
    if (DWC == nullptr || DWC->WetClothingAsset == nullptr)
    {
        return FText::GetEmpty();
    }

    USkeletalMeshComponent* Target = ResolveCustomizationTarget(DWC);
    if (Target == nullptr)
    {
        return FText::Format(
            LOCTEXT(
                "RuntimeMeshWarningNoTarget",
                "DWC target not found. Required: {0}. Candidates: {1}"),
            FText::FromString(GetNameSafe(DWC->GetRequiredRuntimeSkeletalMesh())),
            MakeCandidateListText(DWC));
    }

    USkeletalMesh* Required = DWC->GetRequiredRuntimeSkeletalMesh();
    if (Required == nullptr)
    {
        return FText::Format(
            LOCTEXT(
                "RuntimeMeshWarningNoRuntimeMesh",
                "Wet Clothing Asset '{0}' has no Runtime Mesh."),
            FText::FromString(GetNameSafe(DWC->WetClothingAsset)));
    }

    return FText::Format(
        LOCTEXT(
            "RuntimeMeshWarningMismatch",
            "Target mesh mismatch. Target: {0}. Required: {1}."),
        MakeTargetComponentText(Target),
        FText::FromString(GetNameSafe(Required)));
}

EVisibility FDynamicWetClothesComponentCustomization::GetRuntimeMeshWarningVisibility() const
{
    return HasRuntimeMeshWarning() ? EVisibility::Visible : EVisibility::Collapsed;
}

FText FDynamicWetClothesComponentCustomization::GetApplyButtonText() const
{
    const UDynamicWetClothesComponent* DWC = Component.Get();
    return DWC != nullptr && DWC->IsTemplate()
        ? LOCTEXT("ApplyBlueprint", "Change Blueprint Default Mesh to Runtime Mesh")
        : LOCTEXT("ApplyActor", "Change Current Actor Mesh to Runtime Mesh");
}

bool FDynamicWetClothesComponentCustomization::HasRuntimeMeshWarning() const
{
    const UDynamicWetClothesComponent* DWC = Component.Get();
    if (DWC == nullptr || DWC->WetClothingAsset == nullptr)
    {
        return false;
    }

    USkeletalMeshComponent* Target = ResolveCustomizationTarget(DWC);
    USkeletalMesh* Required = DWC->GetRequiredRuntimeSkeletalMesh();
    return Target == nullptr || Required == nullptr || Target->GetSkeletalMeshAsset() != Required;
}

bool FDynamicWetClothesComponentCustomization::HasRuntimeMeshMismatch() const
{
    const UDynamicWetClothesComponent* DWC = Component.Get();
    if (DWC == nullptr)
    {
        return false;
    }
    USkeletalMeshComponent* Target = ResolveCustomizationTarget(DWC);
    USkeletalMesh* Required = DWC->GetRequiredRuntimeSkeletalMesh();
    return Target != nullptr && Required != nullptr && Target->GetSkeletalMeshAsset() != Required;
}

bool FDynamicWetClothesComponentCustomization::CanApplyRuntimeMesh() const
{
    return HasRuntimeMeshMismatch();
}

FReply FDynamicWetClothesComponentCustomization::HandleApplyRuntimeMesh()
{
    UDynamicWetClothesComponent* DWC = Component.Get();
    if (DWC == nullptr)
    {
        return FReply::Handled();
    }
    USkeletalMeshComponent* Target = ResolveCustomizationTarget(DWC);
    USkeletalMesh* Required = DWC->GetRequiredRuntimeSkeletalMesh();
    if (Target == nullptr || Required == nullptr || Target->GetSkeletalMeshAsset() == Required)
    {
        return FReply::Handled();
    }

    UBlueprint* OwningBlueprint = nullptr;
    if (DWC->IsTemplate())
    {
        Target = ResolveCustomizationTarget(DWC, &OwningBlueprint);
        if (Target == nullptr)
        {
            return FReply::Handled();
        }
    }

    const FScopedTransaction Transaction(LOCTEXT("ApplyRuntimeMeshTransaction", "Change DWC Target to Runtime Mesh"));
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
    return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
