#include "Async/DWCWetInputTasks.h"

#include "Components/DynamicWetClothesComponent.h"
#include "Utility/DWCProfiling.h"

FDWCWetSurfaceInputTask::FDWCWetSurfaceInputTask(
    TWeakObjectPtr<UDynamicWetClothesComponent> InOwner,
    FDWCWetSurfaceInputSnapshot&&               InSnapshot)
    : Owner(InOwner), Snapshot(MoveTemp(InSnapshot))
{
}

EDWCTaskKind FDWCWetSurfaceInputTask::GetKind() const
{
    return EDWCTaskKind::WetSurfaceInput;
}

FName FDWCWetSurfaceInputTask::GetDebugName() const
{
    return TEXT("DWC.WetSurfaceInput");
}

void FDWCWetSurfaceInputTask::ExecuteWorker()
{
    DWC_PROFILE_SCOPE(DWC_WetSurfaceInputTask_ExecuteWorker);

    SetStatus(EDWCTaskStatus::Running);
    Result.PendingWetnessDeltas.Reset();
    Result.ImmediateWetnessDeltas.Reset();

    if (FMath::IsNearlyZero(Snapshot.Amount) ||
        Snapshot.SkinnedPositions.Num() == 0 ||
        Snapshot.VertexTarget.VertexCount != Snapshot.SkinnedPositions.Num() ||
        Snapshot.SkinnedPositions.Num() != Snapshot.VertexWettableFlags.Num() ||
        Snapshot.SkinnedPositions.Num() != Snapshot.VertexProfileParameters.Num())
    {
        SetStatus(EDWCTaskStatus::Failed);
        return;
    }

    Result.PendingWetnessDeltas.Reserve(Snapshot.SkinnedPositions.Num() / 4);

    for (int32 VertexIndex = 0; VertexIndex < Snapshot.SkinnedPositions.Num(); ++VertexIndex)
    {
        if (!Snapshot.VertexWettableFlags.IsValidIndex(VertexIndex) ||
            !Snapshot.VertexWettableFlags[VertexIndex])
        {
            continue;
        }

        const FVector WorldPosition =
            Snapshot.ComponentTransform.TransformPosition(FVector(Snapshot.SkinnedPositions[VertexIndex]));

        float SurfaceZ = 0.0f;
        if (!QueryWaterSurfaceData(WorldPosition, SurfaceZ) || WorldPosition.Z > SurfaceZ)
        {
            continue;
        }

        const float AbsorptionMultiplier =
            Snapshot.VertexProfileParameters[VertexIndex].GetAbsorptionMultiplier();
        const float VertexAmount = Snapshot.Amount > 0.0f
                                       ? Snapshot.Amount * AbsorptionMultiplier
                                       : Snapshot.Amount;

        if (VertexAmount > 0.0f)
        {
            Result.PendingWetnessDeltas.Add({VertexIndex, VertexAmount});
        }
        else
        {
            Result.ImmediateWetnessDeltas.Add({VertexIndex, VertexAmount});
        }
    }

    SetStatus(EDWCTaskStatus::Completed);
}

void FDWCWetSurfaceInputTask::CommitGameThread()
{
    DWC_PROFILE_SCOPE(DWC_WetSurfaceInputTask_CommitGameThread);

    if (!Owner.IsValid() || GetStatus() != EDWCTaskStatus::Completed)
    {
        return;
    }

    // Intentionally empty for the initial scaffold. The next step is to expose
    // a component-side commit API that validates VertexTarget.Target and merges
    // Result into the current runtime state on the game thread.
}

bool FDWCWetSurfaceInputTask::QueryWaterSurfaceData(const FVector& WorldPosition, float& OutSurfaceZ) const
{
    OutSurfaceZ = 0.0f;

    const FDWCWaterSurfaceData& WaterSurfaceData = Snapshot.WaterSurfaceData;
    if (WaterSurfaceData.SizeX < 2 ||
        WaterSurfaceData.SizeY < 2 ||
        !WaterSurfaceData.Bounds.IsValid)
    {
        return false;
    }

    const FVector BoundsMin = WaterSurfaceData.Bounds.Min;
    const FVector BoundsMax = WaterSurfaceData.Bounds.Max;
    const float   BoundsSizeX = BoundsMax.X - BoundsMin.X;
    const float   BoundsSizeY = BoundsMax.Y - BoundsMin.Y;

    if (BoundsSizeX <= KINDA_SMALL_NUMBER || BoundsSizeY <= KINDA_SMALL_NUMBER)
    {
        return false;
    }

    const float NormalizedX = FMath::Clamp((WorldPosition.X - BoundsMin.X) / BoundsSizeX, 0.0f, 1.0f);
    const float NormalizedY = FMath::Clamp((WorldPosition.Y - BoundsMin.Y) / BoundsSizeY, 0.0f, 1.0f);

    const float GridX = NormalizedX * static_cast<float>(WaterSurfaceData.SizeX - 1);
    const float GridY = NormalizedY * static_cast<float>(WaterSurfaceData.SizeY - 1);

    const int32 X0 = FMath::Clamp(FMath::FloorToInt(GridX), 0, WaterSurfaceData.SizeX - 1);
    const int32 Y0 = FMath::Clamp(FMath::FloorToInt(GridY), 0, WaterSurfaceData.SizeY - 1);
    const int32 X1 = FMath::Clamp(X0 + 1, 0, WaterSurfaceData.SizeX - 1);
    const int32 Y1 = FMath::Clamp(Y0 + 1, 0, WaterSurfaceData.SizeY - 1);

    if (!WaterSurfaceData.IsValidSampleIndex(X0, Y0) ||
        !WaterSurfaceData.IsValidSampleIndex(X1, Y0) ||
        !WaterSurfaceData.IsValidSampleIndex(X0, Y1) ||
        !WaterSurfaceData.IsValidSampleIndex(X1, Y1))
    {
        return false;
    }

    const float AlphaX = GridX - static_cast<float>(X0);
    const float AlphaY = GridY - static_cast<float>(Y0);

    const float Z00 = WaterSurfaceData.SurfaceZ[WaterSurfaceData.GetSampleIndex(X0, Y0)];
    const float Z10 = WaterSurfaceData.SurfaceZ[WaterSurfaceData.GetSampleIndex(X1, Y0)];
    const float Z01 = WaterSurfaceData.SurfaceZ[WaterSurfaceData.GetSampleIndex(X0, Y1)];
    const float Z11 = WaterSurfaceData.SurfaceZ[WaterSurfaceData.GetSampleIndex(X1, Y1)];

    const float Z0 = FMath::Lerp(Z00, Z10, AlphaX);
    const float Z1 = FMath::Lerp(Z01, Z11, AlphaX);

    OutSurfaceZ = FMath::Lerp(Z0, Z1, AlphaY);
    return true;
}
