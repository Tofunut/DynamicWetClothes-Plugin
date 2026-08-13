// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/Foundation/Preview/Residency/DWCEditorPreviewGPUResidencyManager.h"

#include "WetClothing/Foundation/TextureWorkspace/DWCEditorRenderUploadQueue.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorTextureWorkspace.h"

namespace
{
    TConstArrayView<EDWCEditorTexturePurpose> GetPurposesForDomain(
        const EDWCEditorPreviewGPUDomain Domain)
    {
        static const EDWCEditorTexturePurpose WetPartPurposes[] = {
            EDWCEditorTexturePurpose::WetPartColor,
            EDWCEditorTexturePurpose::WetPartSelection,
            EDWCEditorTexturePurpose::WetPartSurfaceData,
            EDWCEditorTexturePurpose::WetPartSurfaceWetness,
            EDWCEditorTexturePurpose::WetPartSurfaceDroplet,
            EDWCEditorTexturePurpose::WetPartSurfaceFlowDroplet};
        static const EDWCEditorTexturePurpose WrinklePurposes[] = {
            EDWCEditorTexturePurpose::WrinkleAccumulated,
            EDWCEditorTexturePurpose::WrinkleProcedural,
            EDWCEditorTexturePurpose::WrinkleHover};
        static const EDWCEditorTexturePurpose TransparencyPurposes[] = {
            EDWCEditorTexturePurpose::TransparencyVisualization,
            EDWCEditorTexturePurpose::TransparencyHoverBaseline,
            EDWCEditorTexturePurpose::TransparencyHoverIslandMask};

        switch (Domain)
        {
        case EDWCEditorPreviewGPUDomain::WetPart:
            return WetPartPurposes;
        case EDWCEditorPreviewGPUDomain::Wrinkle:
            return WrinklePurposes;
        case EDWCEditorPreviewGPUDomain::Transparency:
            return TransparencyPurposes;
        case EDWCEditorPreviewGPUDomain::None:
        default:
            return {};
        }
    }
}

FDWCEditorPreviewGPUResidencyManager::FDWCEditorPreviewGPUResidencyManager(
    TSharedRef<FDWCEditorRenderUploadQueue> InUploadQueue,
    TSharedRef<FDWCEditorTextureWorkspace> InTextureWorkspace,
    FDWCEditorPreviewResidencyPolicy InPolicy)
    : UploadQueue(MoveTemp(InUploadQueue))
    , TextureWorkspace(MoveTemp(InTextureWorkspace))
    , Policy(InPolicy)
{
    Policy.HostInactiveGPUGraceSeconds = FMath::Max(0.0, Policy.HostInactiveGPUGraceSeconds);
    Policy.HostInactiveCPUGraceSeconds = FMath::Max(
        Policy.HostInactiveGPUGraceSeconds,
        Policy.HostInactiveCPUGraceSeconds);
}

void FDWCEditorPreviewGPUResidencyManager::SetActiveDomain(
    const EDWCEditorPreviewGPUDomain Domain)
{
    check(IsInGameThread());
    if (bShuttingDown)
    {
        return;
    }
    ActiveDomain = Domain;
    bAllSuspended = false;
    if (Domain != EDWCEditorPreviewGPUDomain::None)
    {
        GetPendingRelease(Domain).Reset();
    }
}

void FDWCEditorPreviewGPUResidencyManager::SuspendDomain(
    const EDWCEditorPreviewGPUDomain Domain,
    const EDWCEditorPreviewResourceReleasePolicy ReleasePolicy,
    const double CurrentTimeSeconds)
{
    check(IsInGameThread());
    if (Domain == EDWCEditorPreviewGPUDomain::None)
    {
        return;
    }
    FPendingDomainRelease& Pending = GetPendingRelease(Domain);
    Pending.Reset();
    switch (ReleasePolicy)
    {
    case EDWCEditorPreviewResourceReleasePolicy::ModeSwitch:
        RetiringGPUBytes += RetireDomainResources(Domain);
        ++ModeSwitchReleaseCount;
        break;
    case EDWCEditorPreviewResourceReleasePolicy::DeferredHostInactive:
    {
        const double Now = ResolveTimeSeconds(CurrentTimeSeconds);
        Pending.GPUDeadlineSeconds = Now + Policy.HostInactiveGPUGraceSeconds;
        Pending.CPUDeadlineSeconds = Now + Policy.HostInactiveCPUGraceSeconds;
        Pending.bGPUReleasePending = true;
        Pending.bCPUReleasePending = true;
        ++DeferredReleaseCount;
        break;
    }
    case EDWCEditorPreviewResourceReleasePolicy::Immediate:
    default:
        ReclaimedCPUBytes += ReclaimDomainCPUResources(Domain);
        RetiringGPUBytes += RetireDomainResources(Domain);
        ++ImmediateReleaseCount;
        break;
    }
    if (ActiveDomain == Domain)
    {
        ActiveDomain = EDWCEditorPreviewGPUDomain::None;
    }
}

void FDWCEditorPreviewGPUResidencyManager::SuspendAll(
    const EDWCEditorPreviewResourceReleasePolicy ReleasePolicy,
    const double CurrentTimeSeconds)
{
    check(IsInGameThread());
    for (const EDWCEditorPreviewGPUDomain Domain : {
             EDWCEditorPreviewGPUDomain::WetPart,
             EDWCEditorPreviewGPUDomain::Wrinkle,
             EDWCEditorPreviewGPUDomain::Transparency})
    {
        SuspendDomain(Domain, ReleasePolicy, CurrentTimeSeconds);
    }
    ActiveDomain = EDWCEditorPreviewGPUDomain::None;
    bAllSuspended = true;
}

void FDWCEditorPreviewGPUResidencyManager::Tick(const double CurrentTimeSeconds)
{
    check(IsInGameThread());
    if (bShuttingDown)
    {
        return;
    }
    ApplyDueReleases(ResolveTimeSeconds(CurrentTimeSeconds));
    UploadQueue->Flush();
    TextureWorkspace->ProcessRetiredGPUResources();
}

void FDWCEditorPreviewGPUResidencyManager::Shutdown()
{
    check(IsInGameThread());
    if (bShuttingDown)
    {
        return;
    }
    SuspendAll(EDWCEditorPreviewResourceReleasePolicy::Immediate);
    bShuttingDown = true;
}

bool FDWCEditorPreviewGPUResidencyManager::HasPendingMaintenance() const
{
    check(IsInGameThread());
    for (const FPendingDomainRelease& Pending : PendingReleases)
    {
        if (Pending.bGPUReleasePending || Pending.bCPUReleasePending)
        {
            return true;
        }
    }
    return TextureWorkspace->HasRetiringGPUResources();
}

FDWCEditorPreviewResidencyDiagnostics FDWCEditorPreviewGPUResidencyManager::GetDiagnostics() const
{
    check(IsInGameThread());
    FDWCEditorPreviewResidencyDiagnostics Result;
    Result.RetiringGPUBytes = RetiringGPUBytes;
    Result.ReclaimedCPUBytes = ReclaimedCPUBytes;
    Result.ModeSwitchReleaseCount = ModeSwitchReleaseCount;
    Result.DeferredReleaseCount = DeferredReleaseCount;
    Result.ImmediateReleaseCount = ImmediateReleaseCount;
    for (const FPendingDomainRelease& Pending : PendingReleases)
    {
        Result.PendingGPUReleaseCount += Pending.bGPUReleasePending ? 1 : 0;
        Result.PendingCPUReleaseCount += Pending.bCPUReleasePending ? 1 : 0;
    }
    return Result;
}

EDWCEditorPreviewGPUDomain FDWCEditorPreviewGPUResidencyManager::GetDomainForPurpose(
    const EDWCEditorTexturePurpose Purpose)
{
    switch (Purpose)
    {
    case EDWCEditorTexturePurpose::WetPartColor:
    case EDWCEditorTexturePurpose::WetPartSelection:
    case EDWCEditorTexturePurpose::WetPartSurfaceData:
    case EDWCEditorTexturePurpose::WetPartSurfaceWetness:
    case EDWCEditorTexturePurpose::WetPartSurfaceDroplet:
    case EDWCEditorTexturePurpose::WetPartSurfaceFlowDroplet:
        return EDWCEditorPreviewGPUDomain::WetPart;
    case EDWCEditorTexturePurpose::WrinkleAccumulated:
    case EDWCEditorTexturePurpose::WrinkleProcedural:
    case EDWCEditorTexturePurpose::WrinkleHover:
        return EDWCEditorPreviewGPUDomain::Wrinkle;
    case EDWCEditorTexturePurpose::TransparencyVisualization:
    case EDWCEditorTexturePurpose::TransparencyHoverBaseline:
    case EDWCEditorTexturePurpose::TransparencyHoverIslandMask:
        return EDWCEditorPreviewGPUDomain::Transparency;
    default:
        return EDWCEditorPreviewGPUDomain::None;
    }
}

uint64 FDWCEditorPreviewGPUResidencyManager::RetireDomainResources(
    const EDWCEditorPreviewGPUDomain Domain)
{
    return TextureWorkspace->RetireUnleasedPurposes(GetPurposesForDomain(Domain));
}

double FDWCEditorPreviewGPUResidencyManager::ResolveTimeSeconds(const double CurrentTimeSeconds)
{
    return CurrentTimeSeconds >= 0.0 ? CurrentTimeSeconds : FPlatformTime::Seconds();
}

FDWCEditorPreviewGPUResidencyManager::FPendingDomainRelease&
FDWCEditorPreviewGPUResidencyManager::GetPendingRelease(const EDWCEditorPreviewGPUDomain Domain)
{
    return PendingReleases[static_cast<uint8>(Domain)];
}

const FDWCEditorPreviewGPUResidencyManager::FPendingDomainRelease&
FDWCEditorPreviewGPUResidencyManager::GetPendingRelease(const EDWCEditorPreviewGPUDomain Domain) const
{
    return PendingReleases[static_cast<uint8>(Domain)];
}

uint64 FDWCEditorPreviewGPUResidencyManager::ReclaimDomainCPUResources(
    const EDWCEditorPreviewGPUDomain Domain)
{
    uint64 NewlyRetiringGPUBytes = 0;
    const uint64 Bytes = TextureWorkspace->ReclaimUnleasedCPUBytesForPurposes(
        GetPurposesForDomain(Domain),
        MAX_uint64,
        &NewlyRetiringGPUBytes);
    RetiringGPUBytes += NewlyRetiringGPUBytes;
    return Bytes;
}

void FDWCEditorPreviewGPUResidencyManager::ApplyDueReleases(const double CurrentTimeSeconds)
{
    for (const EDWCEditorPreviewGPUDomain Domain : {
             EDWCEditorPreviewGPUDomain::WetPart,
             EDWCEditorPreviewGPUDomain::Wrinkle,
             EDWCEditorPreviewGPUDomain::Transparency})
    {
        FPendingDomainRelease& Pending = GetPendingRelease(Domain);
        if (Pending.bGPUReleasePending && CurrentTimeSeconds >= Pending.GPUDeadlineSeconds)
        {
            RetiringGPUBytes += RetireDomainResources(Domain);
            Pending.bGPUReleasePending = false;
        }
        if (Pending.bCPUReleasePending && CurrentTimeSeconds >= Pending.CPUDeadlineSeconds)
        {
            ReclaimedCPUBytes += ReclaimDomainCPUResources(Domain);
            RetiringGPUBytes += RetireDomainResources(Domain);
            Pending.bCPUReleasePending = false;
        }
    }
}
