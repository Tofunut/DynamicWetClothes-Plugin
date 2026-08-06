#include "DWCGPUShaders.h"

namespace DWCGPUShadersPrivate
{
bool ShouldCompileDWC(const FGlobalShaderPermutationParameters& Parameters)
{
    return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}
} // namespace DWCGPUShadersPrivate

using namespace DWCGPUShadersPrivate;

IMPLEMENT_GLOBAL_SHADER(FDWCApplyTriangleAbsorptionCS, "/DWCGPU/DWCApplyAbsorption.usf", "MainCS", SF_Compute);
bool FDWCApplyTriangleAbsorptionCS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
    return ShouldCompileDWC(Parameters);
}

IMPLEMENT_GLOBAL_SHADER(FDWCApplyBinnedAbsorptionCS, "/DWCGPU/DWCApplyAbsorptionBinned.usf", "MainCS", SF_Compute);
bool FDWCApplyBinnedAbsorptionCS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
    return ShouldCompileDWC(Parameters);
}

IMPLEMENT_GLOBAL_SHADER(FDWCUpdateTriangleFlowCS, "/DWCGPU/DWCUpdateTriangleFlow.usf", "MainCS", SF_Compute);
bool FDWCUpdateTriangleFlowCS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
    return ShouldCompileDWC(Parameters);
}

IMPLEMENT_GLOBAL_SHADER(FDWCUpdateRestTriangleFlowCS, "/DWCGPU/DWCUpdateRestTriangleFlow.usf", "MainCS", SF_Compute);
bool FDWCUpdateRestTriangleFlowCS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
    return ShouldCompileDWC(Parameters);
}

IMPLEMENT_GLOBAL_SHADER(FDWCApplyNiagaraWetCollisionCS, "/DWCGPU/DWCApplyNiagaraWetCollision.usf", "MainCS", SF_Compute);
bool FDWCApplyNiagaraWetCollisionCS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
    return ShouldCompileDWC(Parameters);
}

IMPLEMENT_GLOBAL_SHADER(FDWCResolveNiagaraDropletContactsCS, "/DWCGPU/DWCResolveNiagaraDropletContacts.usf", "MainCS", SF_Compute);
bool FDWCResolveNiagaraDropletContactsCS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
    return ShouldCompileDWC(Parameters);
}

IMPLEMENT_GLOBAL_SHADER(FDWCStampNiagaraDropletsCS, "/DWCGPU/DWCStampNiagaraDroplets.usf", "MainCS", SF_Compute);
bool FDWCStampNiagaraDropletsCS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
    return ShouldCompileDWC(Parameters);
}

IMPLEMENT_GLOBAL_SHADER(FDWCDiffuseDryCS, "/DWCGPU/DWCDiffuseDry.usf", "MainCS", SF_Compute);
bool FDWCDiffuseDryCS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
    return ShouldCompileDWC(Parameters);
}

IMPLEMENT_GLOBAL_SHADER(FDWCDiffuseDry8CS, "/DWCGPU/DWCDiffuseDry8.usf", "MainCS", SF_Compute);
bool FDWCDiffuseDry8CS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
    return ShouldCompileDWC(Parameters);
}

IMPLEMENT_GLOBAL_SHADER(FDWCSeamGatherCS, "/DWCGPU/DWCSeamGather.usf", "MainCS", SF_Compute);
bool FDWCSeamGatherCS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
    return ShouldCompileDWC(Parameters);
}

IMPLEMENT_GLOBAL_SHADER(FDWCSurfaceDropletStampCS, "/DWCGPU/DWCSurfaceDropletStamp.usf", "MainCS", SF_Compute);
bool FDWCSurfaceDropletStampCS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
    return ShouldCompileDWC(Parameters);
}

IMPLEMENT_GLOBAL_SHADER(FDWCSurfaceWetnessDryInPlaceCS, "/DWCGPU/DWCSurfaceWetnessDryInPlace.usf", "MainCS", SF_Compute);
bool FDWCSurfaceWetnessDryInPlaceCS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
    return ShouldCompileDWC(Parameters);
}

