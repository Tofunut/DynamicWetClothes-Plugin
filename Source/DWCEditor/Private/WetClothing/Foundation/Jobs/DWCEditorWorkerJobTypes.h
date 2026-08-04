#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Authoring/DWCEditorAuthoringTypes.h"

enum class EDWCEditorWorkerJobKind : uint8
{
    WrinkleAccumulatedPreview,
    TransparencyVisualization,
    WrinkleBake,
    TransparencyAutoBake,
    TransparencyFinalBake
};

enum class EDWCEditorWorkerJobPriority : uint8
{
    Background,
    UserInitiated,
    Interactive
};

enum class EDWCEditorWorkerJobCompletion : uint8
{
    Applied,
    Canceled,
    Superseded,
    Stale,
    Failed
};

struct FDWCEditorWorkerJobKey
{
    EDWCEditorWorkerJobKind Kind = EDWCEditorWorkerJobKind::WrinkleAccumulatedPreview;
    int32 MaterialSlotIndex = INDEX_NONE;
    FGuid LayerGuid;

    bool operator==(const FDWCEditorWorkerJobKey& Other) const
    {
        return Kind == Other.Kind &&
            MaterialSlotIndex == Other.MaterialSlotIndex &&
            LayerGuid == Other.LayerGuid;
    }

    friend uint32 GetTypeHash(const FDWCEditorWorkerJobKey& Key)
    {
        uint32 Hash = GetTypeHash(static_cast<uint8>(Key.Kind));
        Hash = HashCombine(Hash, GetTypeHash(Key.MaterialSlotIndex));
        return HashCombine(Hash, GetTypeHash(Key.LayerGuid));
    }
};

struct FDWCEditorWorkerJobDescriptor
{
    FDWCEditorWorkerJobKey Key;
    EDWCEditorAuthoringDomain Domain = EDWCEditorAuthoringDomain::None;
    uint64 DomainRevision = 0;
    EDWCEditorWorkerJobPriority Priority = EDWCEditorWorkerJobPriority::Background;
    uint64 EstimatedBytes = 0;
    FString DebugName;
};

struct FDWCEditorWorkerJobTicket
{
    FDWCEditorWorkerJobKey Key;
    uint64 JobId = 0;
    uint64 Generation = 0;
    EDWCEditorAuthoringDomain Domain = EDWCEditorAuthoringDomain::None;
    uint64 DomainRevision = 0;

    bool IsValid() const { return JobId != 0 && Generation != 0; }
};

struct FDWCEditorWorkerJobResult
{
    virtual ~FDWCEditorWorkerJobResult() = default;

    bool bSucceeded = true;
    FString Error;
    uint64 ResultBytes = 0;
};
