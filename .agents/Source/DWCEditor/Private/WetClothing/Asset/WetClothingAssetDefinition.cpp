#include "WetClothingAssetDefinition.h"

#include "Core/DWCEditorStyle.h"
#include "DataAssets/WetClothingAsset.h"
#include "Misc/ScopedSlowTask.h"
#include "WetClothing/WCAEditor/WCAEditor.h"

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
    FScopedSlowTask OpenTask(
        1.0f,
        LOCTEXT("OpenWetClothingAssetsProgress", "Opening Wet Clothing Asset..."));
    OpenTask.MakeDialog(false);
    OpenTask.EnterProgressFrame(
        0.2f,
        LOCTEXT("LoadWetClothingAssetObjectsProgress", "Loading Wet Clothing Asset objects..."));

    const TArray<UWetClothingAsset*> WetClothingAssets = OpenArgs.LoadObjects<UWetClothingAsset>();

    if (WetClothingAssets.IsEmpty())
    {
        return EAssetCommandResult::Unhandled;
    }

    const float WorkPerAsset = 0.8f / static_cast<float>(WetClothingAssets.Num());
    for (UWetClothingAsset* WetClothingAsset : WetClothingAssets)
    {
        OpenTask.EnterProgressFrame(
            WorkPerAsset,
            FText::FromString(FString::Printf(
                TEXT("Creating editor for %s (metadata only, runtime payload stays lazy)..."),
                *GetNameSafe(WetClothingAsset))));
        TSharedRef<FWCAEditor> Editor = MakeShared<FWCAEditor>();
        Editor->Initialize(OpenArgs.GetToolkitMode(), OpenArgs.ToolkitHost, WetClothingAsset);
    }

    return EAssetCommandResult::Handled;
}

#undef LOCTEXT_NAMESPACE
