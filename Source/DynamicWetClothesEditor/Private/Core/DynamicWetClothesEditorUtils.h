#pragma once

#include "CoreMinimal.h"

class UObject;

namespace DynamicWetClothesEditorUtils
{
    inline constexpr TCHAR DefaultWetnessProfileLibraryPath[] = TEXT("/Game/WetnessProfiles");
    inline constexpr TCHAR PluginWetnessProfileLibraryPath[] = TEXT("/DynamicWetClothes/Presets/WetnessProfiles");

    TArray<FString> BuildUniqueProfileSearchPaths(const TArray<FString>& AdditionalPaths);
    bool            PromptForContentFolder(FString& OutContentPath);
    bool            SaveAsset(UObject* Asset);
} // namespace DynamicWetClothesEditorUtils
