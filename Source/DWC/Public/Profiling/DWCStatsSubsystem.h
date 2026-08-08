// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "UObject/WeakObjectPtr.h"
#include "CoreMinimal.h"
#include "Profiling/DWCStats.h"
#include "Subsystems/WorldSubsystem.h"
#include "DWCStatsSubsystem.generated.h"

class UDynamicWetClothesComponent;

UCLASS()
class DWC_API UDWCStatsSubsystem final : public UTickableWorldSubsystem
{
    GENERATED_BODY()

  public:
    virtual void    Deinitialize() override;
    virtual void    Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual bool    DoesSupportWorldType(EWorldType::Type WorldType) const override;

    void RegisterComponent(UDynamicWetClothesComponent* Component);
    void UnregisterComponent(UDynamicWetClothesComponent* Component);

    const FDWCStatsSnapshot& GetLatestSnapshot() const { return LatestSnapshot; }
    void                     RefreshStats();

  private:
    void CollectStats(FDWCStatsSnapshot& OutSnapshot);
    void CollectBacklogStats(FDWCWorkloadStatsSnapshot& OutSnapshot) const;
    void PublishStats(const FDWCStatsSnapshot& Snapshot) const;
    void RefreshWorkloadRates(float SampleSeconds);
    void PublishWorkloadStats(const FDWCWorkloadStatsSnapshot& Snapshot) const;
    void SyncMemoryStatGroups();

    TSet<TWeakObjectPtr<UDynamicWetClothesComponent>> RegisteredComponents;
    FDWCStatsSnapshot                                 LatestSnapshot;
    FDWCWorkloadStatsSnapshot                         LatestWorkloadSnapshot;
    FDWCWorkloadEventTotals                           LastWorkloadEventTotals;
    float                                             TimeUntilNextRefresh = 0.0f;
    float                                             WorkloadSampleSeconds = 0.0f;
    bool                                              bMemoryStatGroupStateInitialized = false;
    bool                                              bLastDWCStatEnabled = false;
    bool                                              bWorkloadRateStateInitialized = false;
};
