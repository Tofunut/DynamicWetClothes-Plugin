// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "WetClothing/Modes/Transparency/RevealBake/DWCRevealBakeSurface.h"
#include "CoreMinimal.h"
#include "DataAssets/DWCBakeLayer.h"

#include <initializer_list>

class UTexture2D;
class UMaterialInterface;

class FDWCRevealBakeSourceResolver
{
  public:
    static UTexture2D* ResolveRevealSourceBaseColorTexture(const FDWCBakeResolvedLayer& SourceLayer);
    static UTexture2D* ResolveRevealSourceBaseColorTexture(UMaterialInterface* SourceMaterial);

  private:
    static FString NormalizeTextureSearchText(const FString& InText);
    static bool    ContainsAnyTextureKeyword(const FString& SearchText, std::initializer_list<const TCHAR*> Keywords);
    static int32   ScoreRevealBaseColorTexture(UTexture* Texture, const FString& ParameterName);
};
