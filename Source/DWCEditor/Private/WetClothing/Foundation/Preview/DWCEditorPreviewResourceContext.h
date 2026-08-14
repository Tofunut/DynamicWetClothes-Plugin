// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Assets/DWCEditorAssetResidency.h"

class FDWCEditorPreviewCommitCoordinator;
class FDWCEditorRenderUploadQueue;
class FDWCEditorTextureWorkspace;

/**
 * Read-only capability bundle for one WCA editor preview session.
 * SWCAEditorPanel creates and shuts down the resources; mode panels only use them.
 */
class FDWCEditorPreviewResourceContext final
{
  public:
    FDWCEditorPreviewResourceContext(
        TSharedRef<FDWCEditorRenderUploadQueue> InUploadQueue,
        TSharedRef<FDWCEditorTextureWorkspace> InTextureWorkspace,
        TSharedRef<FDWCEditorPreviewCommitCoordinator> InCommitCoordinator,
        TSharedPtr<FDWCEditorAssetResidencyRegistry> InAssetResidency = nullptr)
        : UploadQueue(MoveTemp(InUploadQueue)),
          TextureWorkspace(MoveTemp(InTextureWorkspace)),
          CommitCoordinator(MoveTemp(InCommitCoordinator)),
          AssetResidency(
              InAssetResidency.IsValid()
                  ? MoveTemp(InAssetResidency)
                  : MakeShared<FDWCEditorAssetResidencyRegistry>())
    {
    }

    TSharedRef<FDWCEditorRenderUploadQueue> GetUploadQueue() const { return UploadQueue; }
    TSharedRef<FDWCEditorTextureWorkspace> GetTextureWorkspace() const { return TextureWorkspace; }
    TSharedRef<FDWCEditorPreviewCommitCoordinator> GetCommitCoordinator() const
    {
        return CommitCoordinator;
    }
    TSharedRef<FDWCEditorAssetResidencyRegistry> GetAssetResidency() const
    {
        return AssetResidency.ToSharedRef();
    }

  private:
    TSharedRef<FDWCEditorRenderUploadQueue> UploadQueue;
    TSharedRef<FDWCEditorTextureWorkspace> TextureWorkspace;
    TSharedRef<FDWCEditorPreviewCommitCoordinator> CommitCoordinator;
    TSharedPtr<FDWCEditorAssetResidencyRegistry> AssetResidency;
};
