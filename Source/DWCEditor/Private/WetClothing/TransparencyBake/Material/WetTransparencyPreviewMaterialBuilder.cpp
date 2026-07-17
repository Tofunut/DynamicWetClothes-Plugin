#include "WetClothing/TransparencyBake/Material/WetTransparencyPreviewMaterialBuilder.h"

#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/Package.h"

FWetTransparencyPreviewMaterialBuildResult FWetTransparencyPreviewMaterialBuilder::Build(
    const FWetTransparencyPreviewMaterialBuildArgs& Args)
{
    FWetTransparencyPreviewMaterialBuildResult Result;
    if (Args.SourceMaterial == nullptr)
    {
        Result.ErrorMessage = TEXT("No source material was provided.");
        return Result;
    }

    Result.PreviewMID = UMaterialInstanceDynamic::Create(Args.SourceMaterial, GetTransientPackage());
    if (Result.PreviewMID == nullptr)
    {
        Result.ErrorMessage = TEXT("Failed to create a transient transparency preview material instance.");
        return Result;
    }

    Result.TransientBaseMaterial = Args.SourceMaterial->GetMaterial();
    Result.TransientMaterialParent = Args.SourceMaterial;
    Result.bSucceeded = true;
    return Result;
}
