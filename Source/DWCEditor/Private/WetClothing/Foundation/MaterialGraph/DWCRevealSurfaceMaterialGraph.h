// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "WetClothing/Foundation/MaterialGraph/DWCSurfaceGraphBuilder.h"

class UMaterial;
class UMaterialExpressionScalarParameter;
class UMaterialExpressionTextureObjectParameter;

/** Inputs for the editor-only packed Reveal Surface authoring preview. */
struct FDWCRevealSurfaceMaterialGraphRequest
{
    UMaterial* Material = nullptr;
    FDWCMaterialGraphPin BaseColor;
    FDWCMaterialGraphPin BaseNormal;
    FDWCMaterialGraphPin DataUV;
    FDWCMaterialGraphPin Visibility;
    FDWCMaterialGraphPin VisualizationMode;
    FName SurfaceTextureParameterName;
    FName UseSurfaceParameterName;
    FName StrengthParameterName;
    FName ShowParameterName;
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
    UMaterialExpressionScalarParameter* StrengthParameter = nullptr;
    UMaterialExpressionScalarParameter* ShowParameter = nullptr;
};

/** Builds the editor-only packed Reveal Surface authoring composite. */
class FDWCRevealSurfaceMaterialGraph
{
  public:
    /** Editor-only packed input: RG=normal, B=metallic, A=source coverage. */
    static FDWCRevealSurfaceMaterialGraphResult BuildAuthoringPreview(
        const FDWCRevealSurfaceMaterialGraphRequest& Request);
};
