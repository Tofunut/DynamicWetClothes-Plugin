#pragma once

#include "CoreMinimal.h"
#include "DataAssets/DWCBakeLayer.h"

class AActor;
class UDWCBakeComponent;
class UMaterial;
class UMaterialExpression;
class UMaterialExpressionComponentMask;
class UMaterialExpressionMultiply;
class UMaterialExpressionScalarParameter;
class UMaterialExpressionTextureSampleParameter2D;
class UMaterialExpressionVertexColor;
class UMaterialInstance;
class UMaterialInstanceConstant;
class UMaterialInterface;
class UTexture;
class UTexture2D;
class UMaterialInstanceDynamic;
class USkeletalMeshComponent;
struct FDWCRevealBakeTextureSet;

class FDWCRevealBakeMaterialBuilder
{
  public:
    static void ApplyLookupPreviewToOuterMaterials(
        const AActor&                   Actor,
        const UDWCBakeComponent&        BakeComponent,
        const FDWCBakeSnapshot&         Snapshot,
        const FDWCBakeResolvedLayer&    OuterLayer,
        const FDWCRevealBakeTextureSet& TextureSet,
        const TArray<FName>&            SourceLayerIds);

  private:
    static USkeletalMeshComponent* FindLayerComponent(const AActor& Actor, const FDWCBakeResolvedLayer& Layer);
    static void SetTextureParameterIfValid(UMaterialInstanceDynamic& MID, FName ParameterName, UTexture* Texture);

    static bool ConnectMaterialExpressionsChecked(
        UMaterialExpression* FromExpression,
        const FString&       FromOutputName,
        UMaterialExpression* ToExpression,
        const FString&       ToInputName);

    static UMaterialExpression* ResolveBaseColorInputOrFallback(UMaterial* Material, FString& OutOutputName);

    static UMaterialExpressionTextureSampleParameter2D* CreateRevealTextureParameter(
        UMaterial*  Material,
        FName       ParameterName,
        UTexture2D* Texture,
        int32       NodePosX,
        int32       NodePosY,
        bool        bColorTexture);

    static UMaterialExpressionComponentMask* CreateRedMask(UMaterial* Material, int32 NodePosX, int32 NodePosY);
    static UMaterialExpressionVertexColor* CreateVertexColor(UMaterial* Material, int32 NodePosX, int32 NodePosY);

    static UMaterialExpressionScalarParameter* CreateRevealScalarParameter(
        UMaterial* Material,
        FName      ParameterName,
        float      DefaultValue,
        int32      NodePosX,
        int32      NodePosY);

    static UMaterialExpressionMultiply* CreateMultiply(UMaterial* Material, int32 NodePosX, int32 NodePosY);

    static UMaterial* DuplicateRevealMaterial(
        UMaterialInterface* SourceMaterialInterface,
        const FString&      AssetNamePrefix);

    static bool ConfigureRevealMaterialGraph(
        UMaterial*                      Material,
        const UDWCBakeComponent&        BakeComponent,
        const FDWCBakeResolvedLayer&    OuterLayer,
        const FDWCRevealBakeTextureSet& TextureSet);

    static UMaterialInstanceConstant* CreateRevealMaterialInstanceForSource(
        const UMaterialInstance* SourceInstance,
        UMaterialInterface*      RevealParent,
        const FString&           AssetNamePrefix);

    static UMaterialInterface* CreateConfiguredRevealMaterial(
        UMaterialInterface*             SourceMaterial,
        const FString&                  AssetNamePrefix,
        const UDWCBakeComponent&        BakeComponent,
        const FDWCBakeResolvedLayer&    OuterLayer,
        const FDWCRevealBakeTextureSet& TextureSet);
};
