// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/Foundation/Async/DWCEditorAsyncOperationTypes.h"

namespace
{
    bool TryAddBytes(const uint64 Value, uint64& InOutTotal)
    {
        if (Value > MAX_uint64 - InOutTotal)
        {
            return false;
        }
        InOutTotal += Value;
        return true;
    }
} // namespace

bool FDWCEditorMemoryBreakdown::TryGetOperationPrivateBytes(uint64& OutBytes) const
{
    OutBytes = 0;
    return TryAddBytes(SnapshotBytes, OutBytes) &&
           TryAddBytes(WorkingBytes, OutBytes) &&
           TryAddBytes(OutputBytes, OutBytes) &&
           TryAddBytes(ScratchBytes, OutBytes);
}

bool FDWCEditorMemoryBreakdown::TryGetTotalDescribedBytes(uint64& OutBytes) const
{
    OutBytes = 0;
    return TryAddBytes(SharedResidentBytes, OutBytes) &&
           TryAddBytes(SnapshotBytes, OutBytes) &&
           TryAddBytes(WorkingBytes, OutBytes) &&
           TryAddBytes(OutputBytes, OutBytes) &&
           TryAddBytes(ScratchBytes, OutBytes) &&
           TryAddBytes(UploadStagingBytes, OutBytes);
}

bool FDWCEditorMemoryBreakdown::IsEmpty() const
{
    return SharedResidentBytes == 0 && SnapshotBytes == 0 && WorkingBytes == 0 &&
           OutputBytes == 0 && ScratchBytes == 0 && UploadStagingBytes == 0;
}
