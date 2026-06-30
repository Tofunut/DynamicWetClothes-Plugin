#pragma once

#include "CoreMinimal.h"
#include "DynamicWet/DynamicWetReceiverSettings.h"

struct FDynamicWetReceiverContext;
struct FDWCWetContact;
struct FDWCWetSurfaceData;

class FDynamicWetReceiverInputApplicator
{
public:
    static float CalculateContactExposure(
        const FVector& WorldNormal,
        const FVector& Direction,
        const FVector& Normal,
        const FDynamicWetReceiverSettings& Settings);

    void ApplyWetnessGlobal(FDynamicWetReceiverContext& Receiver, float Amount);
    void ApplyWetnessBelowHeight(FDynamicWetReceiverContext& Receiver, float WaterSurfaceZ, float Amount);
    bool ApplyWetSurface(
        FDynamicWetReceiverContext& Receiver,
        const FDWCWetSurfaceData& SurfaceData,
        float Amount,
        bool bApplyMaterial);
    bool ApplyRainWetness(
        FDynamicWetReceiverContext& Receiver,
        const FVector& RainDirection,
        float Amount,
        bool bApplyMaterial);
    bool ApplyWetContact(FDynamicWetReceiverContext& Receiver, const FDWCWetContact& Contact, bool bApplyMaterial);
    bool ApplyWetContacts(
        FDynamicWetReceiverContext& Receiver,
        const TArray<FDWCWetContact>& Contacts,
        bool bApplyMaterial);
    bool GetWetnessWorldBounds(const FDynamicWetReceiverContext& Receiver, FBox& OutBounds);
    static bool QueryWetSurfaceData(
        const FDWCWetSurfaceData& SurfaceData,
        const FVector& WorldPosition,
        float& OutSurfaceZ);
};
