#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetWrinkleNormalTextureData.h"
#include "WetWrinkleEditorSettings.generated.h"

UCLASS(config = EditorPerProjectUserSettings)
class UWetWrinkleEditorSettings : public UObject
{
    GENERATED_BODY()

  public:
    static constexpr const TCHAR* DefaultNormalTexturePath = TEXT("/DynamicWetClothes/Presets/WrinkleTextures");

    void GetNormalTextureSearchPaths(TArray<FString>& OutPaths) const;
    bool AddNormalTextureSearchPath(const FString& InPath);
    void RemoveNormalTextureSearchPath(const FString& InPath);

    bool IsNormalTextureHidden(const FSoftObjectPath& TexturePath) const;
    void SetNormalTextureHidden(const FSoftObjectPath& TexturePath, bool bHidden);

    UPROPERTY(config)
    TArray<FDirectoryPath> AdditionalNormalTexturePaths;

    UPROPERTY(config)
    TArray<FSoftObjectPath> HiddenNormalTexturePaths;

    UPROPERTY(config)
    bool bShowHiddenNormalTextures = false;

    UPROPERTY(config)
    bool bLastUseCorrection = true;

    UPROPERTY(config)
    bool bLastHideOriginal = false;

    UPROPERTY(config)
    FWetWrinkleNormalCorrectionSettings LastCorrectionSettings;

  private:
    static FString NormalizeContentPath(const FString& InPath);
};
