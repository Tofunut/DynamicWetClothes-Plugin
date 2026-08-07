//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Async/DWCEditorAsyncOperationTypes.h"

class FDWCEditorAsyncOperationContract
{
public:
    static bool CanTransition(
        EDWCEditorAsyncOperationState From,
        EDWCEditorAsyncOperationState To);

    static bool ValidateTransition(
        EDWCEditorAsyncOperationState From,
        EDWCEditorAsyncOperationState To,
        const TCHAR* OperationDebugName);

    static bool CanTransitionCancellation(
        EDWCEditorAsyncCancellationState From,
        EDWCEditorAsyncCancellationState To);

    static bool ValidateCancellationTransition(
        EDWCEditorAsyncCancellationState From,
        EDWCEditorAsyncCancellationState To,
        const TCHAR* OperationDebugName);

    static bool CanCommit(
        const FDWCEditorAsyncOperationIdentity& Identity,
        const FGuid& CurrentSessionEpoch,
        uint64 CurrentGeneration,
        uint64 CurrentDomainRevision);

    static bool IsCPUResourcePool(EDWCEditorResourcePool Pool);
    static const TCHAR* LexToString(EDWCEditorAsyncOperationState State);
    static const TCHAR* LexToString(EDWCEditorResourcePool Pool);
};
