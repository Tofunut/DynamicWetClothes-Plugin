// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/Modes/Transparency/Viewport/DWCTransparencyPreviewUtilities.h"

#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyRevealColorTileStore.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySourcePayload.h"

DEFINE_LOG_CATEGORY(LogWetTransparencyPreviewViewport);

namespace UE::DWCEditor::TransparencyPreview
{
FDWCEditorTextureKey MakeTextureKey(
    const UWetClothingAsset* Asset,
    const EDWCEditorTexturePurpose Purpose,
    const int32 MaterialSlotIndex,
    const FGuid& LayerGuid)
{
    FDWCEditorTextureKey Key;
    Key.Owner = FObjectKey(Asset);
    Key.Purpose = Purpose;
    Key.MaterialSlotIndex = MaterialSlotIndex;
    Key.LayerGuid = LayerGuid;
    return Key;
}

FDWCEditorTextureDescriptor MakeColorDescriptor(
    const FIntPoint& Size,
    const TextureAddress Address)
{
    FDWCEditorTextureDescriptor Descriptor;
    Descriptor.Size = Size;
    Descriptor.PixelFormat = PF_B8G8R8A8;
    Descriptor.bSRGB = true;
    Descriptor.CompressionSettings = TC_Default;
    Descriptor.MipGenSettings = TMGS_NoMipmaps;
    Descriptor.Filter = TF_Bilinear;
    Descriptor.AddressX = Address;
    Descriptor.AddressY = Address;
    Descriptor.LODGroup = TEXTUREGROUP_World;
    Descriptor.InitialBGRA8 = FColor::Black;
    return Descriptor;
}

FDWCEditorTextureDescriptor MakeMaskDescriptor(
    const FIntPoint& Size,
    const TextureAddress Address)
{
    FDWCEditorTextureDescriptor Descriptor;
    Descriptor.Size = Size;
    Descriptor.PixelFormat = PF_G8;
    Descriptor.bSRGB = false;
    Descriptor.CompressionSettings = TC_Masks;
    Descriptor.MipGenSettings = TMGS_NoMipmaps;
    Descriptor.Filter = TF_Bilinear;
    Descriptor.AddressX = Address;
    Descriptor.AddressY = Address;
    Descriptor.LODGroup = TEXTUREGROUP_World;
    Descriptor.InitialG8 = 0;
    return Descriptor;
}

uint64 GetStrokeSnapshotBytes(
    const TArray<FDWCTransparencyBrushStroke>& Strokes,
    const TArray<FDWCTransparencyRevealColorStroke>& RevealColorStrokes)
{
    uint64 Bytes = Strokes.GetAllocatedSize() + RevealColorStrokes.GetAllocatedSize();
    for (const FDWCTransparencyBrushStroke& Stroke : Strokes)
    {
        Bytes += Stroke.GetSampleAllocatedSize();
        Bytes += Stroke.DisplayName.GetAllocatedSize();
    }
    for (const FDWCTransparencyRevealColorStroke& Stroke : RevealColorStrokes)
    {
        Bytes += Stroke.GetSampleAllocatedSize();
    }
    return Bytes;
}

FDWCEditorWorkerMemoryEstimate EstimateVisualizationMemory(
    const FDWCTransparencySourcePayload& SourcePayload,
    const FDWCTransparencyRevealColorTileStore& RevealColorTileStore,
    const uint64 AlphaSnapshotBytes,
    const TArray<uint8>& OuterEdgeFeatherBuffer,
    const TArray<FDWCTransparencyRevealColorStroke>& RevealColorStrokes,
    const bool bMaterializeAlpha,
    const bool bRebuildRevealColor)
{
    FDWCEditorWorkerMemoryEstimate Estimate;
    const uint64 PixelCount =
        static_cast<uint64>(FMath::Max(SourcePayload.Resolution.X, 0)) *
        static_cast<uint64>(FMath::Max(SourcePayload.Resolution.Y, 0));
    Estimate.ResidentSharedBytes = SourcePayload.GetAllocatedBytes();
    Estimate.SnapshotBytes =
        RevealColorTileStore.GetAllocatedBytes() +
        AlphaSnapshotBytes +
        OuterEdgeFeatherBuffer.GetAllocatedSize() +
        GetStrokeSnapshotBytes({}, RevealColorStrokes) +
        sizeof(FLinearColor);
    Estimate.WorkingBytes =
        (bMaterializeAlpha ? PixelCount * sizeof(uint8) * 2ull : 0ull) +
        (bRebuildRevealColor ? PixelCount * sizeof(FColor) : 0ull);
    Estimate.OutputBytes = PixelCount * sizeof(FColor);
    return Estimate;
}
}
