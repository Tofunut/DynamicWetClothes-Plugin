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
    TSharedRef<FDWCEditorTextureWorkspace> InTextureWorkspace)
    : UploadQueue(MoveTemp(InUploadQueue))
    , TextureWorkspace(MoveTemp(InTextureWorkspace))
{
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
}

void FDWCEditorPreviewGPUResidencyManager::SuspendDomain(
    const EDWCEditorPreviewGPUDomain Domain)
{
    check(IsInGameThread());
    if (Domain == EDWCEditorPreviewGPUDomain::None)
    {
        return;
    }
    RetireDomainResources(Domain);
    if (ActiveDomain == Domain)
    {
        ActiveDomain = EDWCEditorPreviewGPUDomain::None;
    }
}

void FDWCEditorPreviewGPUResidencyManager::SuspendAll()
{
    check(IsInGameThread());
    for (const EDWCEditorPreviewGPUDomain Domain : {
             EDWCEditorPreviewGPUDomain::WetPart,
             EDWCEditorPreviewGPUDomain::Wrinkle,
             EDWCEditorPreviewGPUDomain::Transparency})
    {
        RetireDomainResources(Domain);
    }
    ActiveDomain = EDWCEditorPreviewGPUDomain::None;
    bAllSuspended = true;
}

void FDWCEditorPreviewGPUResidencyManager::Tick()
{
    check(IsInGameThread());
    if (bShuttingDown)
    {
        return;
    }
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
    SuspendAll();
    bShuttingDown = true;
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
