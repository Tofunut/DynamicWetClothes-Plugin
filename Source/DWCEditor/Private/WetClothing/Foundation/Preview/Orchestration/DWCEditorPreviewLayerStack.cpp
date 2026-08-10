// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/Foundation/Preview/Orchestration/DWCEditorPreviewLayerStack.h"

#include "Engine/Texture.h"

namespace
{
    template <typename BindingType>
    void AddOrReplaceBinding(TArray<BindingType>& Bindings, const BindingType& Binding)
    {
        if (Binding.ParameterName.IsNone())
        {
            return;
        }

        if (BindingType* Existing = Bindings.FindByPredicate(
                [&Binding](const BindingType& Candidate)
                {
                    return Candidate.ParameterName == Binding.ParameterName;
                }))
        {
            *Existing = Binding;
            return;
        }
        Bindings.Add(Binding);
    }

    template <typename BindingType, int32 InlineSize, typename EqualityPredicate>
    bool AreBindingArraysEqual(
        const TArray<BindingType, TInlineAllocator<InlineSize>>& A,
        const TArray<BindingType, TInlineAllocator<InlineSize>>& B,
        EqualityPredicate&& Equal)
    {
        if (A.Num() != B.Num())
        {
            return false;
        }

        for (int32 Index = 0; Index < A.Num(); ++Index)
        {
            if (!Equal(A[Index], B[Index]))
            {
                return false;
            }
        }

        return true;
    }
} // namespace

void FDWCEditorPreviewLayer::AddScalar(
    const FName ParameterName,
    const float Value,
    const float ResetValue)
{
    Scalars.Add({ ParameterName, Value, ResetValue });
}

void FDWCEditorPreviewLayer::AddVector(
    const FName         ParameterName,
    const FLinearColor& Value,
    const FLinearColor& ResetValue)
{
    Vectors.Add({ ParameterName, Value, ResetValue });
}

void FDWCEditorPreviewLayer::AddTexture(const FName ParameterName, UTexture* Value)
{
    Textures.Add({ ParameterName, Value });
}

bool FDWCEditorPreviewLayer::IsEquivalentTo(const FDWCEditorPreviewLayer& Other) const
{
    if (Kind != Other.Kind ||
        MaterialSlotIndex != Other.MaterialSlotIndex ||
        AuthoringRevision != Other.AuthoringRevision ||
        ResourceRevision != Other.ResourceRevision ||
        bEnabled != Other.bEnabled)
    {
        return false;
    }

    return AreBindingArraysEqual(
               Scalars,
               Other.Scalars,
               [](const FDWCEditorPreviewScalarBinding& A, const FDWCEditorPreviewScalarBinding& B)
               {
                   return A.ParameterName == B.ParameterName &&
                          A.Value == B.Value &&
                          A.ResetValue == B.ResetValue;
               }) &&
           AreBindingArraysEqual(
               Vectors,
               Other.Vectors,
               [](const FDWCEditorPreviewVectorBinding& A, const FDWCEditorPreviewVectorBinding& B)
               {
                   return A.ParameterName == B.ParameterName &&
                          A.Value == B.Value &&
                          A.ResetValue == B.ResetValue;
               }) &&
           AreBindingArraysEqual(
               Textures,
               Other.Textures,
               [](const FDWCEditorPreviewTextureBinding& A, const FDWCEditorPreviewTextureBinding& B)
               {
                   return A.ParameterName == B.ParameterName &&
                          A.Value.Get() == B.Value.Get();
               });
}

bool FDWCEditorPreviewParameterSet::IsEmpty() const
{
    return Scalars.IsEmpty() && Vectors.IsEmpty() && Textures.IsEmpty();
}

uint64 FDWCEditorPreviewParameterSet::GetAllocatedSize() const
{
    return static_cast<uint64>(Scalars.GetAllocatedSize()) +
           static_cast<uint64>(Vectors.GetAllocatedSize()) +
           static_cast<uint64>(Textures.GetAllocatedSize());
}

void FDWCEditorPreviewLayerStack::AddOrReplace(FDWCEditorPreviewLayer Layer)
{
    if (FDWCEditorPreviewLayer* Existing = Layers.FindByPredicate(
            [&Layer](const FDWCEditorPreviewLayer& Candidate)
            {
                return Candidate.Kind == Layer.Kind;
            }))
    {
        *Existing = MoveTemp(Layer);
    }
    else
    {
        Layers.Add(MoveTemp(Layer));
    }

    Layers.Sort(
        [](const FDWCEditorPreviewLayer& A, const FDWCEditorPreviewLayer& B)
        {
            return GetLayerOrder(A.Kind) < GetLayerOrder(B.Kind);
        });
}

void FDWCEditorPreviewLayerStack::Remove(const EDWCEditorPreviewLayerKind Kind)
{
    Layers.RemoveAll(
        [Kind](const FDWCEditorPreviewLayer& Layer)
        {
            return Layer.Kind == Kind;
        });
}

void FDWCEditorPreviewLayerStack::Reset()
{
    Layers.Reset();
    MaterialSlotIndex = INDEX_NONE;
}

void FDWCEditorPreviewLayerStack::BuildParameterSet(
    FDWCEditorPreviewParameterSet& OutParameters) const
{
    OutParameters.Scalars.Reset();
    OutParameters.Vectors.Reset();
    OutParameters.Textures.Reset();

    for (const FDWCEditorPreviewLayer& Layer : Layers)
    {
        if (!Layer.bEnabled)
        {
            continue;
        }
        for (const FDWCEditorPreviewScalarBinding& Binding : Layer.Scalars)
        {
            AddOrReplaceBinding(OutParameters.Scalars, Binding);
        }
        for (const FDWCEditorPreviewVectorBinding& Binding : Layer.Vectors)
        {
            AddOrReplaceBinding(OutParameters.Vectors, Binding);
        }
        for (const FDWCEditorPreviewTextureBinding& Binding : Layer.Textures)
        {
            AddOrReplaceBinding(OutParameters.Textures, Binding);
        }
    }
}

int32 FDWCEditorPreviewLayerStack::GetLayerOrder(const EDWCEditorPreviewLayerKind Kind)
{
    switch (Kind)
    {
    case EDWCEditorPreviewLayerKind::SavedWrinkle:
        return 100;
    case EDWCEditorPreviewLayerKind::LiveWrinkleAccumulated:
        return 110;
    case EDWCEditorPreviewLayerKind::LiveWrinkleTransient:
        return 120;
    case EDWCEditorPreviewLayerKind::LiveWrinkleHover:
        return 130;
    case EDWCEditorPreviewLayerKind::SavedTransparency:
        return 200;
    case EDWCEditorPreviewLayerKind::LiveTransparency:
        return 210;
    case EDWCEditorPreviewLayerKind::LiveTransparencyHover:
        return 220;
    case EDWCEditorPreviewLayerKind::Visualization:
        return 300;
    default:
        return MAX_int32;
    }
}
