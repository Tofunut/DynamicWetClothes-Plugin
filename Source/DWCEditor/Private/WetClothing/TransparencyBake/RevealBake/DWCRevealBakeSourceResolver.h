#pragma once

#include "WetClothing/TransparencyBake/RevealBake/DWCRevealBakeSurface.h"
#include "CoreMinimal.h"
#include "DataAssets/DWCBakeLayer.h"

#include <initializer_list>

class UTexture;
class UTexture2D;
struct FDWCRevealBakeTextureWriteSettings;

class FDWCRevealBakeSourceResolver
{
  public:
    static TArray<FName> BuildRevealSourceLayerIds(const TArray<FDWCRevealBakeSurface>& SourceSurfaces);

    static const FDWCBakeResolvedLayer* FindResolvedLayerById(
        const FDWCBakeSnapshot& Snapshot,
        FName                   LayerId);

    static UTexture2D* ResolveRevealSourceBaseColorTexture(const FDWCBakeResolvedLayer& SourceLayer);
    static UTexture* ResolvePreviewSourceTexture(const FDWCBakeResolvedLayer& SourceLayer);

    static void PopulateSourceLayerTextures(
        const FDWCBakeSnapshot&              Snapshot,
        const TArray<FName>&                 SourceLayerIds,
        FDWCRevealBakeTextureWriteSettings&  InOutTextureSettings);

  private:
    static FString NormalizeTextureSearchText(const FString& InText);
    static bool ContainsAnyTextureKeyword(const FString& SearchText, std::initializer_list<const TCHAR*> Keywords);
    static int32 ScoreRevealBaseColorTexture(UTexture* Texture, const FString& ParameterName);
};
