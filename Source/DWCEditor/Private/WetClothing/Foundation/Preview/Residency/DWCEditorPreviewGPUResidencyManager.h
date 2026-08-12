// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorTextureWorkspaceTypes.h"

class FDWCEditorRenderUploadQueue;
class FDWCEditorTextureWorkspace;

enum class EDWCEditorPreviewGPUDomain : uint8
{
    None,
    WetPart,
    Wrinkle,
    Transparency
};

/**
 * Session-level policy for transient preview GPU residency.
 *
 * Viewports own workspace leases. This manager only coordinates mode activity,
 * upload completion polling, and retirement of unleased resources.
 */
class FDWCEditorPreviewGPUResidencyManager final
{
  public:
    FDWCEditorPreviewGPUResidencyManager(
        TSharedRef<FDWCEditorRenderUploadQueue> InUploadQueue,
        TSharedRef<FDWCEditorTextureWorkspace> InTextureWorkspace);

    void SetActiveDomain(EDWCEditorPreviewGPUDomain Domain);
    void SuspendDomain(EDWCEditorPreviewGPUDomain Domain);
    void SuspendAll();
    void Tick();
    void Shutdown();

    EDWCEditorPreviewGPUDomain GetActiveDomain() const { return ActiveDomain; }
    bool IsSuspended() const { return bAllSuspended; }

    static EDWCEditorPreviewGPUDomain GetDomainForPurpose(EDWCEditorTexturePurpose Purpose);

  private:
    uint64 RetireDomainResources(EDWCEditorPreviewGPUDomain Domain);

    TSharedRef<FDWCEditorRenderUploadQueue> UploadQueue;
    TSharedRef<FDWCEditorTextureWorkspace> TextureWorkspace;
    EDWCEditorPreviewGPUDomain ActiveDomain = EDWCEditorPreviewGPUDomain::None;
    bool bAllSuspended = false;
    bool bShuttingDown = false;
};
