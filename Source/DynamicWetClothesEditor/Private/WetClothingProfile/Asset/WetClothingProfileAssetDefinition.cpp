#include "WetClothingProfileAssetDefinition.h"

#include "Core/DynamicWetClothesEditorStyle.h"
#include "WetClothingProfile.h"
#include "WetClothingProfile/Editor/WetClothingProfileEditor.h"

#define LOCTEXT_NAMESPACE "WetClothingProfileAssetDefinition"

FText UWetClothingProfileAssetDefinition::GetAssetDisplayName() const
{
    return LOCTEXT("AssetDisplayName", "Wet Clothing Asset");
}

FLinearColor UWetClothingProfileAssetDefinition::GetAssetColor() const
{
    return FLinearColor(0.0f, 0.45f, 0.8f);
}

TSoftClassPtr<UObject> UWetClothingProfileAssetDefinition::GetAssetClass() const
{
    return UWetClothingProfile::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UWetClothingProfileAssetDefinition::GetAssetCategories() const
{
    static const auto Categories = { EAssetCategoryPaths::Misc };
    return Categories;
}

const FSlateBrush* UWetClothingProfileAssetDefinition::GetThumbnailBrush(const FAssetData& InAssetData, const FName InClassName) const
{
    return FDynamicWetClothesEditorStyle::GetBrush(TEXT("ClassThumbnail.WetClothingProfile"));
}

const FSlateBrush* UWetClothingProfileAssetDefinition::GetIconBrush(const FAssetData& InAssetData, const FName InClassName) const
{
    return FDynamicWetClothesEditorStyle::GetBrush(TEXT("ClassIcon.WetClothingProfile"));
}

EAssetCommandResult UWetClothingProfileAssetDefinition::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
    const TArray<UWetClothingProfile*> WetClothingProfiles = OpenArgs.LoadObjects<UWetClothingProfile>();

    if (WetClothingProfiles.IsEmpty())
    {
        return EAssetCommandResult::Unhandled;
    }

    for (UWetClothingProfile* WetClothingProfile : WetClothingProfiles)
    {
        TSharedRef<FWetClothingProfileEditor> Editor = MakeShared<FWetClothingProfileEditor>();
        Editor->Initialize(OpenArgs.GetToolkitMode(), OpenArgs.ToolkitHost, WetClothingProfile);
    }

    return EAssetCommandResult::Handled;
}

#undef LOCTEXT_NAMESPACE
