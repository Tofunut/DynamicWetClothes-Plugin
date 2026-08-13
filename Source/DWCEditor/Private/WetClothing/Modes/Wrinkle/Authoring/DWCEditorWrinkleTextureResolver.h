// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UTexture2D;
struct FWetWrinkleBakedMapSet;
struct FWetWrinklePatchPlacement;

enum class EDWCEditorWrinkleTextureResolveStatus : uint8
{
    Unset,
    Unloaded,
    Ready,
    Missing,
    WrongType,
    Unreadable
};

/** Metadata-first result for an authored wrinkle texture reference. */
struct FDWCEditorWrinkleTextureReferenceSnapshot
{
    EDWCEditorWrinkleTextureResolveStatus Status =
        EDWCEditorWrinkleTextureResolveStatus::Unset;
    FSoftObjectPath ObjectPath;
    TWeakObjectPtr<UTexture2D> Texture;
    FGuid SourceId;
    FIntPoint SourceSize = FIntPoint::ZeroValue;
    bool bFlipGreenChannel = false;
    FString Detail;

    bool HasReference() const { return ObjectPath.IsValid(); }
    bool IsReady() const
    {
        return Status == EDWCEditorWrinkleTextureResolveStatus::Ready &&
            Texture.IsValid() && SourceId.IsValid() && SourceSize.X > 0 && SourceSize.Y > 0;
    }
};

/**
 * The only editor path allowed to turn persistent wrinkle soft references into
 * loaded Texture2D objects. Metadata queries never load the referenced package.
 */
class FDWCEditorWrinkleTextureResolver final
{
  public:
    static FDWCEditorWrinkleTextureReferenceSnapshot InspectSource(
        const FWetWrinklePatchPlacement& Patch);

    static FDWCEditorWrinkleTextureReferenceSnapshot ResolveSource(
        const FWetWrinklePatchPlacement& Patch,
        bool bRequireReadableSource = true);

    static FDWCEditorWrinkleTextureReferenceSnapshot InspectEditorMask(
        const FWetWrinkleBakedMapSet& BakedMap);

    static FDWCEditorWrinkleTextureReferenceSnapshot ResolveEditorMask(
        const FWetWrinkleBakedMapSet& BakedMap,
        bool bRequireReadableSource = true);

    static FDWCEditorWrinkleTextureReferenceSnapshot ResolveEditorMaskReference(
        const TSoftObjectPtr<UTexture2D>& Reference,
        bool bRequireReadableSource = true);

  private:
    static FDWCEditorWrinkleTextureReferenceSnapshot Resolve(
        const TSoftObjectPtr<UTexture2D>& Reference,
        bool bLoad,
        bool bRequireReadableSource,
        const TCHAR* Role);
};
