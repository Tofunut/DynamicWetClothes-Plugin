// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetnessProfileAssetDefinition.h"

#include "Core/DWCEditorStyle.h"
#include "DataAssets/WetnessProfile.h"
#include "WetnessProfile/Editor/WetnessProfileEditor.h"

#define LOCTEXT_NAMESPACE "WetnessProfileAssetDefinition"

FText UWetnessProfileAssetDefinition::GetAssetDisplayName() const
{
    return LOCTEXT("AssetDisplayName", "Wetness Profile");
}

FLinearColor UWetnessProfileAssetDefinition::GetAssetColor() const
{
    return FLinearColor(0.0f, 0.7f, 0.55f);
}

TSoftClassPtr<UObject> UWetnessProfileAssetDefinition::GetAssetClass() const
{
    return UWetnessProfile::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UWetnessProfileAssetDefinition::GetAssetCategories() const
{
    static const auto Categories = { EAssetCategoryPaths::Misc };
    return Categories;
}

const FSlateBrush* UWetnessProfileAssetDefinition::GetThumbnailBrush(const FAssetData& InAssetData, const FName InClassName) const
{
    return FDWCEditorStyle::GetBrush(TEXT("ClassThumbnail.WetnessProfile"));
}

const FSlateBrush* UWetnessProfileAssetDefinition::GetIconBrush(const FAssetData& InAssetData, const FName InClassName) const
{
    return FDWCEditorStyle::GetBrush(TEXT("ClassIcon.WetnessProfile"));
}

EAssetCommandResult UWetnessProfileAssetDefinition::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
    const TArray<UWetnessProfile*> Profiles = OpenArgs.LoadObjects<UWetnessProfile>();
    if (Profiles.IsEmpty())
    {
        return EAssetCommandResult::Unhandled;
    }

    for (UWetnessProfile* Profile : Profiles)
    {
        TSharedRef<FWetnessProfileEditor> Editor = MakeShared<FWetnessProfileEditor>();
        Editor->Initialize(OpenArgs.GetToolkitMode(), OpenArgs.ToolkitHost, Profile);
    }

    return EAssetCommandResult::Handled;
}

#undef LOCTEXT_NAMESPACE
