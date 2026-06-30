#include "DynamicWet/DynamicWetReceiverContext.h"

#include "DynamicWet/DynamicWetReceiverRuntimeData.h"

float FDynamicWetReceiverContext::GetAbsorptionMultiplierForVertex(const int32 VertexIndex) const
{
    return RuntimeData.VertexWetnessProfileParameters.IsValidIndex(VertexIndex)
               ? RuntimeData.VertexWetnessProfileParameters[VertexIndex].GetAbsorptionMultiplier()
               : GetAbsorptionMultiplier();
}

float FDynamicWetReceiverContext::GetDryRatePerSecondForVertex(const int32 VertexIndex) const
{
    return RuntimeData.VertexWetnessProfileParameters.IsValidIndex(VertexIndex)
               ? RuntimeData.VertexWetnessProfileParameters[VertexIndex].GetDryRatePerSecond()
               : GetDryRatePerSecond();
}

float FDynamicWetReceiverContext::GetSpreadRatePerSecondForVertex(const int32 VertexIndex) const
{
    return RuntimeData.VertexWetnessProfileParameters.IsValidIndex(VertexIndex)
               ? RuntimeData.VertexWetnessProfileParameters[VertexIndex].GetSpreadRatePerSecond()
               : GetSpreadRatePerSecond();
}

float FDynamicWetReceiverContext::GetGravityFlowStrengthForVertex(const int32 VertexIndex) const
{
    return RuntimeData.VertexWetnessProfileParameters.IsValidIndex(VertexIndex)
               ? RuntimeData.VertexWetnessProfileParameters[VertexIndex].GetGravityFlowStrength()
               : GetGravityFlowStrength();
}
