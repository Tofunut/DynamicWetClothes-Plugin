#include "WetClothing/TransparencyBake/RevealBake/DWCRevealBakeMaterialBuilder.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Components/DWCBakeComponent.h"
#include "IAssetTools.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/PackageName.h"
#include "Runtime/Engine/Classes/Components/SkeletalMeshComponent.h"
#include "Runtime/Engine/Classes/Engine/Texture.h"
#include "Runtime/Engine/Classes/Engine/Texture2D.h"
#include "Runtime/Engine/Classes/GameFramework/Actor.h"
#include "Runtime/Engine/Public/Materials/MaterialInstanceDynamic.h"
#include "UObject/Package.h"
#include "WetClothing/TransparencyBake/RevealBake/DWCRevealBakeSourceResolver.h"
#include "WetClothing/TransparencyBake/RevealBake/DWCRevealBakeTextureWriter.h"
#include "WetClothing/TransparencyBake/RevealBake/DWCRevealBakeUtilities.h"

USkeletalMeshComponent* FDWCRevealBakeMaterialBuilder::FindLayerComponent(const AActor& Actor, const FDWCBakeResolvedLayer& Layer)
{
    TArray<USkeletalMeshComponent*> MeshComponents;
    Actor.GetComponents<USkeletalMeshComponent>(MeshComponents);
    for (USkeletalMeshComponent* MeshComponent : MeshComponents)
    {
        if (MeshComponent != nullptr && MeshComponent->GetFName() == Layer.ComponentDisplayName)
        {
            return MeshComponent;
        }
    }

    return nullptr;
}

void FDWCRevealBakeMaterialBuilder::SetTextureParameterIfValid(
    UMaterialInstanceDynamic& MID,
    const FName               ParameterName,
    UTexture*                 Texture)
{
    if (!ParameterName.IsNone() && Texture != nullptr)
    {
        MID.SetTextureParameterValue(ParameterName, Texture);
    }
}

bool FDWCRevealBakeMaterialBuilder::ConnectMaterialExpressionsChecked(
    UMaterialExpression* FromExpression,
    const FString&       FromOutputName,
    UMaterialExpression* ToExpression,
    const FString&       ToInputName)
{
    return FromExpression != nullptr &&
           ToExpression != nullptr &&
           UMaterialEditingLibrary::ConnectMaterialExpressions(FromExpression, FromOutputName, ToExpression, ToInputName);
}

UMaterialExpression* FDWCRevealBakeMaterialBuilder::ResolveBaseColorInputOrFallback(UMaterial* Material, FString& OutOutputName)
{
    UMaterialExpression* BaseColorInput = UMaterialEditingLibrary::GetMaterialPropertyInputNode(Material, MP_BaseColor);
    if (BaseColorInput != nullptr)
    {
        OutOutputName = UMaterialEditingLibrary::GetMaterialPropertyInputNodeOutputName(Material, MP_BaseColor);
        return BaseColorInput;
    }

    UMaterialExpressionConstant3Vector* Fallback = Cast<UMaterialExpressionConstant3Vector>(
        UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionConstant3Vector::StaticClass(), -900, -360));
    if (Fallback != nullptr)
    {
        Fallback->Constant = FLinearColor::White;
    }

    OutOutputName.Reset();
    return Fallback;
}

UMaterialExpressionTextureSampleParameter2D* FDWCRevealBakeMaterialBuilder::CreateRevealTextureParameter(
    UMaterial*  Material,
    const FName ParameterName,
    UTexture2D* Texture,
    const int32 NodePosX,
    const int32 NodePosY,
    const bool  bColorTexture)
{
    UMaterialExpressionTextureSampleParameter2D* Parameter = Cast<UMaterialExpressionTextureSampleParameter2D>(
        UMaterialEditingLibrary::CreateMaterialExpression(
            Material,
            UMaterialExpressionTextureSampleParameter2D::StaticClass(),
            NodePosX,
            NodePosY));
    if (Parameter != nullptr)
    {
        Parameter->ParameterName = ParameterName;
        Parameter->Texture = Texture;
        Parameter->SamplerType = bColorTexture ? SAMPLERTYPE_Color : SAMPLERTYPE_LinearColor;
    }
    return Parameter;
}

UMaterialExpressionComponentMask* FDWCRevealBakeMaterialBuilder::CreateRedMask(
    UMaterial*   Material,
    const int32 NodePosX,
    const int32 NodePosY)
{
    UMaterialExpressionComponentMask* Mask = Cast<UMaterialExpressionComponentMask>(
        UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionComponentMask::StaticClass(), NodePosX, NodePosY));
    if (Mask != nullptr)
    {
        Mask->R = true;
        Mask->G = false;
        Mask->B = false;
        Mask->A = false;
    }
    return Mask;
}

UMaterialExpressionVertexColor* FDWCRevealBakeMaterialBuilder::CreateVertexColor(
    UMaterial*   Material,
    const int32 NodePosX,
    const int32 NodePosY)
{
    return Cast<UMaterialExpressionVertexColor>(
        UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionVertexColor::StaticClass(), NodePosX, NodePosY));
}

UMaterialExpressionScalarParameter* FDWCRevealBakeMaterialBuilder::CreateRevealScalarParameter(
    UMaterial*  Material,
    const FName ParameterName,
    const float DefaultValue,
    const int32 NodePosX,
    const int32 NodePosY)
{
    UMaterialExpressionScalarParameter* Parameter = Cast<UMaterialExpressionScalarParameter>(
        UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionScalarParameter::StaticClass(), NodePosX, NodePosY));
    if (Parameter != nullptr)
    {
        Parameter->ParameterName = ParameterName;
        Parameter->DefaultValue = DefaultValue;
    }
    return Parameter;
}

UMaterialExpressionMultiply* FDWCRevealBakeMaterialBuilder::CreateMultiply(
    UMaterial*   Material,
    const int32 NodePosX,
    const int32 NodePosY)
{
    return Cast<UMaterialExpressionMultiply>(
        UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionMultiply::StaticClass(), NodePosX, NodePosY));
}

UMaterial* FDWCRevealBakeMaterialBuilder::DuplicateRevealMaterial(
    UMaterialInterface* SourceMaterialInterface,
    const FString&      AssetNamePrefix,
    const FString&      TargetPackagePath)
{
    if (SourceMaterialInterface == nullptr)
    {
        return nullptr;
    }

    UMaterial* SourceMaterial = SourceMaterialInterface->GetMaterial();
    if (SourceMaterial == nullptr || SourceMaterial->GetOutermost() == GetTransientPackage())
    {
        return nullptr;
    }

    const FString SourcePackagePath = FPackageName::GetLongPackagePath(SourceMaterial->GetOutermost()->GetName());
    const FString ResolvedTargetPackagePath = !TargetPackagePath.IsEmpty()
        ? TargetPackagePath
        : (SourcePackagePath.IsEmpty()
            ? FString(FDWCRevealBakeUtilities::GetDefaultRevealBakePackagePath())
            : SourcePackagePath);
    const FString TargetAssetBaseName = FString::Printf(
        TEXT("%s_%s_DWCReveal"),
        *FDWCRevealBakeUtilities::SanitizeAssetToken(AssetNamePrefix),
        *FDWCRevealBakeUtilities::SanitizeAssetToken(SourceMaterial->GetName()));

    FString            UniquePackageName;
    FString            UniqueAssetName;
    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    AssetToolsModule.Get().CreateUniqueAssetName(ResolvedTargetPackagePath / TargetAssetBaseName, FString(), UniquePackageName, UniqueAssetName);

    return Cast<UMaterial>(AssetToolsModule.Get().DuplicateAsset(
        UniqueAssetName,
        FPackageName::GetLongPackagePath(UniquePackageName),
        SourceMaterial));
}

bool FDWCRevealBakeMaterialBuilder::ConfigureRevealMaterialGraph(
    UMaterial*                      Material,
    const UDWCBakeComponent&        BakeComponent,
    const FDWCBakeResolvedLayer&    OuterLayer,
    const FDWCRevealBakeTextureSet& TextureSet)
{
    if (Material == nullptr || TextureSet.ColorMap == nullptr || TextureSet.MaskMap == nullptr || TextureSet.ConfidenceMap == nullptr)
    {
        return false;
    }

    Material->Modify();

    FString              BaseColorOutputName;
    UMaterialExpression* BaseColorInput = ResolveBaseColorInputOrFallback(Material, BaseColorOutputName);
    UMaterialExpressionTextureCoordinate* TextureCoordinate = Cast<UMaterialExpressionTextureCoordinate>(
        UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureCoordinate::StaticClass(), -1120, -120));
    UMaterialExpressionTextureSampleParameter2D* RevealColor = CreateRevealTextureParameter(
        Material,
        BakeComponent.RevealColorMapParameterName,
        TextureSet.ColorMap,
        -900,
        -120,
        true);
    UMaterialExpressionTextureSampleParameter2D* RevealMask = CreateRevealTextureParameter(
        Material,
        BakeComponent.RevealMaskMapParameterName,
        TextureSet.MaskMap,
        -900,
        90,
        false);
    UMaterialExpressionTextureSampleParameter2D* RevealConfidence = CreateRevealTextureParameter(
        Material,
        BakeComponent.RevealConfidenceMapParameterName,
        TextureSet.ConfidenceMap,
        -900,
        300,
        false);
    UMaterialExpressionComponentMask* MaskRed = CreateRedMask(Material, -680, 90);
    UMaterialExpressionComponentMask* ConfidenceRed = CreateRedMask(Material, -680, 300);
    UMaterialExpressionVertexColor* VertexColor = CreateVertexColor(Material, -680, 620);
    UMaterialExpressionComponentMask* VertexWetnessRed = CreateRedMask(Material, -460, 620);
    UMaterialExpressionScalarParameter* MaskMultiplier = CreateRevealScalarParameter(
        Material,
        BakeComponent.RevealMaskMultiplierParameterName,
        FMath::Max(0.0f, BakeComponent.RevealMaskMultiplier),
        -680,
        410);
    UMaterialExpressionScalarParameter* ConfidenceMultiplier = CreateRevealScalarParameter(
        Material,
        BakeComponent.RevealConfidenceMultiplierParameterName,
        FMath::Max(0.0f, BakeComponent.RevealConfidenceMultiplier),
        -680,
        500);
    UMaterialExpressionScalarParameter* RevealBlend = CreateRevealScalarParameter(
        Material,
        BakeComponent.RevealPreviewBlendParameterName,
        FMath::Clamp(BakeComponent.RevealPreviewBlendPercent / 100.0f, 0.0f, 1.0f),
        -680,
        590);
    UMaterialExpressionScalarParameter* UseRevealPreview = CreateRevealScalarParameter(
        Material,
        BakeComponent.UseRevealPreviewParameterName,
        1.0f,
        -680,
        700);
    UMaterialExpressionMultiply* MaskTimesMultiplier = CreateMultiply(Material, -460, 150);
    UMaterialExpressionMultiply* ConfidenceTimesMultiplier = CreateMultiply(Material, -460, 330);
    UMaterialExpressionMultiply* MaskTimesConfidence = CreateMultiply(Material, -280, 240);
    UMaterialExpressionMultiply* TimesBlend = CreateMultiply(Material, -100, 300);
    UMaterialExpressionMultiply* TimesVertexWetness = CreateMultiply(Material, 80, 360);
    UMaterialExpressionMultiply* RevealAlpha = CreateMultiply(Material, 260, 420);
    UMaterialExpressionLinearInterpolate* Lerp = Cast<UMaterialExpressionLinearInterpolate>(
        UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionLinearInterpolate::StaticClass(), 450, -70));

    if (TextureCoordinate == nullptr || BaseColorInput == nullptr || RevealColor == nullptr || RevealMask == nullptr ||
        RevealConfidence == nullptr || MaskRed == nullptr || ConfidenceRed == nullptr || VertexColor == nullptr ||
        VertexWetnessRed == nullptr || RevealBlend == nullptr || MaskMultiplier == nullptr || ConfidenceMultiplier == nullptr || UseRevealPreview == nullptr ||
        MaskTimesMultiplier == nullptr || ConfidenceTimesMultiplier == nullptr || MaskTimesConfidence == nullptr ||
        TimesBlend == nullptr || TimesVertexWetness == nullptr || RevealAlpha == nullptr || Lerp == nullptr)
    {
        return false;
    }

    TextureCoordinate->CoordinateIndex = OuterLayer.OuterUVChannel;

    bool bConnected = true;
    bConnected &= ConnectMaterialExpressionsChecked(TextureCoordinate, FString(), RevealColor, TEXT("UVs"));
    bConnected &= ConnectMaterialExpressionsChecked(TextureCoordinate, FString(), RevealMask, TEXT("UVs"));
    bConnected &= ConnectMaterialExpressionsChecked(TextureCoordinate, FString(), RevealConfidence, TEXT("UVs"));
    MaskRed->Input.Connect(0, RevealMask);
    ConfidenceRed->Input.Connect(0, RevealConfidence);
    bConnected &= ConnectMaterialExpressionsChecked(MaskRed, FString(), MaskTimesMultiplier, TEXT("A"));
    bConnected &= ConnectMaterialExpressionsChecked(MaskMultiplier, FString(), MaskTimesMultiplier, TEXT("B"));
    bConnected &= ConnectMaterialExpressionsChecked(ConfidenceRed, FString(), ConfidenceTimesMultiplier, TEXT("A"));
    bConnected &= ConnectMaterialExpressionsChecked(ConfidenceMultiplier, FString(), ConfidenceTimesMultiplier, TEXT("B"));
    bConnected &= ConnectMaterialExpressionsChecked(MaskTimesMultiplier, FString(), MaskTimesConfidence, TEXT("A"));
    bConnected &= ConnectMaterialExpressionsChecked(ConfidenceTimesMultiplier, FString(), MaskTimesConfidence, TEXT("B"));
    bConnected &= ConnectMaterialExpressionsChecked(MaskTimesConfidence, FString(), TimesBlend, TEXT("A"));
    bConnected &= ConnectMaterialExpressionsChecked(RevealBlend, FString(), TimesBlend, TEXT("B"));
    VertexWetnessRed->Input.Connect(0, VertexColor);
    bConnected &= ConnectMaterialExpressionsChecked(TimesBlend, FString(), TimesVertexWetness, TEXT("A"));
    bConnected &= ConnectMaterialExpressionsChecked(VertexWetnessRed, FString(), TimesVertexWetness, TEXT("B"));
    bConnected &= ConnectMaterialExpressionsChecked(TimesVertexWetness, FString(), RevealAlpha, TEXT("A"));
    bConnected &= ConnectMaterialExpressionsChecked(UseRevealPreview, FString(), RevealAlpha, TEXT("B"));
    bConnected &= ConnectMaterialExpressionsChecked(BaseColorInput, BaseColorOutputName, Lerp, TEXT("A"));
    bConnected &= ConnectMaterialExpressionsChecked(RevealColor, FString(), Lerp, TEXT("B"));
    bConnected &= ConnectMaterialExpressionsChecked(RevealAlpha, FString(), Lerp, TEXT("Alpha"));
    bConnected &= UMaterialEditingLibrary::ConnectMaterialProperty(Lerp, FString(), MP_BaseColor);

    if (bConnected)
    {
        UMaterialEditingLibrary::RecompileMaterial(Material);
    }
    const TArray<FString> CompileErrors;
    Material->PostEditChange();
    Material->MarkPackageDirty();
    return bConnected && CompileErrors.Num() == 0;
}

UMaterialInstanceConstant* FDWCRevealBakeMaterialBuilder::CreateRevealMaterialInstanceForSource(
    const UMaterialInstance* SourceInstance,
    UMaterialInterface*      RevealParent,
    const FString&           AssetNamePrefix,
    const FString&           TargetPackagePath)
{
    if (SourceInstance == nullptr || RevealParent == nullptr)
    {
        return nullptr;
    }

    FString            UniquePackageName;
    FString            UniqueAssetName;
    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    AssetToolsModule.Get().CreateUniqueAssetName(
        (!TargetPackagePath.IsEmpty()
            ? TargetPackagePath
            : FString(FDWCRevealBakeUtilities::GetDefaultRevealBakePackagePath())) / (AssetNamePrefix + TEXT("_MI")),
        FString(),
        UniquePackageName,
        UniqueAssetName);

    UPackage* Package = CreatePackage(*UniquePackageName);
    if (Package == nullptr)
    {
        return nullptr;
    }

    UMaterialInstanceConstant* RevealInstance = NewObject<UMaterialInstanceConstant>(
        Package,
        *UniqueAssetName,
        RF_Public | RF_Standalone | RF_Transactional);
    if (RevealInstance == nullptr)
    {
        return nullptr;
    }

    RevealInstance->Modify();
    RevealInstance->SetParentEditorOnly(RevealParent);
    RevealInstance->ScalarParameterValues = SourceInstance->ScalarParameterValues;
    RevealInstance->VectorParameterValues = SourceInstance->VectorParameterValues;
    RevealInstance->DoubleVectorParameterValues = SourceInstance->DoubleVectorParameterValues;
    RevealInstance->TextureParameterValues = SourceInstance->TextureParameterValues;
    RevealInstance->TextureCollectionParameterValues = SourceInstance->TextureCollectionParameterValues;
    RevealInstance->ParameterCollectionParameterValues = SourceInstance->ParameterCollectionParameterValues;
    RevealInstance->RuntimeVirtualTextureParameterValues = SourceInstance->RuntimeVirtualTextureParameterValues;
    RevealInstance->SparseVolumeTextureParameterValues = SourceInstance->SparseVolumeTextureParameterValues;
    RevealInstance->FontParameterValues = SourceInstance->FontParameterValues;
    RevealInstance->UserSceneTextureOverrides = SourceInstance->UserSceneTextureOverrides;

    FStaticParameterSet StaticParameters = SourceInstance->GetStaticParameters();
    FMaterialInstanceBasePropertyOverrides BasePropertyOverrides = SourceInstance->BasePropertyOverrides;
    RevealInstance->UpdateStaticPermutation(StaticParameters, BasePropertyOverrides);
    RevealInstance->PostEditChange();
    RevealInstance->MarkPackageDirty();
    Package->MarkPackageDirty();
    FAssetRegistryModule::AssetCreated(RevealInstance);
    return RevealInstance;
}

void FDWCRevealBakeMaterialBuilder::ApplyLookupPreviewToOuterMaterials(
    const AActor&                   Actor,
    const UDWCBakeComponent&        BakeComponent,
    const FDWCBakeSnapshot&         Snapshot,
    const FDWCBakeResolvedLayer&    OuterLayer,
    const FDWCRevealBakeTextureSet& TextureSet,
    const TArray<FName>&            SourceLayerIds)
{
    USkeletalMeshComponent* OuterComponent = FindLayerComponent(Actor, OuterLayer);
    if (OuterComponent == nullptr)
    {
        return;
    }

    TArray<UTexture*> SourceTextures;
    SourceTextures.Reserve(SourceLayerIds.Num());
    for (const FName SourceLayerId : SourceLayerIds)
    {
        const FDWCBakeResolvedLayer* SourceLayer = FDWCRevealBakeSourceResolver::FindResolvedLayerById(Snapshot, SourceLayerId);
        SourceTextures.Add(SourceLayer != nullptr ? FDWCRevealBakeSourceResolver::ResolvePreviewSourceTexture(*SourceLayer) : nullptr);
    }

    const float Blend = FMath::Clamp(BakeComponent.RevealPreviewBlendPercent / 100.0f, 0.0f, 1.0f);
    const float UsePreview = Blend > 0.0f ? 1.0f : 0.0f;

    const int32 MaterialCount = OuterComponent->GetNumMaterials();
    for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
    {
        UMaterialInterface* SourceMaterial = OuterComponent->GetMaterial(MaterialIndex);
        const FString RevealMaterialPrefix = FString::Printf(
            TEXT("M_DWCReveal_%s_%s_%d"),
            *FDWCRevealBakeUtilities::SanitizeAssetToken(Actor.GetName()),
            *FDWCRevealBakeUtilities::SanitizeAssetToken(OuterLayer.LayerId.ToString()),
            MaterialIndex);
        if (UMaterialInterface* ConfiguredRevealMaterial = CreateConfiguredRevealMaterial(
                SourceMaterial,
                RevealMaterialPrefix,
                BakeComponent,
                OuterLayer,
                TextureSet))
        {
            OuterComponent->SetMaterial(MaterialIndex, ConfiguredRevealMaterial);
        }

        UMaterialInstanceDynamic* MID = OuterComponent->CreateAndSetMaterialInstanceDynamic(MaterialIndex);
        if (MID == nullptr)
        {
            continue;
        }

        if (!BakeComponent.UseRevealPreviewParameterName.IsNone())
        {
            MID->SetScalarParameterValue(BakeComponent.UseRevealPreviewParameterName, UsePreview);
        }

        if (!BakeComponent.RevealPreviewBlendParameterName.IsNone())
        {
            MID->SetScalarParameterValue(BakeComponent.RevealPreviewBlendParameterName, Blend);
        }

        if (!BakeComponent.RevealMaskMultiplierParameterName.IsNone())
        {
            MID->SetScalarParameterValue(
                BakeComponent.RevealMaskMultiplierParameterName,
                FMath::Max(0.0f, BakeComponent.RevealMaskMultiplier));
        }

        if (!BakeComponent.RevealConfidenceMultiplierParameterName.IsNone())
        {
            MID->SetScalarParameterValue(
                BakeComponent.RevealConfidenceMultiplierParameterName,
                FMath::Max(0.0f, BakeComponent.RevealConfidenceMultiplier));
        }

        SetTextureParameterIfValid(*MID, BakeComponent.RevealLookupMapParameterName, TextureSet.LookupMap);
        SetTextureParameterIfValid(*MID, BakeComponent.RevealColorMapParameterName, TextureSet.ColorMap);
        SetTextureParameterIfValid(*MID, BakeComponent.RevealMaskMapParameterName, TextureSet.MaskMap);
        SetTextureParameterIfValid(*MID, BakeComponent.RevealConfidenceMapParameterName, TextureSet.ConfidenceMap);

        if (SourceTextures.IsValidIndex(0))
        {
            SetTextureParameterIfValid(*MID, BakeComponent.RevealSourceTexture0ParameterName, SourceTextures[0]);
        }
        if (SourceTextures.IsValidIndex(1))
        {
            SetTextureParameterIfValid(*MID, BakeComponent.RevealSourceTexture1ParameterName, SourceTextures[1]);
        }
        if (SourceTextures.IsValidIndex(2))
        {
            SetTextureParameterIfValid(*MID, BakeComponent.RevealSourceTexture2ParameterName, SourceTextures[2]);
        }
        if (SourceTextures.IsValidIndex(3))
        {
            SetTextureParameterIfValid(*MID, BakeComponent.RevealSourceTexture3ParameterName, SourceTextures[3]);
        }
    }

    OuterComponent->MarkRenderStateDirty();
}

UMaterialInterface* FDWCRevealBakeMaterialBuilder::CreateConfiguredRevealMaterial(
    UMaterialInterface*             SourceMaterial,
    const FString&                  AssetNamePrefix,
    const UDWCBakeComponent&        BakeComponent,
    const FDWCBakeResolvedLayer&    OuterLayer,
    const FDWCRevealBakeTextureSet& TextureSet,
    const FString&                  TargetPackagePath)
{
    UMaterial* RevealMaterial = DuplicateRevealMaterial(SourceMaterial, AssetNamePrefix, TargetPackagePath);
    if (RevealMaterial == nullptr)
    {
        return nullptr;
    }

    if (!ConfigureRevealMaterialGraph(RevealMaterial, BakeComponent, OuterLayer, TextureSet))
    {
        return nullptr;
    }

    if (const UMaterialInstance* SourceInstance = Cast<UMaterialInstance>(SourceMaterial))
    {
        if (UMaterialInstanceConstant* RevealInstance = CreateRevealMaterialInstanceForSource(
                SourceInstance,
                RevealMaterial,
                AssetNamePrefix,
                TargetPackagePath))
        {
            return RevealInstance;
        }
    }

    return RevealMaterial;
}
