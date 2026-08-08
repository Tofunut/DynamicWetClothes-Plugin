// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UMaterial;
class UMaterialExpression;
class UMaterialExpressionMaterialFunctionCall;
class UMaterialExpressionTextureCoordinate;
class UMaterialFunctionInterface;

struct FDWCMaterialGraphPin
{
    UMaterialExpression* Expression = nullptr;
    FString              OutputName;

    bool IsValid() const { return Expression != nullptr; }
};

struct FDWCBaseSurfaceGraphInputs
{
    FDWCMaterialGraphPin BaseColor;
    FDWCMaterialGraphPin Roughness;
    FDWCMaterialGraphPin Specular;
    FDWCMaterialGraphPin Metallic;
    FDWCMaterialGraphPin Normal;

    bool IsValid() const
    {
        return BaseColor.IsValid() && Roughness.IsValid() && Specular.IsValid() &&
               Metallic.IsValid() && Normal.IsValid();
    }
};

struct FDWCSurfaceGraphOutputs
{
    FDWCMaterialGraphPin BaseColor;
    FDWCMaterialGraphPin Roughness;
    FDWCMaterialGraphPin Specular;
    FDWCMaterialGraphPin Normal;
    FDWCMaterialGraphPin SurfaceCoverage;
    FDWCMaterialGraphPin DropletCoverage;
    FDWCMaterialGraphPin DropletWetness;
    FDWCMaterialGraphPin DropletBrush;

    bool IsValid() const
    {
        return BaseColor.IsValid() && Roughness.IsValid() && Specular.IsValid() && Normal.IsValid() &&
               SurfaceCoverage.IsValid() && DropletCoverage.IsValid() &&
               DropletWetness.IsValid() && DropletBrush.IsValid();
    }
};

struct FDWCSurfaceGraphBuildRequest
{
    UMaterial* Material = nullptr;
    int32      DWCDataUVChannelIndex = INDEX_NONE;
    int32      SurfaceWaterNormalUVChannelIndex = 0;

    // The caller owns backend selection. Runtime passes the CPU/GPU switch;
    // editor preview will pass its scalar preview wetness parameter.
    FDWCMaterialGraphPin WetnessInput;

    // Optional validated dependency supplied by a caller that validates the full
    // material-function set once. Build validates dependencies when this is null.
    UMaterialFunctionInterface* EvaluateFunction = nullptr;
};

struct FDWCSurfaceGraphBuildResult
{
    bool            bSucceeded = false;
    TArray<FString> FailureReasons;

    FDWCBaseSurfaceGraphInputs               BaseInputs;
    FDWCSurfaceGraphOutputs                  Outputs;
    UMaterialExpressionMaterialFunctionCall* EvaluateExpression = nullptr;
    UMaterialExpressionTextureCoordinate*    DWCDataUVExpression = nullptr;
    UMaterialExpressionTextureCoordinate*    SurfaceWaterNormalUVExpression = nullptr;
};

/** Builds the backend-independent DWC surface evaluation graph in an editor material. */
class FDWCSurfaceGraphBuilder
{
  public:
    static bool ValidateDependencies(
        TArray<FString>&             OutFailureReasons,
        UMaterialFunctionInterface** OutEvaluateFunction = nullptr);

    static FDWCSurfaceGraphBuildResult Build(const FDWCSurfaceGraphBuildRequest& Request);
};
