//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "WetClothing/Foundation/Async/DWCEditorAsyncOperationTypes.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobTypes.h"

namespace DWCEditorWorkerAsyncCompatibility
{
    inline FName GetOperationNamespace(const EDWCEditorWorkerJobKind Kind)
    {
        switch (Kind)
        {
        case EDWCEditorWorkerJobKind::WrinkleAccumulatedPreview:
            return TEXT("WrinkleAccumulatedPreview");
        case EDWCEditorWorkerJobKind::TransparencyVisualization:
            return TEXT("TransparencyVisualization");
        case EDWCEditorWorkerJobKind::WrinkleBake:
            return TEXT("WrinkleBake");
        case EDWCEditorWorkerJobKind::TransparencyAutoBake:
            return TEXT("TransparencyAutoBake");
        case EDWCEditorWorkerJobKind::TransparencyFinalBake:
            return TEXT("TransparencyFinalBake");
        default:
            return NAME_None;
        }
    }

    inline FDWCEditorAsyncOperationKey MakeOperationKey(const FDWCEditorWorkerJobKey& Key)
    {
        FDWCEditorAsyncOperationKey Result;
        Result.Namespace = GetOperationNamespace(Key.Kind);
        Result.MaterialSlotIndex = Key.MaterialSlotIndex;
        Result.ResourceGuid = Key.LayerGuid;
        return Result;
    }

    inline FDWCEditorMemoryBreakdown MakeMemoryBreakdown(
        const FDWCEditorWorkerMemoryEstimate& Estimate)
    {
        FDWCEditorMemoryBreakdown Result;
        Result.SharedResidentBytes = Estimate.ResidentSharedBytes;
        Result.SnapshotBytes = Estimate.SnapshotBytes;
        Result.WorkingBytes = Estimate.WorkingBytes;
        Result.OutputBytes = Estimate.OutputBytes;
        Result.ScratchBytes = Estimate.ScratchBytes;
        return Result;
    }

    inline FDWCEditorAsyncOperationIdentity MakeOperationIdentity(
        const FDWCEditorWorkerJobTicket& Ticket,
        const FGuid& SessionEpoch)
    {
        FDWCEditorAsyncOperationIdentity Result;
        Result.Key = MakeOperationKey(Ticket.Key);
        Result.SessionEpoch = SessionEpoch;
        Result.OperationId = Ticket.JobId;
        Result.Generation = Ticket.Generation;
        Result.Domain = Ticket.Domain;
        Result.DomainRevision = Ticket.DomainRevision;
        return Result;
    }
}
