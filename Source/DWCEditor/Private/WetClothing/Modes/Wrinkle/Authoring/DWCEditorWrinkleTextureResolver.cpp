// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/Modes/Wrinkle/Authoring/DWCEditorWrinkleTextureResolver.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "DataAssets/WetClothingWrinkleData.h"
#include "Engine/Texture2D.h"
#include "Modules/ModuleManager.h"
#include "WetClothing/Foundation/Diagnostics/DWCEditorAuthoringPayloadDiagnostics.h"

namespace
{
    void PopulateLoadedTexture(
        UTexture2D& Texture,
        const bool bRequireReadableSource,
        const TCHAR* Role,
        FDWCEditorWrinkleTextureReferenceSnapshot& OutSnapshot)
    {
        OutSnapshot.Texture = &Texture;
        OutSnapshot.SourceId = Texture.Source.GetId();
        OutSnapshot.SourceSize = FIntPoint(Texture.Source.GetSizeX(), Texture.Source.GetSizeY());
        OutSnapshot.bFlipGreenChannel = Texture.bFlipGreenChannel;
        if (bRequireReadableSource &&
            (!Texture.Source.IsValid() || !OutSnapshot.SourceId.IsValid() ||
             OutSnapshot.SourceSize.X <= 0 || OutSnapshot.SourceSize.Y <= 0))
        {
            OutSnapshot.Status = EDWCEditorWrinkleTextureResolveStatus::Unreadable;
            OutSnapshot.Detail = FString::Printf(
                TEXT("The %s texture '%s' has no readable editor source data."),
                Role,
                *Texture.GetPathName());
            return;
        }

        OutSnapshot.Status = EDWCEditorWrinkleTextureResolveStatus::Ready;
        OutSnapshot.Detail.Reset();
    }
}

FDWCEditorWrinkleTextureReferenceSnapshot FDWCEditorWrinkleTextureResolver::InspectSource(
    const FWetWrinklePatchPlacement& Patch)
{
    return Resolve(Patch.WrinkleNormalTexture, false, false, TEXT("wrinkle source"));
}

FDWCEditorWrinkleTextureReferenceSnapshot FDWCEditorWrinkleTextureResolver::ResolveSource(
    const FWetWrinklePatchPlacement& Patch,
    const bool bRequireReadableSource)
{
    return Resolve(
        Patch.WrinkleNormalTexture,
        true,
        bRequireReadableSource,
        TEXT("wrinkle source"));
}

FDWCEditorWrinkleTextureReferenceSnapshot FDWCEditorWrinkleTextureResolver::InspectEditorMask(
    const FWetWrinkleBakedMapSet& BakedMap)
{
#if WITH_EDITORONLY_DATA
    return Resolve(BakedMap.BakedWrinkleMask, false, false, TEXT("editor wrinkle coverage mask"));
#else
    return {};
#endif
}

FDWCEditorWrinkleTextureReferenceSnapshot FDWCEditorWrinkleTextureResolver::ResolveEditorMask(
    const FWetWrinkleBakedMapSet& BakedMap,
    const bool bRequireReadableSource)
{
#if WITH_EDITORONLY_DATA
    return ResolveEditorMaskReference(BakedMap.BakedWrinkleMask, bRequireReadableSource);
#else
    return {};
#endif
}

FDWCEditorWrinkleTextureReferenceSnapshot
FDWCEditorWrinkleTextureResolver::ResolveEditorMaskReference(
    const TSoftObjectPtr<UTexture2D>& Reference,
    const bool bRequireReadableSource)
{
    return Resolve(
        Reference,
        true,
        bRequireReadableSource,
        TEXT("editor wrinkle coverage mask"));
}

FDWCEditorWrinkleTextureReferenceSnapshot FDWCEditorWrinkleTextureResolver::Resolve(
    const TSoftObjectPtr<UTexture2D>& Reference,
    const bool bLoad,
    const bool bRequireReadableSource,
    const TCHAR* Role)
{
    check(IsInGameThread());
    FDWCEditorWrinkleTextureReferenceSnapshot Result;
    Result.ObjectPath = Reference.ToSoftObjectPath();
    if (!Result.ObjectPath.IsValid())
    {
        Result.Detail = FString::Printf(TEXT("No %s texture is assigned."), Role);
        return Result;
    }

    if (UTexture2D* LoadedTexture = Reference.Get())
    {
        PopulateLoadedTexture(*LoadedTexture, bRequireReadableSource, Role, Result);
        return Result;
    }

    const FAssetData AssetData =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"))
            .Get()
            .GetAssetByObjectPath(Result.ObjectPath);
    if (!AssetData.IsValid())
    {
        Result.Status = EDWCEditorWrinkleTextureResolveStatus::Missing;
        Result.Detail = FString::Printf(
            TEXT("The %s texture '%s' does not exist."),
            Role,
            *Result.ObjectPath.ToString());
        return Result;
    }
    if (AssetData.AssetClassPath != UTexture2D::StaticClass()->GetClassPathName())
    {
        Result.Status = EDWCEditorWrinkleTextureResolveStatus::WrongType;
        Result.Detail = FString::Printf(
            TEXT("The %s reference '%s' is not a Texture2D."),
            Role,
            *Result.ObjectPath.ToString());
        return Result;
    }
    if (!bLoad)
    {
        Result.Status = EDWCEditorWrinkleTextureResolveStatus::Unloaded;
        Result.Detail.Reset();
        return Result;
    }

    UTexture2D* Texture = Reference.LoadSynchronous();
    if (Texture == nullptr)
    {
        Result.Status = EDWCEditorWrinkleTextureResolveStatus::Missing;
        Result.Detail = FString::Printf(
            TEXT("The %s texture '%s' could not be loaded."),
            Role,
            *Result.ObjectPath.ToString());
        return Result;
    }
    FDWCEditorAuthoringPayloadDiagnostics::RecordExplicitLoad(Texture, Role);
    PopulateLoadedTexture(*Texture, bRequireReadableSource, Role, Result);
    return Result;
}
