#pragma once

#include "CoreMinimal.h"

class UObject;

namespace DWCEditorUtils
{
    inline constexpr TCHAR DefaultWetnessProfileLibraryPath[] = TEXT("/Game/WetnessProfiles");
    inline constexpr TCHAR PluginWetnessProfileLibraryPath[] = TEXT("/DWC/Presets/WetnessProfiles");

    TArray<FString> BuildUniqueProfileSearchPaths(const TArray<FString>& AdditionalPaths);
    bool            PromptForContentFolder(FString& OutContentPath);
    bool            SaveAsset(UObject* Asset);
} // namespace DWCEditorUtils
