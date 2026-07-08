#include "WetClothingAssetDefinition.h"

#include "Core/DWCEditorStyle.h"
#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/Common/Editor/WetClothingAssetEditor.h"

#define LOCTEXT_NAMESPACE "WetClothingAssetDefinition"

FText UWetClothingAssetDefinition::GetAssetDisplayName() const
{
    return LOCTEXT("AssetDisplayName", "Wet Clothing Asset");
}

FLinearColor UWetClothingAssetDefinition::GetAssetColor() const
{
    return FLinearColor(0.0f, 0.45f, 0.8f);
}

TSoftClassPtr<UObject> UWetClothingAssetDefinition::GetAssetClass() const
{
    return UWetClothingAsset::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UWetClothingAssetDefinition::GetAssetCategories() const
{
    static const auto Categories = { EAssetCategoryPaths::Misc };
    return Categories;
}

const FSlateBrush* UWetClothingAssetDefinition::GetThumbnailBrush(const FAssetData& InAssetData, const FName InClassName) const
{
    return FDWCEditorStyle::GetBrush(TEXT("ClassThumbnail.WetClothingAsset"));
}

const FSlateBrush* UWetClothingAssetDefinition::GetIconBrush(const FAssetData& InAssetData, const FName InClassName) const
{
    return FDWCEditorStyle::GetBrush(TEXT("ClassIcon.WetClothingAsset"));
}

EAssetCommandResult UWetClothingAssetDefinition::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
    const TArray<UWetClothingAsset*> WetClothingAssets = OpenArgs.LoadObjects<UWetClothingAsset>();

    if (WetClothingAssets.IsEmpty())
    {
        return EAssetCommandResult::Unhandled;
    }

    for (UWetClothingAsset* WetClothingAsset : WetClothingAssets)
    {
        TSharedRef<FWetClothingAssetEditor> Editor = MakeShared<FWetClothingAssetEditor>();
        Editor->Initialize(OpenArgs.GetToolkitMode(), OpenArgs.ToolkitHost, WetClothingAsset);
    }

    return EAssetCommandResult::Handled;
}

#undef LOCTEXT_NAMESPACE
