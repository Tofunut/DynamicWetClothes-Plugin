//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class UObject;
class UPackage;
class UWetClothingAsset;
enum class EDWCTransparencyTempArtifactKind : uint8;

/** Cook boundary for persistent, rebuildable Transparency Editor artifacts. */
class FDWCTransparencyIntermediateAssetPolicy
{
  public:
    static bool IsIntermediateArtifactKind(EDWCTransparencyTempArtifactKind Kind);
    static bool IsIntermediatePackagePath(const FString& PackageName);

    /**
     * Marks a loaded intermediate package editor-only. The path check prevents
     * final runtime textures from being excluded accidentally.
     */
    static bool EnsureEditorOnlyPackage(
        UObject& Artifact,
        bool* OutChanged = nullptr,
        FString* OutError = nullptr);

    /** Returns true when the referenced package cannot enter a runtime cook. */
    static bool IsReferenceCookExcluded(
        const FSoftObjectPath& ArtifactPath,
        FString* OutReason = nullptr);

    /** Returns the persisted or loaded package flag without loading the asset. */
    static bool HasEditorOnlyPackageFlag(const FSoftObjectPath& AssetPath);

    /** Repairs only already-loaded references, preserving lazy cache loading. */
    static void RepairLoadedReferences(
        UWetClothingAsset& Asset,
        TArray<UPackage*>& OutChangedPackages,
        TArray<FString>& OutWarnings);
};
