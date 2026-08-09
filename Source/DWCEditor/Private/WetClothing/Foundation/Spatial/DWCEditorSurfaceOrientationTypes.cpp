//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Spatial/DWCEditorSurfaceOrientationTypes.h"

#include "WetClothing/Foundation/Spatial/DWCEditorSurfacePatchProjectionVersion.h"

namespace
{
    bool IsFiniteDirection(const FVector3f& Direction)
    {
        return FMath::IsFinite(Direction.X) && FMath::IsFinite(Direction.Y) &&
            FMath::IsFinite(Direction.Z) && Direction.SizeSquared() > 0.25f;
    }

    bool FailContract(FString* OutError, const TCHAR* Message)
    {
        if (OutError != nullptr)
        {
            *OutError = Message;
        }
        return false;
    }
}

bool FDWCEditorSurfaceOrientationFieldEntry::IsValid() const
{
    if (TriangleIndex == INDEX_NONE)
    {
        return false;
    }
    for (const FPackedNormal& PackedDirection : CornerFallbackV)
    {
        if (!IsFiniteDirection(PackedDirection.ToFVector3f()))
        {
            return false;
        }
    }
    return true;
}

void FDWCEditorSurfaceOrientationField::Reset()
{
    BuildStatus = EDWCEditorSurfaceOrientationFieldBuildStatus::Unbuilt;
    PolicySignature = 0;
    FieldLayoutVersion = 0;
    EntryIndexByTriangle.Empty();
    Entries.Empty();
    Diagnostics = {};
}

bool FDWCEditorSurfaceOrientationField::IsEmpty() const
{
    return Entries.IsEmpty();
}

bool FDWCEditorSurfaceOrientationField::IsCompatible(const uint32 InPolicySignature) const
{
    return BuildStatus != EDWCEditorSurfaceOrientationFieldBuildStatus::Unbuilt &&
        InPolicySignature != 0 && PolicySignature == InPolicySignature &&
        FieldLayoutVersion == DWCEditorSurfaceOrientationVersion::FieldLayout;
}

const FDWCEditorSurfaceOrientationFieldEntry*
FDWCEditorSurfaceOrientationField::FindByTriangleIndex(const int32 TriangleIndex) const
{
    if (!EntryIndexByTriangle.IsValidIndex(TriangleIndex))
    {
        return nullptr;
    }
    const int32 EntryIndex = EntryIndexByTriangle[TriangleIndex];
    return Entries.IsValidIndex(EntryIndex) ? &Entries[EntryIndex] : nullptr;
}

uint64 FDWCEditorSurfaceOrientationField::GetAllocatedSizeBytes() const
{
    return static_cast<uint64>(EntryIndexByTriangle.GetAllocatedSize()) +
        static_cast<uint64>(Entries.GetAllocatedSize());
}

bool FDWCEditorSurfaceOrientationField::ValidateContract(
    const int32 TriangleCount,
    FString* OutError) const
{
    if (TriangleCount < 0)
    {
        return FailContract(OutError, TEXT("The surface orientation triangle count is invalid."));
    }
    if (Entries.IsEmpty())
    {
        if (!EntryIndexByTriangle.IsEmpty())
        {
            return FailContract(
                OutError,
                TEXT("An empty orientation field must not allocate a triangle lookup."));
        }
        if (BuildStatus == EDWCEditorSurfaceOrientationFieldBuildStatus::Unbuilt)
        {
            return (PolicySignature == 0 && FieldLayoutVersion == 0) ||
                FailContract(OutError, TEXT("An unbuilt orientation field has a build contract."));
        }
        return (PolicySignature != 0 &&
                FieldLayoutVersion == DWCEditorSurfaceOrientationVersion::FieldLayout) ||
            FailContract(OutError, TEXT("A built empty orientation field has no compatible policy contract."));
    }
    if (BuildStatus == EDWCEditorSurfaceOrientationFieldBuildStatus::Unbuilt)
    {
        return FailContract(OutError, TEXT("An unbuilt orientation field contains sparse entries."));
    }
    if (PolicySignature == 0 ||
        FieldLayoutVersion != DWCEditorSurfaceOrientationVersion::FieldLayout)
    {
        return FailContract(OutError, TEXT("The surface orientation field has no compatible policy contract."));
    }
    if (EntryIndexByTriangle.Num() != TriangleCount)
    {
        return FailContract(OutError, TEXT("The surface orientation lookup does not match the triangle count."));
    }

    int32 PreviousTriangleIndex = INDEX_NONE;
    for (int32 EntryIndex = 0; EntryIndex < Entries.Num(); ++EntryIndex)
    {
        const FDWCEditorSurfaceOrientationFieldEntry& Entry = Entries[EntryIndex];
        if (!Entry.IsValid() || Entry.TriangleIndex < 0 || Entry.TriangleIndex >= TriangleCount)
        {
            return FailContract(OutError, TEXT("The surface orientation field contains an invalid entry."));
        }
        if (EntryIndex > 0 && Entry.TriangleIndex <= PreviousTriangleIndex)
        {
            return FailContract(OutError, TEXT("Surface orientation entries must be uniquely sorted by triangle."));
        }
        if (EntryIndexByTriangle[Entry.TriangleIndex] != EntryIndex)
        {
            return FailContract(OutError, TEXT("The surface orientation lookup does not reference its entry."));
        }
        PreviousTriangleIndex = Entry.TriangleIndex;
    }

    for (int32 TriangleIndex = 0; TriangleIndex < EntryIndexByTriangle.Num(); ++TriangleIndex)
    {
        const int32 EntryIndex = EntryIndexByTriangle[TriangleIndex];
        if (EntryIndex != INDEX_NONE &&
            (!Entries.IsValidIndex(EntryIndex) || Entries[EntryIndex].TriangleIndex != TriangleIndex))
        {
            return FailContract(OutError, TEXT("The surface orientation lookup contains a dangling entry."));
        }
    }
    return true;
}
