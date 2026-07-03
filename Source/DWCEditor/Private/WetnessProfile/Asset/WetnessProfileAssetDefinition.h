#pragma once

#include "AssetDefinitionDefault.h"
#include "WetnessProfileAssetDefinition.generated.h"

UCLASS()
class DWCEDITOR_API UWetnessProfileAssetDefinition : public UAssetDefinitionDefault
{
    GENERATED_BODY()

  public:
    virtual FText                               GetAssetDisplayName() const override;
    virtual FLinearColor                        GetAssetColor() const override;
    virtual TSoftClassPtr<UObject>              GetAssetClass() const override;
    virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override;
    virtual const FSlateBrush*                  GetThumbnailBrush(const FAssetData& InAssetData, const FName InClassName) const override;
    virtual const FSlateBrush*                  GetIconBrush(const FAssetData& InAssetData, const FName InClassName) const override;
    virtual EAssetCommandResult                 OpenAssets(const FAssetOpenArgs& OpenArgs) const override;
};
