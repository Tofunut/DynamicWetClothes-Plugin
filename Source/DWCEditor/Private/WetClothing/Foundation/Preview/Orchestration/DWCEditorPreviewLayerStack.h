#pragma once

#include "CoreMinimal.h"

class UTexture;

enum class EDWCEditorPreviewLayerKind : uint8
{
    SavedWrinkle,
    LiveWrinkleAccumulated,
    LiveWrinkleTransient,
    LiveWrinkleHover,
    SavedTransparency,
    LiveTransparency,
    Visualization
};

struct FDWCEditorPreviewScalarBinding
{
    FName ParameterName;
    float Value = 0.0f;
    float ResetValue = 0.0f;
};

struct FDWCEditorPreviewVectorBinding
{
    FName ParameterName;
    FLinearColor Value = FLinearColor::Black;
    FLinearColor ResetValue = FLinearColor::Black;
};

struct FDWCEditorPreviewTextureBinding
{
    FName ParameterName;
    TWeakObjectPtr<UTexture> Value;
};

/** One semantic contribution to an editor preview. */
struct FDWCEditorPreviewLayer
{
    EDWCEditorPreviewLayerKind Kind = EDWCEditorPreviewLayerKind::Visualization;
    int32 MaterialSlotIndex = INDEX_NONE;
    uint64 AuthoringRevision = 0;
    uint64 ResourceRevision = 0;
    bool bEnabled = true;

    TArray<FDWCEditorPreviewScalarBinding, TInlineAllocator<8>> Scalars;
    TArray<FDWCEditorPreviewVectorBinding, TInlineAllocator<4>> Vectors;
    TArray<FDWCEditorPreviewTextureBinding, TInlineAllocator<4>> Textures;

    void AddScalar(FName ParameterName, float Value, float ResetValue = 0.0f);
    void AddVector(
        FName ParameterName,
        const FLinearColor& Value,
        const FLinearColor& ResetValue = FLinearColor::Black);
    void AddTexture(FName ParameterName, UTexture* Value);
};

/** Flattened parameter set consumed by a preview MID. */
struct FDWCEditorPreviewParameterSet
{
    TArray<FDWCEditorPreviewScalarBinding> Scalars;
    TArray<FDWCEditorPreviewVectorBinding> Vectors;
    TArray<FDWCEditorPreviewTextureBinding> Textures;

    bool IsEmpty() const;
    uint64 GetAllocatedSize() const;
};

/** Deterministic semantic layer order. Later layers override duplicate parameter bindings. */
struct FDWCEditorPreviewLayerStack
{
    int32 MaterialSlotIndex = INDEX_NONE;
    TArray<FDWCEditorPreviewLayer, TInlineAllocator<8>> Layers;

    void AddOrReplace(FDWCEditorPreviewLayer Layer);
    void Remove(EDWCEditorPreviewLayerKind Kind);
    void Reset();
    void BuildParameterSet(FDWCEditorPreviewParameterSet& OutParameters) const;

    static int32 GetLayerOrder(EDWCEditorPreviewLayerKind Kind);
};
