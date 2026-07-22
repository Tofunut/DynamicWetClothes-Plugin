#include "WetClothingAssetFactory.h"

#include "AssetThumbnail.h"
#include "AssetRegistry/AssetData.h"
#include "DataAssets/WetClothingAsset.h"
#include "DetailsViewArgs.h"
#include "Engine/SkeletalMesh.h"
#include "Framework/Application/SlateApplication.h"
#include "IDetailsView.h"
#include "Misc/MessageDialog.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "PropertyEditorDelegates.h"
#include "PropertyCustomizationHelpers.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "UObject/UnrealType.h"
#include "WetClothing/Asset/Setup/DWCDataUVBuildService.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WetClothingAssetFactory"

namespace
{
    constexpr float CreateDialogWidth = 720.0f;
    constexpr uint32 SourceMeshThumbnailSize = 112;
    constexpr int32 RecommendedDWCDataUVSelection = INDEX_NONE;

    int32 GetSkeletalMeshUVChannelCount(const USkeletalMesh* Mesh, const int32 LODIndex)
    {
        const FSkeletalMeshRenderData* RenderData = Mesh != nullptr ? Mesh->GetResourceForRendering() : nullptr;
        if (RenderData == nullptr || !RenderData->LODRenderData.IsValidIndex(LODIndex))
        {
            return 0;
        }

        return static_cast<int32>(RenderData->LODRenderData[LODIndex].GetNumTexCoords());
    }

    int32 GetSkeletalMeshLODCount(const USkeletalMesh* Mesh)
    {
        const FSkeletalMeshRenderData* RenderData = Mesh != nullptr ? Mesh->GetResourceForRendering() : nullptr;
        return RenderData != nullptr ? RenderData->LODRenderData.Num() : 0;
    }

    void ClampLODRangeForMesh(const USkeletalMesh* Mesh, int32& FirstLODIndex, int32& LastLODIndex)
    {
        const int32 LODCount = GetSkeletalMeshLODCount(Mesh);
        if (LODCount <= 0)
        {
            FirstLODIndex = 0;
            LastLODIndex = 0;
            return;
        }

        const int32 LastAvailableLODIndex = LODCount - 1;
        FirstLODIndex = FMath::Clamp(FirstLODIndex, 0, LastAvailableLODIndex);
        LastLODIndex = FMath::Clamp(LastLODIndex, FirstLODIndex, LastAvailableLODIndex);
    }

    FText BuildLODRangeInfoText(const USkeletalMesh* Mesh, const int32 FirstLODIndex, const int32 LastLODIndex)
    {
        const int32 LODCount = GetSkeletalMeshLODCount(Mesh);
        if (LODCount <= 0)
        {
            return LOCTEXT("LODRangeInfoNoMesh", "Select a source mesh to inspect available LODs.");
        }

        return FText::Format(
            LOCTEXT("LODRangeInfo", "Available LODs: LOD0 - LOD{0}. DWC Data UV and Original UV topology will be generated for LOD{1} - LOD{2}."),
            FText::AsNumber(LODCount - 1),
            FText::AsNumber(FirstLODIndex),
            FText::AsNumber(LastLODIndex));
    }

    int32 GetDefaultDWCDataUVChannelIndex(const USkeletalMesh* Mesh, const int32 OriginalUVChannelIndex)
    {
        const int32 UVChannelCount = GetSkeletalMeshUVChannelCount(Mesh, 0);
        return UVChannelCount > 0 ? FMath::Clamp(UVChannelCount, 0, 7) : FMath::Clamp(OriginalUVChannelIndex + 1, 0, 7);
    }

    FText BuildCreateDWCDataUVTargetText()
    {
        return LOCTEXT(
            "CreatePreparedMeshTargetText",
            "Prepared Mesh: DWC creates a dedicated skeletal mesh copy and writes all generated UV and topology data there. The source mesh remains untouched.");
    }

    FString BuildUVChannelList(const int32 FirstUVChannelIndex, const int32 LastUVChannelIndex)
    {
        FString Result;
        for (int32 UVChannelIndex = FirstUVChannelIndex; UVChannelIndex <= LastUVChannelIndex; ++UVChannelIndex)
        {
            if (!Result.IsEmpty())
            {
                Result += TEXT(", ");
            }
            Result += FString::Printf(TEXT("UV%d"), UVChannelIndex);
        }
        return Result;
    }

    FText BuildSourceMeshUVInfoText(const USkeletalMesh* Mesh)
    {
        if (Mesh == nullptr)
        {
            return LOCTEXT("SourceMeshUVInfoNoMesh", "Select a source mesh to inspect its LOD0 UV channels.");
        }

        const FSkeletalMeshRenderData* RenderData = Mesh->GetResourceForRendering();
        if (RenderData == nullptr || RenderData->LODRenderData.IsEmpty())
        {
            return LOCTEXT("SourceMeshUVInfoNoRenderData", "Render LOD data is unavailable for this mesh.");
        }

        const int32 UVChannelCount = static_cast<int32>(RenderData->LODRenderData[0].GetNumTexCoords());
        if (UVChannelCount <= 0)
        {
            return LOCTEXT("SourceMeshUVInfoNoUVs", "LOD0 has no UV channels available.");
        }

        return FText::Format(
            LOCTEXT("SourceMeshUVInfo", "LOD0 has {0} source UV channel(s): {1}. Choose the Original UV channel in the setup settings."),
            FText::AsNumber(UVChannelCount),
            FText::FromString(BuildUVChannelList(0, UVChannelCount - 1)));
    }

    bool IsExistingSourceUVChannel(const USkeletalMesh* Mesh, const int32 UVChannelIndex)
    {
        return UVChannelIndex >= 0 && UVChannelIndex < GetSkeletalMeshUVChannelCount(Mesh, 0);
    }

    bool IsDWCDataUVSelectionValid(
        const USkeletalMesh* Mesh,
        const int32 OriginalUVChannelIndex,
        const bool bUseRecommendedDWCDataUVChannel,
        const int32 SelectedUVChannelIndex)
    {
        if (Mesh == nullptr || !IsExistingSourceUVChannel(Mesh, OriginalUVChannelIndex))
        {
            return false;
        }

        const int32 EffectiveUVChannelIndex = bUseRecommendedDWCDataUVChannel
            ? GetDefaultDWCDataUVChannelIndex(Mesh, OriginalUVChannelIndex)
            : SelectedUVChannelIndex;
        return EffectiveUVChannelIndex >= 0 &&
               EffectiveUVChannelIndex <= 7 &&
               EffectiveUVChannelIndex != OriginalUVChannelIndex;
    }

    bool ConfirmExistingDataUVOverwrite(
        const USkeletalMesh* Mesh,
        const int32 OriginalUVChannelIndex,
        const int32 DataUVChannelIndex)
    {
        if (DataUVChannelIndex == OriginalUVChannelIndex)
        {
            FMessageDialog::Open(
                EAppMsgType::Ok,
                LOCTEXT(
                    "DataUVMatchesOriginalUVError",
                    "The DWC Data UV channel cannot be the same as the Original UV channel."));
            return false;
        }

        if (!IsExistingSourceUVChannel(Mesh, DataUVChannelIndex))
        {
            return true;
        }

        const FText Warning = FText::Format(
            LOCTEXT(
                "ConfirmExistingDataUVOverwrite",
                "UV{0} already contains UV data.\n\nCreating the DWC Data UV in this channel will replace the existing UV{0} data on the target mesh.\n\nContinue?"),
            FText::AsNumber(DataUVChannelIndex));
        return FMessageDialog::Open(EAppMsgType::YesNo, Warning) == EAppReturnType::Yes;
    }

    FText BuildDWCDataUVChannelLabel(const int32 Selection, const USkeletalMesh* Mesh, const int32 OriginalUVChannelIndex)
    {
        if (Selection == RecommendedDWCDataUVSelection)
        {
            return FText::Format(
                LOCTEXT("RecommendedDWCDataUVChannelLabel", "Recommended (UV{0})"),
                FText::AsNumber(GetDefaultDWCDataUVChannelIndex(Mesh, OriginalUVChannelIndex)));
        }

        return FText::Format(LOCTEXT("ExplicitDWCDataUVChannelLabel", "UV{0}"), FText::AsNumber(Selection));
    }

    FText BuildPreferredDWCDataUVInfoText(
        const USkeletalMesh* Mesh,
        const int32 OriginalUVChannelIndex,
        const bool bUseRecommendedDWCDataUVChannel,
        const int32 PreferredDWCDataUVChannelIndex)
    {
        const int32 SourceUVChannelCount = GetSkeletalMeshUVChannelCount(Mesh, 0);
        if (SourceUVChannelCount <= 0)
        {
            return FText::Format(
                LOCTEXT("PreferredDWCDataUVInfoNoMesh", "DWC Data UV will be generated into UV{0}."),
                FText::AsNumber(PreferredDWCDataUVChannelIndex));
        }

        const int32 RecommendedUVChannelIndex = GetDefaultDWCDataUVChannelIndex(Mesh, OriginalUVChannelIndex);
        if (bUseRecommendedDWCDataUVChannel)
        {
            if (IsExistingSourceUVChannel(Mesh, RecommendedUVChannelIndex))
            {
                return FText::Format(
                    LOCTEXT("PreferredDWCDataUVInfoRecommendedOccupied", "Recommended UV{0} already contains UV data. Creating will ask for confirmation before replacing it."),
                    FText::AsNumber(RecommendedUVChannelIndex));
            }

            return FText::Format(
                LOCTEXT("PreferredDWCDataUVInfo", "Recommended target is UV{0}. Existing source channels: {1}."),
                FText::AsNumber(RecommendedUVChannelIndex),
                FText::FromString(BuildUVChannelList(0, SourceUVChannelCount - 1)));
        }

        if (IsExistingSourceUVChannel(Mesh, PreferredDWCDataUVChannelIndex))
        {
            return FText::Format(
                LOCTEXT("PreferredDWCDataUVInfoOccupied", "UV{0} already contains UV data. Creating will ask for confirmation before replacing it."),
                FText::AsNumber(PreferredDWCDataUVChannelIndex));
        }

        return FText::Format(
            LOCTEXT("PreferredDWCDataUVInfoCustom", "Selected target is UV{0}. Recommended for this mesh: UV{1}. Existing source channels: {2}."),
            FText::AsNumber(PreferredDWCDataUVChannelIndex),
            FText::AsNumber(RecommendedUVChannelIndex),
            FText::FromString(BuildUVChannelList(0, SourceUVChannelCount - 1)));
    }

    void RefreshSourceMeshThumbnail(
        const TSharedPtr<FAssetThumbnail>& SourceMeshThumbnail,
        const TSharedPtr<FAssetThumbnailPool>& SourceMeshThumbnailPool,
        USkeletalMesh* SourceMesh)
    {
        if (!SourceMeshThumbnail.IsValid())
        {
            return;
        }

        SourceMeshThumbnail->SetAsset(SourceMesh);
        SourceMeshThumbnail->SetRealTime(SourceMesh != nullptr);
        SourceMeshThumbnail->RefreshThumbnail();

        if (SourceMeshThumbnailPool.IsValid())
        {
            TArray<TSharedPtr<FAssetThumbnail>> ThumbnailsToPrioritize;
            ThumbnailsToPrioritize.Add(SourceMeshThumbnail);
            SourceMeshThumbnailPool->PrioritizeThumbnails(
                ThumbnailsToPrioritize,
                SourceMeshThumbnailSize,
                SourceMeshThumbnailSize);
        }
    }
}

#if WITH_EDITOR
void UWetClothingAssetCreationSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    const FName PropertyName = PropertyChangedEvent.Property != nullptr
        ? PropertyChangedEvent.Property->GetFName()
        : NAME_None;
    if (PropertyName == GET_MEMBER_NAME_CHECKED(UWetClothingAssetCreationSettings, SourceSkeletalMesh))
    {
        PreferredDWCDataUVChannelIndex = GetDefaultDWCDataUVChannelIndex(SourceSkeletalMesh, OriginalUVChannelIndex);
        FirstGeneratedLODIndex = 0;
        LastGeneratedLODIndex = FMath::Max(0, GetSkeletalMeshLODCount(SourceSkeletalMesh) - 1);
    }
    OriginalUVChannelIndex = FMath::Clamp(OriginalUVChannelIndex, 0, 7);
    PreferredDWCDataUVChannelIndex = FMath::Clamp(PreferredDWCDataUVChannelIndex, 0, 7);
    ClampLODRangeForMesh(SourceSkeletalMesh, FirstGeneratedLODIndex, LastGeneratedLODIndex);
}
#endif

UWetClothingAssetFactory::UWetClothingAssetFactory()
{
    SupportedClass = UWetClothingAsset::StaticClass();
    bCreateNew = true;
    bEditAfterNew = true;
}

bool UWetClothingAssetFactory::ConfigureProperties()
{
    bConfirmedOverwriteExistingDataUVChannel = false;
    PendingCreationSettings = NewObject<UWetClothingAssetCreationSettings>(this, NAME_None, RF_Transient);

    FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
    FDetailsViewArgs DetailsArgs;
    DetailsArgs.bAllowSearch = false;
    DetailsArgs.bHideSelectionTip = true;
    DetailsArgs.bLockable = false;
    DetailsArgs.bUpdatesFromSelection = false;
    DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
    TSharedRef<IDetailsView> DetailsView = PropertyEditor.CreateDetailView(DetailsArgs);
    DetailsView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateLambda([](const FPropertyAndParent& PropertyAndParent)
    {
        const FName PropertyName = PropertyAndParent.Property.GetFName();
        return PropertyName != GET_MEMBER_NAME_CHECKED(UWetClothingAssetCreationSettings, SourceSkeletalMesh) &&
               PropertyName != GET_MEMBER_NAME_CHECKED(UWetClothingAssetCreationSettings, PreferredDWCDataUVChannelIndex) &&
               PropertyName != GET_MEMBER_NAME_CHECKED(UWetClothingAssetCreationSettings, FirstGeneratedLODIndex) &&
               PropertyName != GET_MEMBER_NAME_CHECKED(UWetClothingAssetCreationSettings, LastGeneratedLODIndex);
    }));
    DetailsView->SetObject(PendingCreationSettings);

    TArray<TSharedPtr<int32>> DWCDataUVChannelOptions;
    DWCDataUVChannelOptions.Add(MakeShared<int32>(RecommendedDWCDataUVSelection));
    for (int32 UVChannelIndex = 0; UVChannelIndex <= 7; ++UVChannelIndex)
    {
        DWCDataUVChannelOptions.Add(MakeShared<int32>(UVChannelIndex));
    }
    bool bUseRecommendedDWCDataUVChannel = true;
    TSharedPtr<FAssetThumbnailPool> SourceMeshThumbnailPool = UThumbnailManager::Get().GetSharedThumbnailPool();
    TSharedPtr<FAssetThumbnail> SourceMeshThumbnail = MakeShared<FAssetThumbnail>(
        PendingCreationSettings->SourceSkeletalMesh.Get(),
        SourceMeshThumbnailSize,
        SourceMeshThumbnailSize,
        SourceMeshThumbnailPool);
    FAssetThumbnailConfig SourceMeshThumbnailConfig;
    SourceMeshThumbnailConfig.bAllowFadeIn = false;
    SourceMeshThumbnailConfig.bAllowHintText = false;
    SourceMeshThumbnailConfig.ThumbnailLabel = EThumbnailLabel::NoLabel;
    SourceMeshThumbnailConfig.ShowAssetBorder = true;
    RefreshSourceMeshThumbnail(
        SourceMeshThumbnail,
        SourceMeshThumbnailPool,
        PendingCreationSettings->SourceSkeletalMesh.Get());

    auto HandleSourceMeshPicked =
        [this, DetailsView, &bUseRecommendedDWCDataUVChannel, SourceMeshThumbnail, SourceMeshThumbnailPool](const FAssetData& AssetData)
        {
            if (PendingCreationSettings == nullptr)
            {
                return;
            }

            USkeletalMesh* NewSourceMesh = Cast<USkeletalMesh>(AssetData.GetAsset());
            PendingCreationSettings->SourceSkeletalMesh = NewSourceMesh;
            bUseRecommendedDWCDataUVChannel = true;
            PendingCreationSettings->PreferredDWCDataUVChannelIndex = GetDefaultDWCDataUVChannelIndex(NewSourceMesh, PendingCreationSettings->OriginalUVChannelIndex);
            PendingCreationSettings->FirstGeneratedLODIndex = 0;
            PendingCreationSettings->LastGeneratedLODIndex = FMath::Max(0, GetSkeletalMeshLODCount(NewSourceMesh) - 1);
            ClampLODRangeForMesh(NewSourceMesh, PendingCreationSettings->FirstGeneratedLODIndex, PendingCreationSettings->LastGeneratedLODIndex);
            RefreshSourceMeshThumbnail(SourceMeshThumbnail, SourceMeshThumbnailPool, NewSourceMesh);
            DetailsView->ForceRefresh();
        };

    auto IsCreateEnabled =
        [this, &bUseRecommendedDWCDataUVChannel]()
        {
            return PendingCreationSettings != nullptr &&
                   PendingCreationSettings->SourceSkeletalMesh != nullptr &&
                   IsDWCDataUVSelectionValid(
                       PendingCreationSettings->SourceSkeletalMesh,
                       PendingCreationSettings->OriginalUVChannelIndex,
                       bUseRecommendedDWCDataUVChannel,
                       PendingCreationSettings->PreferredDWCDataUVChannelIndex);
        };

    bool bAccepted = false;
    TSharedRef<SWindow> Dialog =
        SNew(SWindow)
        .Title(LOCTEXT("CreateTitle", "Create Wet Clothing Asset"))
        .SizingRule(ESizingRule::Autosized)
        .SupportsMaximize(false)
        .SupportsMinimize(false);

    Dialog->SetContent(
        SNew(SBox)
        .WidthOverride(CreateDialogWidth)
        [
            SNew(SBorder)
            .Padding(12.0f)
            [
                SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 0, 0, 8)
            [
                SNew(STextBlock)
                .AutoWrapText(true)
                .Text(LOCTEXT(
                    "CreateDescription",
                    "Choose the Source Mesh and which derived data to prepare."))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 0, 0, 8)
            [
                SNew(SBorder)
                .Padding(8.0f)
                .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT("SourceSkeletalMeshPickerLabel", "Source Skeletal Mesh"))
                        .Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 6.0f, 0.0f, 0.0f)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot()
                        .AutoWidth()
                        .VAlign(VAlign_Top)
                        [
                            SNew(SBox)
                            .WidthOverride(SourceMeshThumbnailSize)
                            .HeightOverride(SourceMeshThumbnailSize)
                            [
                                SourceMeshThumbnail->MakeThumbnailWidget(SourceMeshThumbnailConfig)
                            ]
                        ]
                        + SHorizontalBox::Slot()
                        .FillWidth(1.0f)
                        .Padding(10.0f, 0.0f, 0.0f, 0.0f)
                        .VAlign(VAlign_Top)
                        [
                            SNew(SVerticalBox)
                            + SVerticalBox::Slot()
                            .AutoHeight()
                            [
                                SNew(SObjectPropertyEntryBox)
                                .AllowedClass(USkeletalMesh::StaticClass())
                                .AllowClear(true)
                                .AllowCreate(false)
                                .DisplayThumbnail(false)
                                .ThumbnailPool(SourceMeshThumbnailPool)
                                .ObjectPath_Lambda([this]()
                                {
                                    return PendingCreationSettings != nullptr && PendingCreationSettings->SourceSkeletalMesh != nullptr
                                        ? PendingCreationSettings->SourceSkeletalMesh->GetPathName()
                                        : FString();
                                })
                                .OnObjectChanged_Lambda(HandleSourceMeshPicked)
                            ]
                            + SVerticalBox::Slot()
                            .AutoHeight()
                            .Padding(0.0f, 6.0f, 0.0f, 0.0f)
                            [
                                SNew(STextBlock)
                                .AutoWrapText(true)
                                .Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))
                                .ColorAndOpacity(FStyleColors::AccentBlue)
                                .Text_Lambda([this]()
                                {
                                    return PendingCreationSettings != nullptr
                                        ? BuildSourceMeshUVInfoText(PendingCreationSettings->SourceSkeletalMesh)
                                        : BuildSourceMeshUVInfoText(nullptr);
                                })
                            ]
                        ]
                    ]
                    ]
                ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 0, 0, 8)
            [
                SNew(SBorder)
                .Padding(8.0f)
                .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT("DWCUVChannelSectionLabel", "DWC Data UV"))
                        .Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 8.0f, 0.0f, 0.0f)
                    [
                        SNew(STextBlock)
                        .AutoWrapText(true)
                        .Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))
                        .ColorAndOpacity(FStyleColors::AccentGreen)
                        .Text(BuildCreateDWCDataUVTargetText())
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 10.0f, 0.0f, 0.0f)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot()
                        .FillWidth(1.0f)
                        .VAlign(VAlign_Center)
                        [
                            SNew(STextBlock)
                            .Text(LOCTEXT("PreferredDWCDataUVChannelLabel", "Preferred DWC Data UV Channel"))
                        ]
                        + SHorizontalBox::Slot()
                        .AutoWidth()
                        .VAlign(VAlign_Center)
                        [
                            SNew(SBox)
                            .WidthOverride(180.0f)
                            [
                                SNew(SComboBox<TSharedPtr<int32>>)
                                .OptionsSource(&DWCDataUVChannelOptions)
                                .InitiallySelectedItem(DWCDataUVChannelOptions[0])
                                .OnGenerateWidget_Lambda([this](TSharedPtr<int32> Item)
                                {
                                    const int32 Selection = Item.IsValid() ? *Item : RecommendedDWCDataUVSelection;
                                    return SNew(STextBlock)
                                        .Text(BuildDWCDataUVChannelLabel(
                                            Selection,
                                            PendingCreationSettings != nullptr ? PendingCreationSettings->SourceSkeletalMesh : nullptr,
                                            PendingCreationSettings != nullptr ? PendingCreationSettings->OriginalUVChannelIndex : 0));
                                })
                                .OnSelectionChanged_Lambda([this, &bUseRecommendedDWCDataUVChannel](TSharedPtr<int32> Item, ESelectInfo::Type)
                                {
                                    if (PendingCreationSettings == nullptr || !Item.IsValid())
                                    {
                                        return;
                                    }

                                    const int32 Selection = *Item;
                                    bUseRecommendedDWCDataUVChannel = Selection == RecommendedDWCDataUVSelection;
                                    if (bUseRecommendedDWCDataUVChannel)
                                    {
                                        PendingCreationSettings->PreferredDWCDataUVChannelIndex =
                                            GetDefaultDWCDataUVChannelIndex(PendingCreationSettings->SourceSkeletalMesh, PendingCreationSettings->OriginalUVChannelIndex);
                                    }
                                    else
                                    {
                                        PendingCreationSettings->PreferredDWCDataUVChannelIndex = FMath::Clamp(Selection, 0, 7);
                                    }
                                })
                                [
                                    SNew(STextBlock)
                                    .Text_Lambda([this, &bUseRecommendedDWCDataUVChannel]()
                                    {
                                        const int32 Selection = bUseRecommendedDWCDataUVChannel
                                            ? RecommendedDWCDataUVSelection
                                            : (PendingCreationSettings != nullptr
                                                ? PendingCreationSettings->PreferredDWCDataUVChannelIndex
                                                : RecommendedDWCDataUVSelection);
                                        return BuildDWCDataUVChannelLabel(
                                            Selection,
                                            PendingCreationSettings != nullptr ? PendingCreationSettings->SourceSkeletalMesh : nullptr,
                                            PendingCreationSettings != nullptr ? PendingCreationSettings->OriginalUVChannelIndex : 0);
                                    })
                                ]
                            ]
                        ]
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 4.0f, 0.0f, 0.0f)
                    [
                        SNew(STextBlock)
                        .AutoWrapText(true)
                        .Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))
                        .ColorAndOpacity_Lambda([this, &bUseRecommendedDWCDataUVChannel]()
                        {
                            return PendingCreationSettings != nullptr &&
                                   !IsDWCDataUVSelectionValid(
                                       PendingCreationSettings->SourceSkeletalMesh,
                                       PendingCreationSettings->OriginalUVChannelIndex,
                                       bUseRecommendedDWCDataUVChannel,
                                       PendingCreationSettings->PreferredDWCDataUVChannelIndex)
                                       ? FStyleColors::Error
                                       : FStyleColors::AccentBlue;
                        })
                        .Text_Lambda([this, &bUseRecommendedDWCDataUVChannel]()
                        {
                            return PendingCreationSettings != nullptr
                                ? BuildPreferredDWCDataUVInfoText(
                                    PendingCreationSettings->SourceSkeletalMesh,
                                    PendingCreationSettings->OriginalUVChannelIndex,
                                    bUseRecommendedDWCDataUVChannel,
                                    PendingCreationSettings->PreferredDWCDataUVChannelIndex)
                                : BuildPreferredDWCDataUVInfoText(nullptr, 0, true, 1);
                        })
                    ]
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 0, 0, 8)
            [
                SNew(SBorder)
                .Padding(8.0f)
                .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot()
                        .FillWidth(1.0f)
                        .VAlign(VAlign_Center)
                        [
                            SNew(STextBlock)
                            .Text(LOCTEXT("LODRangeLabel", "LOD Mapping Range"))
                            .Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))
                        ]
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 6.0f, 0.0f, 0.0f)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot()
                        .FillWidth(1.0f)
                        .VAlign(VAlign_Center)
                        [
                            SNew(STextBlock)
                            .Text(LOCTEXT("FirstGeneratedLODLabel", "First Mapped LOD"))
                        ]
                        + SHorizontalBox::Slot()
                        .AutoWidth()
                        .VAlign(VAlign_Center)
                        [
                            SNew(SBox)
                            .WidthOverride(120.0f)
                            [
                                SNew(SSpinBox<int32>)
                                .MinValue(0)
                                .MaxValue_Lambda([this]()
                                {
                                    return FMath::Max(0, GetSkeletalMeshLODCount(PendingCreationSettings != nullptr ? PendingCreationSettings->SourceSkeletalMesh : nullptr) - 1);
                                })
                                .Value_Lambda([this]()
                                {
                                    return PendingCreationSettings != nullptr ? PendingCreationSettings->FirstGeneratedLODIndex : 0;
                                })
                                .OnValueChanged_Lambda([this](int32 NewValue)
                                {
                                    if (PendingCreationSettings == nullptr)
                                    {
                                        return;
                                    }
                                    PendingCreationSettings->FirstGeneratedLODIndex = NewValue;
                                    ClampLODRangeForMesh(PendingCreationSettings->SourceSkeletalMesh, PendingCreationSettings->FirstGeneratedLODIndex, PendingCreationSettings->LastGeneratedLODIndex);
                                })
                            ]
                        ]
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 6.0f, 0.0f, 0.0f)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot()
                        .FillWidth(1.0f)
                        .VAlign(VAlign_Center)
                        [
                            SNew(STextBlock)
                            .Text(LOCTEXT("LastGeneratedLODLabel", "Last Mapped LOD"))
                        ]
                        + SHorizontalBox::Slot()
                        .AutoWidth()
                        .VAlign(VAlign_Center)
                        [
                            SNew(SBox)
                            .WidthOverride(120.0f)
                            [
                                SNew(SSpinBox<int32>)
                                .MinValue_Lambda([this]()
                                {
                                    return PendingCreationSettings != nullptr ? PendingCreationSettings->FirstGeneratedLODIndex : 0;
                                })
                                .MaxValue_Lambda([this]()
                                {
                                    return FMath::Max(0, GetSkeletalMeshLODCount(PendingCreationSettings != nullptr ? PendingCreationSettings->SourceSkeletalMesh : nullptr) - 1);
                                })
                                .Value_Lambda([this]()
                                {
                                    return PendingCreationSettings != nullptr ? PendingCreationSettings->LastGeneratedLODIndex : 0;
                                })
                                .OnValueChanged_Lambda([this](int32 NewValue)
                                {
                                    if (PendingCreationSettings == nullptr)
                                    {
                                        return;
                                    }
                                    PendingCreationSettings->LastGeneratedLODIndex = NewValue;
                                    ClampLODRangeForMesh(PendingCreationSettings->SourceSkeletalMesh, PendingCreationSettings->FirstGeneratedLODIndex, PendingCreationSettings->LastGeneratedLODIndex);
                                })
                            ]
                        ]
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 6.0f, 0.0f, 0.0f)
                    [
                        SNew(STextBlock)
                        .AutoWrapText(true)
                        .Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))
                        .ColorAndOpacity(FStyleColors::AccentBlue)
                        .Text_Lambda([this]()
                        {
                            if (PendingCreationSettings == nullptr)
                            {
                                return BuildLODRangeInfoText(nullptr, 0, 0);
                            }
                            ClampLODRangeForMesh(PendingCreationSettings->SourceSkeletalMesh, PendingCreationSettings->FirstGeneratedLODIndex, PendingCreationSettings->LastGeneratedLODIndex);
                            return BuildLODRangeInfoText(
                                PendingCreationSettings->SourceSkeletalMesh,
                                PendingCreationSettings->FirstGeneratedLODIndex,
                                PendingCreationSettings->LastGeneratedLODIndex);
                        })
                    ]
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SBox)
                .WidthOverride(560.0f)
                .HeightOverride(320.0f)
                [DetailsView]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .HAlign(HAlign_Right)
            .Padding(0, 10, 0, 0)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(0, 0, 6, 0)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("Cancel", "Cancel"))
                    .OnClicked_Lambda([Dialog]()
                    {
                        Dialog->RequestDestroyWindow();
                        return FReply::Handled();
                    })
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(SButton)
                    .Text(LOCTEXT("Create", "Create"))
                    .IsEnabled_Lambda(IsCreateEnabled)
                    .OnClicked_Lambda([this, &bAccepted, &bUseRecommendedDWCDataUVChannel, Dialog]()
                    {
                        if (PendingCreationSettings == nullptr)
                        {
                            return FReply::Handled();
                        }

                        const int32 DataUVChannelIndex = bUseRecommendedDWCDataUVChannel
                            ? GetDefaultDWCDataUVChannelIndex(
                                PendingCreationSettings->SourceSkeletalMesh,
                                PendingCreationSettings->OriginalUVChannelIndex)
                            : PendingCreationSettings->PreferredDWCDataUVChannelIndex;
                        if (!ConfirmExistingDataUVOverwrite(
                                PendingCreationSettings->SourceSkeletalMesh,
                                PendingCreationSettings->OriginalUVChannelIndex,
                                DataUVChannelIndex))
                        {
                            return FReply::Handled();
                        }

                        PendingCreationSettings->PreferredDWCDataUVChannelIndex = DataUVChannelIndex;
                        ClampLODRangeForMesh(
                            PendingCreationSettings->SourceSkeletalMesh,
                            PendingCreationSettings->FirstGeneratedLODIndex,
                            PendingCreationSettings->LastGeneratedLODIndex);
                        bConfirmedOverwriteExistingDataUVChannel =
                            IsExistingSourceUVChannel(PendingCreationSettings->SourceSkeletalMesh, DataUVChannelIndex);
                        bAccepted = true;
                        Dialog->RequestDestroyWindow();
                        return FReply::Handled();
                    })
                ]
            ]
            ]
        ]);

    FSlateApplication::Get().AddModalWindow(Dialog, FSlateApplication::Get().GetActiveTopLevelWindow());
    if (!bAccepted)
    {
        PendingCreationSettings = nullptr;
    }
    return bAccepted;
}

UObject* UWetClothingAssetFactory::FactoryCreateNew(
    UClass* Class,
    UObject* InParent,
    FName Name,
    EObjectFlags Flags,
    UObject* Context,
    FFeedbackContext* Warn)
{
    if (PendingCreationSettings == nullptr || PendingCreationSettings->SourceSkeletalMesh == nullptr)
    {
        return nullptr;
    }

    UWetClothingAsset* Asset = NewObject<UWetClothingAsset>(InParent, Class, Name, Flags | RF_Transactional);
    FScopedSlowTask SlowTask(
        3.0f,
        FText::FromString(FString::Printf(TEXT("Creating Wet Clothing Asset %s..."), *Name.ToString())));
    SlowTask.MakeDialog(false);

    FString ErrorMessage;
    SlowTask.EnterProgressFrame(
        1.0f,
        LOCTEXT("CreateWCAInitializeProgress", "Initializing Wet Clothing Asset settings and default wet parts..."));
    if (!Asset->InitializeNewAsset(
            PendingCreationSettings->SourceSkeletalMesh,
            PendingCreationSettings->BuildSettings(),
            &ErrorMessage))
    {
        UE_LOG(LogTemp, Error, TEXT("DWC: Failed to initialize Wet Clothing Asset: %s"), *ErrorMessage);
        PendingCreationSettings = nullptr;
        bConfirmedOverwriteExistingDataUVChannel = false;
        return nullptr;
    }

    SlowTask.EnterProgressFrame(
        1.0f,
        LOCTEXT("CreateWCAGenerateDataUVProgress", "Creating the DWC Prepared Skeletal Mesh and generating Data UV..."));
    const FDWCDataUVBuildResult DataUVResult = FDWCDataUVBuildService::Generate(*Asset, true, bConfirmedOverwriteExistingDataUVChannel);
    if (!DataUVResult.bSucceeded)
    {
        Asset->SetLastBakeFailure(DataUVResult.Message);
        UE_LOG(LogTemp, Warning, TEXT("DWC: WCA was created but DWC Data UV generation failed: %s"), *DataUVResult.Message);
    }

    SlowTask.EnterProgressFrame(
        1.0f,
        LOCTEXT("CreateWCAFinalizeProgress", "Finalizing Wet Clothing Asset creation..."));
    Asset->MarkPackageDirty();
    PendingCreationSettings = nullptr;
    bConfirmedOverwriteExistingDataUVChannel = false;
    return Asset;
}

bool UWetClothingAssetFactory::ShouldShowInNewMenu() const
{
    return true;
}

#undef LOCTEXT_NAMESPACE
