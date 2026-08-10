// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "WetClothing/Foundation/MaterialGraph/DWCSurfaceGraphBuilder.h"

class UMaterial;
class UMaterialExpressionScalarParameter;
class UMaterialExpressionTextureObjectParameter;

/** Inputs used to add the shared Reveal Surface color/normal composite to an editor material graph. */
struct FDWCRevealSurfaceMaterialGraphRequest
{
    UMaterial* Material = nullptr;
    FDWCMaterialGraphPin BaseColor;
    FDWCMaterialGraphPin BaseNormal;
    FDWCMaterialGraphPin DataUV;
    FDWCMaterialGraphPin Visibility;
    FName SurfaceTextureParameterName;
    FName UseSurfaceParameterName;
    FName MetallicDarkeningParameterName;
    int32 NodePosX = 0;
    int32 NodePosY = 0;
    FString Description;
};

struct FDWCRevealSurfaceMaterialGraphResult
{
    bool bSucceeded = false;
    FString FailureReason;
    FDWCMaterialGraphPin BaseColor;
    FDWCMaterialGraphPin Normal;
    UMaterialExpressionTextureObjectParameter* SurfaceTextureParameter = nullptr;
    UMaterialExpressionScalarParameter* UseSurfaceParameter = nullptr;
    UMaterialExpressionScalarParameter* MetallicDarkeningParameter = nullptr;
};

/**
 * Builds the common Reveal Surface composite.
 *
 * The source texture is intentionally sampled as color data: RG are a
 * reoriented tangent-space normal, B is inner metallic, and A is coverage.
 * Visibility is supplied by the caller so runtime and editor preview can use
 * their own transparency working map while sharing the exact surface math.
 */
class FDWCRevealSurfaceMaterialGraph
{
  public:
    static FDWCRevealSurfaceMaterialGraphResult Build(const FDWCRevealSurfaceMaterialGraphRequest& Request);
};
