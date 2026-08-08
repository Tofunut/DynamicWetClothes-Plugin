// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "UObject/WeakObjectPtr.h"
#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SCompoundWidget.h"

class UTexture2D;
class UWetClothingAsset;
class FDWCEditorAuthoringDocument;
struct FAssetData;

class SWetWrinkleCustomNormalPanel : public SCompoundWidget
{
  public:
    SLATE_BEGIN_ARGS(SWetWrinkleCustomNormalPanel) {}
    SLATE_ARGUMENT(UWetClothingAsset*, WetClothingAsset)
    SLATE_ARGUMENT(TSharedPtr<FDWCEditorAuthoringDocument>, AuthoringDocument)
    SLATE_ATTRIBUTE(int32, MaterialSlotIndex)
    SLATE_EVENT(FSimpleDelegate, OnSettingsChanged)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void Refresh();

  private:
    FString            GetTextureObjectPath() const;
    void               HandleTextureChanged(const FAssetData& AssetData);
    const FSlateBrush* GetPreviewBrush() const;
    EVisibility        GetPreviewVisibility() const;
    FText              GetStatusText() const;
    FSlateColor        GetStatusColor() const;
    FText              GetTextureInfoText() const;
    FReply             HandleBrowseClicked();
    FReply             HandleOpenClicked();
    FReply             HandleFixSettingsClicked();
    bool               CanUseTextureCommands() const;
    UTexture2D*        ResolveTexture() const;

    TWeakObjectPtr<UWetClothingAsset>       WetClothingAsset;
    TSharedPtr<FDWCEditorAuthoringDocument> AuthoringDocument;
    TAttribute<int32>                       MaterialSlotIndex;
    FSimpleDelegate                         OnSettingsChanged;
    FSlateBrush                             PreviewBrush;
};
