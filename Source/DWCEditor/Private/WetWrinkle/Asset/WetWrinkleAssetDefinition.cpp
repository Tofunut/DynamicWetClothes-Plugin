#include "WetWrinkleAssetDefinition.h"

#include "Core/DWCEditorStyle.h"
#include "DataAssets/WetWrinkleAsset.h"
#include "WetWrinkle/Editor/WetWrinkleAssetEditor.h"

#define LOCTEXT_NAMESPACE "WetWrinkleAssetDefinition"

FText UWetWrinkleAssetDefinition::GetAssetDisplayName() const
{
    return LOCTEXT("AssetDisplayName", "Wet Wrinkle Asset");
}

FLinearColor UWetWrinkleAssetDefinition::GetAssetColor() const
{
    return FLinearColor(0.16f, 0.55f, 0.95f);
}

TSoftClassPtr<UObject> UWetWrinkleAssetDefinition::GetAssetClass() const
{
    return UWetWrinkleAsset::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UWetWrinkleAssetDefinition::GetAssetCategories() const
{
    static const auto Categories = { EAssetCategoryPaths::Misc };
    return Categories;
}

const FSlateBrush* UWetWrinkleAssetDefinition::GetThumbnailBrush(const FAssetData& InAssetData, const FName InClassName) const
{
    return FDWCEditorStyle::GetBrush(TEXT("ClassThumbnail.WetWrinkleAsset"));
}

const FSlateBrush* UWetWrinkleAssetDefinition::GetIconBrush(const FAssetData& InAssetData, const FName InClassName) const
{
    return FDWCEditorStyle::GetBrush(TEXT("ClassIcon.WetWrinkleAsset"));
}

EAssetCommandResult UWetWrinkleAssetDefinition::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
    const TArray<UWetWrinkleAsset*> WetWrinkleAssets = OpenArgs.LoadObjects<UWetWrinkleAsset>();

    if (WetWrinkleAssets.IsEmpty())
    {
        return EAssetCommandResult::Unhandled;
    }

    for (UWetWrinkleAsset* WetWrinkleAsset : WetWrinkleAssets)
    {
        TSharedRef<FWetWrinkleAssetEditor> Editor = MakeShared<FWetWrinkleAssetEditor>();
        Editor->Initialize(OpenArgs.GetToolkitMode(), OpenArgs.ToolkitHost, WetWrinkleAsset);
    }

    return EAssetCommandResult::Handled;
}

#undef LOCTEXT_NAMESPACE
