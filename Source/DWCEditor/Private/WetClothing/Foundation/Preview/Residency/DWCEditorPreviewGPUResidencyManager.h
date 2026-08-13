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

/** Resource retention policy selected from the editor lifecycle transition. */
enum class EDWCEditorPreviewResourceReleasePolicy : uint8
{
    /** Mode switches keep CPU working data warm but release unleased GPU textures. */
    ModeSwitch,
    /** Short host visibility changes use grace periods so a quick return is cheap. */
    DeferredHostInactive,
    /** PIE, builds, minimization, and shutdown release all unleased domain data now. */
    Immediate
};

struct FDWCEditorPreviewResidencyPolicy
{
    double HostInactiveGPUGraceSeconds = 0.75;
    double HostInactiveCPUGraceSeconds = 5.0;
};

struct FDWCEditorPreviewResidencyDiagnostics
{
    uint64 RetiringGPUBytes = 0;
    uint64 ReclaimedCPUBytes = 0;
    uint64 ModeSwitchReleaseCount = 0;
    uint64 DeferredReleaseCount = 0;
    uint64 ImmediateReleaseCount = 0;
    int32 PendingGPUReleaseCount = 0;
    int32 PendingCPUReleaseCount = 0;
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
        TSharedRef<FDWCEditorTextureWorkspace> InTextureWorkspace,
        FDWCEditorPreviewResidencyPolicy InPolicy = {});

    void SetActiveDomain(EDWCEditorPreviewGPUDomain Domain);
    void SuspendDomain(
        EDWCEditorPreviewGPUDomain Domain,
        EDWCEditorPreviewResourceReleasePolicy ReleasePolicy =
            EDWCEditorPreviewResourceReleasePolicy::ModeSwitch,
        double CurrentTimeSeconds = -1.0);
    void SuspendAll(
        EDWCEditorPreviewResourceReleasePolicy ReleasePolicy =
            EDWCEditorPreviewResourceReleasePolicy::Immediate,
        double CurrentTimeSeconds = -1.0);
    void Tick(double CurrentTimeSeconds = -1.0);
    void Shutdown();

    EDWCEditorPreviewGPUDomain GetActiveDomain() const { return ActiveDomain; }
    bool IsSuspended() const { return bAllSuspended; }
    bool HasPendingMaintenance() const;
    FDWCEditorPreviewResidencyDiagnostics GetDiagnostics() const;

    static EDWCEditorPreviewGPUDomain GetDomainForPurpose(EDWCEditorTexturePurpose Purpose);

  private:
    struct FPendingDomainRelease
    {
        double GPUDeadlineSeconds = 0.0;
        double CPUDeadlineSeconds = 0.0;
        bool bGPUReleasePending = false;
        bool bCPUReleasePending = false;

        void Reset() { *this = {}; }
    };

    static double ResolveTimeSeconds(double CurrentTimeSeconds);
    FPendingDomainRelease& GetPendingRelease(EDWCEditorPreviewGPUDomain Domain);
    const FPendingDomainRelease& GetPendingRelease(EDWCEditorPreviewGPUDomain Domain) const;
    uint64 ReclaimDomainCPUResources(EDWCEditorPreviewGPUDomain Domain);
    uint64 RetireDomainResources(EDWCEditorPreviewGPUDomain Domain);
    void ApplyDueReleases(double CurrentTimeSeconds);

    TSharedRef<FDWCEditorRenderUploadQueue> UploadQueue;
    TSharedRef<FDWCEditorTextureWorkspace> TextureWorkspace;
    FDWCEditorPreviewResidencyPolicy Policy;
    FPendingDomainRelease PendingReleases[4];
    uint64 RetiringGPUBytes = 0;
    uint64 ReclaimedCPUBytes = 0;
    uint64 ModeSwitchReleaseCount = 0;
    uint64 DeferredReleaseCount = 0;
    uint64 ImmediateReleaseCount = 0;
    EDWCEditorPreviewGPUDomain ActiveDomain = EDWCEditorPreviewGPUDomain::None;
    bool bAllSuspended = false;
    bool bShuttingDown = false;
};
