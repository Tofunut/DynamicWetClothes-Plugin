#pragma once

#include "CoreMinimal.h"

class UObject;

namespace DynamicWetClothesEditorUtils
{
	inline constexpr TCHAR DefaultWetnessProfileLibraryPath[] = TEXT("/Game/WetnessProfiles");

	TArray<FString> BuildUniqueProfileSearchPaths(const TArray<FString>& AdditionalPaths);
	bool PromptForContentFolder(FString& OutContentPath);
	bool SaveAsset(UObject* Asset);
}
