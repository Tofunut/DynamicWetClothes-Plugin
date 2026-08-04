#include "DWCGPUBackend.h"

#include "Utility/DWCDataUVBufferView.h"
#include "TextureResource.h"
#include "WetRendering/WetMaterialParameters.h"
#include "WetRendering/DWCGPUResourceSubsystem.h"

#include "DWCGPUShaders.h"
#include "Niagara/DWCGPUNiagaraWetCollisionBridge.h"
#include "CachedGeometry.h"
#include "Components/DynamicWetClothesComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/WetClothingSettings.h"
#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetnessProfile.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Profiling/DWCStats.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/RealtimeGPUProfiler.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "RenderingThread.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SkeletalRenderPublic.h"

DEFINE_LOG_CATEGORY_STATIC(LogDWCGPU, Log, All);

DECLARE_GPU_STAT_NAMED(DWC_UpdateTriangleFlow, TEXT("DWC UpdateTriangleFlow"));
DECLARE_GPU_STAT_NAMED(DWC_SurfaceStamp, TEXT("DWC SurfaceStamp"));
DECLARE_GPU_STAT_NAMED(DWC_SurfaceDry, TEXT("DWC SurfaceDry"));
DECLARE_GPU_STAT_NAMED(DWC_NiagaraDropletResolve, TEXT("DWC NiagaraDropletResolve"));
DECLARE_GPU_STAT_NAMED(DWC_NiagaraDropletStamp, TEXT("DWC NiagaraDropletStamp"));
DECLARE_GPU_STAT_NAMED(DWC_ApplyAbsorption, TEXT("DWC ApplyAbsorption"));
DECLARE_GPU_STAT_NAMED(DWC_DiffuseDry, TEXT("DWC DiffuseDry"));
DECLARE_GPU_STAT_NAMED(DWC_SeamGather, TEXT("DWC SeamGather"));

namespace DWCGPUBackendPrivate
{
constexpr int32 DWCFullSimulationMapVersion = FDWCGPULODBakeData::CurrentMapBakeVersion;
constexpr float DWCSeamTransferScale = 1.0f;
constexpr int32 DWCAbsorptionBinTileSize = 16;
constexpr int32 DWCAbsorptionBinMinDispatches = 32;
constexpr int32 DWCAbsorptionContactFloat4Count = 6;
constexpr int32 DWCMaxComputeGroupsPerDimension = 65535;

static TAutoConsoleVariable<int32> CVarDWCGPUBinnedAbsorption(
    TEXT("r.DWC.GPU.BinnedAbsorption"),
    1,
    TEXT("Use tile-binned GPU absorption for large positive wetness input batches. 0 disables the experimental path."),
    ECVF_Default);

static TAutoConsoleVariable<float> CVarDWCGPUWetAreaStampRadiusPixels(
    TEXT("r.DWC.GPU.WetAreaStampRadiusPixels"),
    3.0f,
    TEXT("Circular Data-UV stamp radius, in wetness-map texels, for GPU WetArea samples."),
    ECVF_Default);

uint32 FloatToBits(const float Value)
{
    uint32 Bits = 0;
    FMemory::Memcpy(&Bits, &Value, sizeof(uint32));
    return Bits;
}

FResolvedAbsorbedWaterSimulationParameters MakeRuntimeAbsorbedWaterSimulationProfile(
    const FWetnessProfileParameters& Parameters)
{
    return Parameters.ResolveAbsorbedWaterSimulation();
}

FWetnessProfileParameters ResolveRuntimeWetnessProfileParameters(
    const UWetClothingAsset& Asset,
    const int32 ProfileIndex)
{
    const FWetClothingEditableWetPartData& WetPartData = Asset.Authored.PartData.EditableWetPartData;
    const FWetPartProfileAssignment* AuthoredProfile = WetPartData.FindProfile(ProfileIndex);
    if (AuthoredProfile == nullptr)
    {
        return FWetnessProfileParameters();
    }

#if WITH_EDITOR
    if (AuthoredProfile->SourceProfile.IsValid())
    {
        UObject* SourceObject = AuthoredProfile->SourceProfile.ResolveObject();
        if (SourceObject == nullptr)
        {
            SourceObject = AuthoredProfile->SourceProfile.TryLoad();
        }

        if (const UWetnessProfile* SourceProfile =
                Cast<UWetnessProfile>(SourceObject))
        {
            return SourceProfile->GetParameters();
        }
        else
        {
            UE_LOG(
                LogDWCGPU,
                Warning,
                TEXT("DWCGPU: Failed to resolve Wetness Profile '%s' for WCA '%s'. Using the WCA snapshot/fallback profile."),
                *AuthoredProfile->SourceProfile.ToString(),
                *GetNameSafe(&Asset));
        }
    }
#endif

    return Asset.Derived.Inline.ResolvedWetnessProfileParameters.IsValidIndex(ProfileIndex)
        ? Asset.Derived.Inline.ResolvedWetnessProfileParameters[ProfileIndex]
        : AuthoredProfile->Parameters;
}

int32 FindOrAddGPUProfile(
    TArray<FVector4f>& Profiles,
    const FResolvedAbsorbedWaterSimulationParameters& Candidate,
    const float DropletDryRatePerSecond,
    const float SpreadRateScale,
    const float DryRateScale,
    const float GravityFlowStrengthScale,
    float& OutMaxSpreadRate,
    float& OutMaxDryRate,
    float& OutMaxGravityFlowStrength)
{
    const float SpreadRate = FMath::Max(0.0f, Candidate.SpreadRatePerSecond * SpreadRateScale);
    const float DryRate = FMath::Max(0.0f, Candidate.DryRatePerSecond * DryRateScale);
    const float GravityFlowStrength = FMath::Max(0.0f, Candidate.GravityFlowStrength * GravityFlowStrengthScale);
    const float DropletDryRate = FMath::Max(0.0f, DropletDryRatePerSecond * DryRateScale);
    const FVector4f PackedProfile(
        SpreadRate,
        DryRate,
        GravityFlowStrength,
        DropletDryRate);
    for (int32 ProfileIndex = 0; ProfileIndex < Profiles.Num(); ++ProfileIndex)
    {
        const FVector4f& Existing = Profiles[ProfileIndex];
        if (FMath::IsNearlyEqual(Existing.X, PackedProfile.X, KINDA_SMALL_NUMBER) &&
            FMath::IsNearlyEqual(Existing.Y, PackedProfile.Y, KINDA_SMALL_NUMBER) &&
            FMath::IsNearlyEqual(Existing.Z, PackedProfile.Z, KINDA_SMALL_NUMBER) &&
            FMath::IsNearlyEqual(Existing.W, PackedProfile.W, KINDA_SMALL_NUMBER))
        {
            return ProfileIndex;
        }
    }

    OutMaxSpreadRate = FMath::Max(OutMaxSpreadRate, SpreadRate);
    OutMaxDryRate = FMath::Max(OutMaxDryRate, FMath::Max(DryRate, DropletDryRate));
    OutMaxGravityFlowStrength = FMath::Max(OutMaxGravityFlowStrength, GravityFlowStrength);
    return Profiles.Add(PackedProfile);
}

const FWetClothingWetPartEntry* ResolveWetPartForBakedTriangle(
    const UWetClothingAsset& Asset,
    const FDWCGPUBakedTriangle& Triangle)
{
    const FWetClothingEditableWetPartData& WetPartData = Asset.Authored.PartData.EditableWetPartData;
    const FWetClothingAuthoredMaterialSlot* Slot = WetPartData.FindMaterialSlot(Triangle.MaterialSlotIndex);
    if (Slot == nullptr)
    {
        return nullptr;
    }

    for (const FWetClothingWetPartEntry& Entry : Slot->WetPartEntries)
    {
        if (Entry.WetPartID != 0 && Entry.AssignedUVIslandIDs.Contains(Triangle.UVIslandID))
        {
            return &Entry;
        }
    }
    return nullptr;
}

int32 ResolveAuthoredProfileIndexForBakedTriangle(
    const UWetClothingAsset& Asset,
    const FDWCGPUBakedTriangle& Triangle)
{
    const FWetClothingEditableWetPartData& WetPartData = Asset.Authored.PartData.EditableWetPartData;
    if (const FWetClothingWetPartEntry* Part = ResolveWetPartForBakedTriangle(Asset, Triangle))
    {
        return WetPartData.Profiles.IsValidIndex(Part->ProfileIndex)
            ? Part->ProfileIndex
            : 0;
    }
    return INDEX_NONE;
}

struct alignas(16) FUint4GPU
{
    uint32 X = 0;
    uint32 Y = 0;
    uint32 Z = 0;
    uint32 W = 0;

    FUint4GPU() = default;
    FUint4GPU(uint32 InX, uint32 InY, uint32 InZ, uint32 InW)
        : X(InX), Y(InY), Z(InZ), W(InW)
    {
    }
};

struct FUint2GPU
{
    uint32 X = 0;
    uint32 Y = 0;

    FUint2GPU() = default;
    FUint2GPU(uint32 InX, uint32 InY)
        : X(InX), Y(InY)
    {
    }
};

static_assert(sizeof(FUint4GPU) == 16, "FUint4GPU must match HLSL uint4.");
static_assert(sizeof(FUint2GPU) == 8, "FUint2GPU must match HLSL uint2.");
static_assert(sizeof(FVector4f) == 16, "FVector4f must match HLSL float4.");

struct FTriangleAbsorptionDispatch
{
    FIntPoint DispatchMin = FIntPoint::ZeroValue;
    FIntPoint DispatchSize = FIntPoint::ZeroValue;
    FVector4f UV01 = FVector4f(0, 0, 0, 0);
    FVector4f UV2AndSettings = FVector4f(0, 0, 0, 0);
    FVector4f ContactAndRadius = FVector4f(0, 0, 0, 1);
    FVector4f P0AndAmount = FVector4f(0, 0, 0, 0);
    FVector4f P1AndMaxWetness = FVector4f(0, 0, 0, 1);
    FVector4f P2AndMode = FVector4f(0, 0, 0, 0);
};

struct FSurfaceStampDispatch
{
    FIntPoint DispatchMin = FIntPoint::ZeroValue;
    FIntPoint DispatchSize = FIntPoint::ZeroValue;
    FVector2f UV = FVector2f::ZeroVector;
    FVector2f CenterPixels = FVector2f::ZeroVector;
    FVector2f HalfSizePixels = FVector2f::ZeroVector;
    float Amount = 0.0f;
    bool bDroplet2 = false;
};

struct FSlotRenderDispatch
{
    int32 SlotRuntimeIndex = INDEX_NONE;
    int32 StaticSlotIndex = INDEX_NONE;
    int32 Resolution = 0;
    int32 SurfaceWaterResolution = 0;
    FTextureRenderTargetResource* CurrentResource = nullptr;
    FTextureRenderTargetResource* NextResource = nullptr;
    FTextureRenderTargetResource* CurrentPendingResource = nullptr;
    FTextureRenderTargetResource* NextPendingResource = nullptr;
    FTextureRenderTargetResource* SurfaceDroplet1Resource = nullptr;
    FTextureRenderTargetResource* SurfaceDroplet2Resource = nullptr;
    TArray<FTriangleAbsorptionDispatch> AbsorptionDispatches;
    TArray<FVector4f> BinnedAbsorptionContacts;
    TArray<FUint2GPU> BinnedAbsorptionTileBins;
    TArray<uint32> BinnedAbsorptionTileContactIndices;
    FIntPoint BinnedAbsorptionTileGridSize = FIntPoint::ZeroValue;
    TArray<FSurfaceStampDispatch> SurfaceStampDispatches;
};

bool BuildSurfaceStampDispatch(
    const FDWCSurfaceStampRequest& Request,
    const int32 Resolution,
    FSurfaceStampDispatch& OutDispatch)
{
    if (Resolution <= 0 ||
        Request.UV.ContainsNaN() ||
        !FMath::IsFinite(Request.UV.X) ||
        !FMath::IsFinite(Request.UV.Y) ||
        Request.Amount <= 0.0f)
    {
        return false;
    }

    const FVector2f HalfSize(
        FMath::Max(0.5f, Request.HalfSizePixels.X),
        FMath::Max(0.5f, Request.HalfSizePixels.Y));
    const FVector2f Center(
        FMath::Clamp(Request.UV.X, 0.0f, 1.0f) * static_cast<float>(Resolution),
        FMath::Clamp(Request.UV.Y, 0.0f, 1.0f) * static_cast<float>(Resolution));

    const int32 MinX = FMath::FloorToInt(Center.X - HalfSize.X - 1.0f);
    const int32 MinY = FMath::FloorToInt(Center.Y - HalfSize.Y - 1.0f);
    const int32 MaxX = FMath::CeilToInt(Center.X + HalfSize.X + 1.0f);
    const int32 MaxY = FMath::CeilToInt(Center.Y + HalfSize.Y + 1.0f);

    OutDispatch.DispatchMin = FIntPoint(MinX, MinY);
    OutDispatch.DispatchSize = FIntPoint(MaxX - MinX + 1, MaxY - MinY + 1);
    OutDispatch.UV = Request.UV;
    OutDispatch.CenterPixels = Center;
    OutDispatch.HalfSizePixels = HalfSize;
    OutDispatch.Amount = FMath::Clamp(Request.Amount, 0.0f, 1.0f);
    OutDispatch.bDroplet2 = Request.bDroplet2;
    return OutDispatch.DispatchSize.X > 0 && OutDispatch.DispatchSize.Y > 0;
}

bool BuildAbsorptionStampDispatch(
    const FDWCResolvedSurfaceContact& Contact,
    const FDWCGPUBakedTriangle& Triangle,
    const float RadiusPixels,
    const int32 Resolution,
    FTriangleAbsorptionDispatch& OutDispatch)
{
    if (Resolution <= 0 || Contact.ContactUV.ContainsNaN() ||
        !FMath::IsFinite(Contact.ContactUV.X) || !FMath::IsFinite(Contact.ContactUV.Y))
    {
        return false;
    }

    const float SafeRadiusPixels = FMath::Max(0.5f, RadiusPixels);
    const FVector2f ClampedUV(
        FMath::Clamp(Contact.ContactUV.X, 0.0f, 1.0f),
        FMath::Clamp(Contact.ContactUV.Y, 0.0f, 1.0f));
    const FIntPoint CenterTexel(
        FMath::Clamp(FMath::FloorToInt(ClampedUV.X * Resolution), 0, Resolution - 1),
        FMath::Clamp(FMath::FloorToInt(ClampedUV.Y * Resolution), 0, Resolution - 1));
    const FVector2f CenterPixels(
        static_cast<float>(CenterTexel.X) + 0.5f,
        static_cast<float>(CenterTexel.Y) + 0.5f);

    const int32 MinX = FMath::Clamp(
        FMath::FloorToInt(CenterPixels.X - SafeRadiusPixels - 1.0f),
        0,
        Resolution - 1);
    const int32 MinY = FMath::Clamp(
        FMath::FloorToInt(CenterPixels.Y - SafeRadiusPixels - 1.0f),
        0,
        Resolution - 1);
    const int32 MaxX = FMath::Clamp(
        FMath::CeilToInt(CenterPixels.X + SafeRadiusPixels + 1.0f),
        0,
        Resolution - 1);
    const int32 MaxY = FMath::Clamp(
        FMath::CeilToInt(CenterPixels.Y + SafeRadiusPixels + 1.0f),
        0,
        Resolution - 1);
    if (MaxX < MinX || MaxY < MinY)
    {
        return false;
    }

    const float InverseResolution = 1.0f / static_cast<float>(Resolution);
    OutDispatch.DispatchMin = FIntPoint(MinX, MinY);
    OutDispatch.DispatchSize = FIntPoint(MaxX - MinX + 1, MaxY - MinY + 1);
    OutDispatch.UV01 = FVector4f(
        CenterPixels.X * InverseResolution,
        CenterPixels.Y * InverseResolution,
        SafeRadiusPixels * InverseResolution,
        SafeRadiusPixels * InverseResolution);
    OutDispatch.UV2AndSettings.X =
        static_cast<float>(FMath::Max(0, Triangle.UVIslandID) + 1);
    return true;
}

bool IsSampledWetAreaContact(const FDWCResolvedSurfaceContact& Contact)
{
    constexpr float TriangleCenterWeight = 1.0f / 3.0f;
    constexpr float WeightTolerance = 1.0e-4f;

    // ResolveWetArea has always emitted one contact at the exact triangle centroid.
    // Keep that CPU contract untouched and select the GPU-only stamp path here.
    return FMath::IsNearlyEqual(Contact.Barycentric.X, TriangleCenterWeight, WeightTolerance) &&
           FMath::IsNearlyEqual(Contact.Barycentric.Y, TriangleCenterWeight, WeightTolerance) &&
           FMath::IsNearlyEqual(Contact.Barycentric.Z, TriangleCenterWeight, WeightTolerance) &&
           FMath::IsNearlyZero(Contact.DistanceToSurface) &&
           FMath::IsNearlyEqual(Contact.TriangleInfluence, 1.0f, WeightTolerance);
}

bool BuildDispatchBounds(
    const FDWCGPUBakedTriangle& Triangle,
    const int32 Resolution,
    FIntPoint& OutMin,
    FIntPoint& OutSize)
{
    if (Resolution <= 0)
    {
        return false;
    }

    const double MinU = FMath::Min3(Triangle.UV0.X, Triangle.UV1.X, Triangle.UV2.X);
    const double MaxU = FMath::Max3(Triangle.UV0.X, Triangle.UV1.X, Triangle.UV2.X);
    const double MinV = FMath::Min3(Triangle.UV0.Y, Triangle.UV1.Y, Triangle.UV2.Y);
    const double MaxV = FMath::Max3(Triangle.UV0.Y, Triangle.UV1.Y, Triangle.UV2.Y);

    const int32 MinX = FMath::Clamp(FMath::FloorToInt(MinU * Resolution), 0, Resolution - 1);
    const int32 MinY = FMath::Clamp(FMath::FloorToInt(MinV * Resolution), 0, Resolution - 1);
    const int32 MaxX = FMath::Clamp(FMath::CeilToInt(MaxU * Resolution) - 1, 0, Resolution - 1);
    const int32 MaxY = FMath::Clamp(FMath::CeilToInt(MaxV * Resolution) - 1, 0, Resolution - 1);

    if (MaxX < MinX || MaxY < MinY)
    {
        return false;
    }

    OutMin = FIntPoint(MinX, MinY);
    OutSize = FIntPoint(MaxX - MinX + 1, MaxY - MinY + 1);
    return OutSize.X > 0 && OutSize.Y > 0;
}

bool CanUseBinnedAbsorption(const FTriangleAbsorptionDispatch& Dispatch)
{
    const bool bSupportedMode = Dispatch.P2AndMode.W < 0.5f || Dispatch.P2AndMode.W > 1.5f;
    return bSupportedMode &&
           Dispatch.P0AndAmount.W > 0.0f &&
           Dispatch.DispatchSize.X > 0 &&
           Dispatch.DispatchSize.Y > 0;
}

void AppendBinnedAbsorptionContact(
    const FTriangleAbsorptionDispatch& Dispatch,
    TArray<FVector4f>& OutContacts)
{
    OutContacts.Add(Dispatch.UV01);
    OutContacts.Add(Dispatch.UV2AndSettings);
    OutContacts.Add(Dispatch.ContactAndRadius);
    OutContacts.Add(Dispatch.P0AndAmount);
    OutContacts.Add(Dispatch.P1AndMaxWetness);
    OutContacts.Add(Dispatch.P2AndMode);
}

void BuildBinnedAbsorptionDispatches(FSlotRenderDispatch& SlotDispatch)
{
    if (SlotDispatch.Resolution <= 0 ||
        CVarDWCGPUBinnedAbsorption.GetValueOnAnyThread() == 0 ||
        SlotDispatch.AbsorptionDispatches.Num() < DWCAbsorptionBinMinDispatches)
    {
        return;
    }

    const FIntPoint TileGridSize(
        FMath::DivideAndRoundUp(SlotDispatch.Resolution, DWCAbsorptionBinTileSize),
        FMath::DivideAndRoundUp(SlotDispatch.Resolution, DWCAbsorptionBinTileSize));
    const int32 TileCount = TileGridSize.X * TileGridSize.Y;
    if (TileCount <= 0)
    {
        return;
    }

    TArray<TArray<uint32>> TileContactLists;
    TileContactLists.SetNum(TileCount);

    TArray<FVector4f> BinnedContacts;
    BinnedContacts.Reserve(SlotDispatch.AbsorptionDispatches.Num() * DWCAbsorptionContactFloat4Count);

    TArray<FTriangleAbsorptionDispatch> FallbackDispatches;
    FallbackDispatches.Reserve(SlotDispatch.AbsorptionDispatches.Num());

    for (const FTriangleAbsorptionDispatch& Dispatch : SlotDispatch.AbsorptionDispatches)
    {
        if (!CanUseBinnedAbsorption(Dispatch))
        {
            FallbackDispatches.Add(Dispatch);
            continue;
        }

        const uint32 ContactIndex = static_cast<uint32>(BinnedContacts.Num() / DWCAbsorptionContactFloat4Count);
        AppendBinnedAbsorptionContact(Dispatch, BinnedContacts);

        const FIntPoint MaxTexel = Dispatch.DispatchMin + Dispatch.DispatchSize - FIntPoint(1, 1);
        const int32 MinTileX = FMath::Clamp(Dispatch.DispatchMin.X / DWCAbsorptionBinTileSize, 0, TileGridSize.X - 1);
        const int32 MinTileY = FMath::Clamp(Dispatch.DispatchMin.Y / DWCAbsorptionBinTileSize, 0, TileGridSize.Y - 1);
        const int32 MaxTileX = FMath::Clamp(MaxTexel.X / DWCAbsorptionBinTileSize, 0, TileGridSize.X - 1);
        const int32 MaxTileY = FMath::Clamp(MaxTexel.Y / DWCAbsorptionBinTileSize, 0, TileGridSize.Y - 1);

        for (int32 TileY = MinTileY; TileY <= MaxTileY; ++TileY)
        {
            for (int32 TileX = MinTileX; TileX <= MaxTileX; ++TileX)
            {
                TileContactLists[TileY * TileGridSize.X + TileX].Add(ContactIndex);
            }
        }
    }

    if (BinnedContacts.IsEmpty())
    {
        return;
    }

    TArray<FUint2GPU> TileBins;
    TileBins.SetNum(TileCount);
    TArray<uint32> TileContactIndices;
    for (int32 TileIndex = 0; TileIndex < TileCount; ++TileIndex)
    {
        const TArray<uint32>& ContactList = TileContactLists[TileIndex];
        const uint32 Offset = static_cast<uint32>(TileContactIndices.Num());
        const uint32 Count = static_cast<uint32>(ContactList.Num());
        TileBins[TileIndex] = FUint2GPU(Offset, Count);
        TileContactIndices.Append(ContactList);
    }

    if (TileContactIndices.IsEmpty())
    {
        return;
    }

    SlotDispatch.AbsorptionDispatches = MoveTemp(FallbackDispatches);
    SlotDispatch.BinnedAbsorptionContacts = MoveTemp(BinnedContacts);
    SlotDispatch.BinnedAbsorptionTileBins = MoveTemp(TileBins);
    SlotDispatch.BinnedAbsorptionTileContactIndices = MoveTemp(TileContactIndices);
    SlotDispatch.BinnedAbsorptionTileGridSize = TileGridSize;
}

FVector4f MakePositionAndValue(const FVector& Position, const float Value)
{
    return FVector4f(
        static_cast<float>(Position.X),
        static_cast<float>(Position.Y),
        static_cast<float>(Position.Z),
        Value);
}

void FillTriangleUVs(const FDWCGPUBakedTriangle& Triangle, FTriangleAbsorptionDispatch& OutDispatch)
{
    OutDispatch.UV01 = FVector4f(
        static_cast<float>(Triangle.UV0.X),
        static_cast<float>(Triangle.UV0.Y),
        static_cast<float>(Triangle.UV1.X),
        static_cast<float>(Triangle.UV1.Y));
    OutDispatch.UV2AndSettings = FVector4f(
        static_cast<float>(Triangle.UV2.X),
        static_cast<float>(Triangle.UV2.Y),
        0.0f,
        0.0f);
}

void CollectExpectedWettableSlots(const UWetClothingAsset& Asset, TSet<int32>& OutMaterialSlots)
{
    OutMaterialSlots.Reset();
    for (const FWetClothingAuthoredMaterialSlot& SlotState : Asset.Authored.PartData.EditableWetPartData.MaterialSlots)
    {
        if (SlotState.bIsWettableSlot && SlotState.MaterialSlotIndex != INDEX_NONE)
        {
            OutMaterialSlots.Add(SlotState.MaterialSlotIndex);
        }
    }
}

FString JoinSortedSlotSet(const TSet<int32>& MaterialSlots)
{
    TArray<int32> SortedSlots = MaterialSlots.Array();
    SortedSlots.Sort();

    TArray<FString> SlotStrings;
    SlotStrings.Reserve(SortedSlots.Num());
    for (const int32 MaterialSlot : SortedSlots)
    {
        SlotStrings.Add(FString::FromInt(MaterialSlot));
    }
    return FString::Join(SlotStrings, TEXT(","));
}

bool MaterialHasTextureParameter(UMaterialInterface* Material, const FName ParameterName)
{
    if (Material == nullptr || ParameterName.IsNone())
    {
        return false;
    }

    TArray<FMaterialParameterInfo> ParameterInfos;
    TArray<FGuid> ParameterIds;
    Material->GetAllTextureParameterInfo(ParameterInfos, ParameterIds);
    return ParameterInfos.ContainsByPredicate(
        [ParameterName](const FMaterialParameterInfo& ParameterInfo)
        {
            return ParameterInfo.Name == ParameterName;
        });
}

bool MaterialHasScalarParameter(UMaterialInterface* Material, const FName ParameterName)
{
    if (Material == nullptr || ParameterName.IsNone())
    {
        return false;
    }

    TArray<FMaterialParameterInfo> ParameterInfos;
    TArray<FGuid> ParameterIds;
    Material->GetAllScalarParameterInfo(ParameterInfos, ParameterIds);
    return ParameterInfos.ContainsByPredicate(
        [ParameterName](const FMaterialParameterInfo& ParameterInfo)
        {
            return ParameterInfo.Name == ParameterName;
        });
}

template <typename ElementType>
FRDGBufferRef RegisterOrUploadStructuredBuffer(
    FRDGBuilder& GraphBuilder,
    TRefCountPtr<FRDGPooledBuffer>& PooledBuffer,
    const TCHAR* Name,
    const TArray<ElementType>& Data)
{
    if (PooledBuffer.IsValid())
    {
        return GraphBuilder.RegisterExternalBuffer(PooledBuffer, Name);
    }

    if (Data.IsEmpty())
    {
        return nullptr;
    }

    FRDGBufferRef Buffer = CreateStructuredBuffer(GraphBuilder, Name, Data);
    GraphBuilder.QueueBufferExtraction(Buffer, &PooledBuffer);
    return Buffer;
}
} // namespace DWCGPUBackendPrivate

using namespace DWCGPUBackendPrivate;

struct FDWCGPUBackend::FStaticSimulationData
{
    struct FSectionData
    {
        TArray<FUint4GPU> TriangleIndices;
        TArray<FVector4f> TriangleUV01;
        TArray<FVector4f> TriangleUV2RestArea;
        TArray<FVector4f> RestPositions;
    };

    struct FSlotData
    {
        int32 MaterialSlotIndex = INDEX_NONE;
        int32 Resolution = 0;
        int32 SurfaceWaterResolution = 0;
        TArray<FUint4GPU> TexelLookup;
        TArray<FUint4GPU> SurfaceTexelLookup;
        TArray<FUint4GPU> SeamDestinations;
        TArray<FVector4f> SeamIncoming;
    };

    int32 TriangleCount = 0;
    TArray<FSectionData> Sections;
    TArray<FVector4f> Profiles;
    TArray<uint32> TriangleProfileIndices;
    TArray<FVector4f> TriangleDataToSurfaceWaterNormalUV;
    TArray<FVector4f> TriangleUV01;
    TArray<FVector4f> TriangleUV2AndDroplet;
    TArray<FVector4f> TriangleFlowDropletSettings;
    TArray<float> TriangleFlowSpawnPositionSpread;
    TArray<FUint4GPU> TriangleSurfaceMetadata;
    TArray<FSlotData> Slots;
};

struct FDWCGPUBackend::FRenderState
{
    /** Per-component profile buffer after runtime Spread/Dry/Gravity scale overrides. */
    TRefCountPtr<FRDGPooledBuffer> Profiles;

    /** Per-component triangle -> runtime profile index buffer because profile dedupe can change without a GPU-map rebake. */
    TRefCountPtr<FRDGPooledBuffer> TriangleProfileIndices;

    /** Static triangle data used to resolve Niagara GPU contacts to independent static/flow Data-UV stamps. */
    TRefCountPtr<FRDGPooledBuffer> NiagaraTriangleUV01;
    TRefCountPtr<FRDGPooledBuffer> NiagaraTriangleUV2AndDroplet;
    TRefCountPtr<FRDGPooledBuffer> NiagaraTriangleFlowDropletSettings;
    TRefCountPtr<FRDGPooledBuffer> NiagaraTriangleFlowSpawnPositionSpread;
    TRefCountPtr<FRDGPooledBuffer> NiagaraTriangleSurfaceMetadata;

    /** Per-character buffers updated from the current skinned pose. */
    TRefCountPtr<FRDGPooledBuffer> TriangleFlow;
    TRefCountPtr<FRDGPooledBuffer> TriangleMetric;
    TRefCountPtr<FRDGPooledBuffer> TrianglePositions;
    TArray<TRefCountPtr<FRDGPooledBuffer>> RestPositionBuffers;

};

UTextureRenderTarget2D* FDWCGPUBackend::FMaterialSlotRuntime::GetCurrentMap() const
{
    return WetnessMaps.IsValidIndex(CurrentTextureIndex) ? WetnessMaps[CurrentTextureIndex].Get() : nullptr;
}

UTextureRenderTarget2D* FDWCGPUBackend::FMaterialSlotRuntime::GetNextMap() const
{
    const int32 NextIndex = 1 - CurrentTextureIndex;
    return WetnessMaps.IsValidIndex(NextIndex) ? WetnessMaps[NextIndex].Get() : nullptr;
}

UTextureRenderTarget2D* FDWCGPUBackend::FMaterialSlotRuntime::GetCurrentPendingMap() const
{
    return PendingWetnessMaps.IsValidIndex(CurrentPendingTextureIndex)
        ? PendingWetnessMaps[CurrentPendingTextureIndex].Get()
        : nullptr;
}

UTextureRenderTarget2D* FDWCGPUBackend::FMaterialSlotRuntime::GetNextPendingMap() const
{
    const int32 NextIndex = 1 - CurrentPendingTextureIndex;
    return PendingWetnessMaps.IsValidIndex(NextIndex)
        ? PendingWetnessMaps[NextIndex].Get()
        : nullptr;
}

void FDWCGPUBackend::FMaterialSlotRuntime::SwapMaps()
{
    CurrentTextureIndex = 1 - CurrentTextureIndex;
}

void FDWCGPUBackend::FMaterialSlotRuntime::SwapPendingMaps()
{
    CurrentPendingTextureIndex = 1 - CurrentPendingTextureIndex;
}

bool FDWCGPUBackend::Initialize(const FDWCGPUBackendInitArgs& Args)
{
    Shutdown();

    if (!Args.OwnerComponent || !Args.TargetSkeletalMesh || !Args.WetClothingAsset ||
        !Args.WetMaterialInstances || Args.LODIndex < 0)
    {
        UE_LOG(LogDWCGPU, Warning, TEXT("DWCGPU: Full wetness-map simulation requires an owner, mesh, asset, and valid LOD."));
        return false;
    }

    const USkeletalMesh* SkeletalMesh = Args.TargetSkeletalMesh->GetSkeletalMeshAsset();
    const FDWCGPULODBakeData& GPUData = Args.WetClothingAsset->GetGPUWetMapRuntimeData(Args.LODIndex);
    if (!SkeletalMesh || !Args.WetClothingAsset->IsGPUWetMapDataValidForMesh(SkeletalMesh, Args.LODIndex) ||
        !GPUData.bMapDataValid || GPUData.MapBakeVersion != DWCFullSimulationMapVersion || GPUData.LODIndex != Args.LODIndex)
    {
        UE_LOG(LogDWCGPU, Warning, TEXT("DWCGPU: GPU simulation maps are missing or out of date for %s. Use Build for Runtime > Build GPU Runtime Data in the Wet Clothing Asset editor."), *GetNameSafe(SkeletalMesh));
        return false;
    }

    OwnerComponent = Args.OwnerComponent;
    TargetSkeletalMesh = Args.TargetSkeletalMesh;
    WetClothingAsset = Args.WetClothingAsset;
    WetMaterialInstances = Args.WetMaterialInstances;
    WetnessMapParameterName = DWCWetMaterialParameters::WetnessMap();
    LODIndex = Args.LODIndex;
    MaxWetness = Args.WetnessSettings ? FMath::Max(0.0f, Args.WetnessSettings->MaxWetness) : 1.0f;
    SpreadRateScale = FMath::Max(0.0f, Args.SpreadRateScale);
    DryRateScale = FMath::Max(0.0f, Args.DryRateScale);
    GravityFlowStrengthScale = FMath::Max(0.0f, Args.GravityFlowStrengthScale);
    CapillaryImmediateAbsorptionFraction =
        FMath::Max(0.0f, Args.CapillaryImmediateAbsorptionFraction);
    ReceiverGPUId = Args.ReceiverGPUId;
    bUseEightDirectionDiffusion = Args.bUseEightDirectionDiffusion;

    if (UE_LOG_ACTIVE(LogDWCGPU, VeryVerbose))
    {
        UE_LOG(
            LogDWCGPU,
            Log,
            TEXT("DWCGPU: Initializing backend. Owner='%s', mesh='%s', asset='%s', LOD=%d, DWCDataUV=%d, wetnessParam='%s', bakedSlots=%d, bakedTriangles=%d, maxWetness=%.3f, spreadScale=%.3f, dryScale=%.3f, gravityScale=%.3f."),
            *GetNameSafe(Args.OwnerComponent),
            *GetNameSafe(Args.TargetSkeletalMesh),
            *GetNameSafe(Args.WetClothingAsset),
            LODIndex,
            Args.WetClothingAsset->GetDWCDataUVChannelIndex(),
            *WetnessMapParameterName.ToString(),
            GPUData.MaterialSlots.Num(),
            GPUData.Triangles.Num(),
            MaxWetness,
            SpreadRateScale,
            DryRateScale,
            GravityFlowStrengthScale);
    }

    if (!BuildStaticSimulationData() || !AcquireSharedStaticResources() || !CreateSlotResources())
    {
        Shutdown();
        return false;
    }

    // Debug lookup is optional. Normal GPU simulation must not fail when debug metadata is unavailable.
    BuildDebugVertexLookup();

    bInitialized = true;
    if (UE_LOG_ACTIVE(LogDWCGPU, VeryVerbose))
    {
        UE_LOG(
            LogDWCGPU,
            Log,
            TEXT("DWCGPU: Backend initialized. Runtime material slots=%d."),
            MaterialSlots.Num());
    }
    return true;
}

bool FDWCGPUBackend::BuildStaticSimulationData()
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const USkeletalMeshComponent* MeshComponent = TargetSkeletalMesh.Get();
    const USkeletalMesh* SkeletalMesh = MeshComponent ? MeshComponent->GetSkeletalMeshAsset() : nullptr;
    if (!Asset || !SkeletalMesh)
    {
        return false;
    }

    const FDWCGPULODBakeData& Baked = Asset->GetGPUWetMapRuntimeData(LODIndex);
    const int32 ExpectedDWCDataUVChannel = Asset->GetDWCDataUVChannelIndex();
    if (ExpectedDWCDataUVChannel == INDEX_NONE)
    {
        UE_LOG(
            LogDWCGPU,
            Warning,
            TEXT("DWCGPU: Wet Clothing Asset '%s' has no generated DWC Data UV channel. Create a new WCA if the sealed DWC Data UV is missing or invalid before using GPU simulation."),
            *GetNameSafe(Asset));
        return false;
    }

    TSet<int32> ExpectedWettableSlots;
    CollectExpectedWettableSlots(*Asset, ExpectedWettableSlots);
    if (ExpectedWettableSlots.IsEmpty())
    {
        UE_LOG(
            LogDWCGPU,
            Warning,
            TEXT("DWCGPU: Wet Clothing Asset '%s' has no wettable material slots for GPU simulation."),
            *GetNameSafe(Asset));
        return false;
    }

    TSharedPtr<FStaticSimulationData, ESPMode::ThreadSafe> Data = MakeShared<FStaticSimulationData, ESPMode::ThreadSafe>();
    Data->TriangleCount = Baked.Triangles.Num();
    Data->Profiles.Reserve(FMath::Max(1, Baked.Profiles.Num()));
    const FWetClothingEditableWetPartData& WetPartData = Asset->Authored.PartData.EditableWetPartData;
    TArray<FResolvedAbsorbedWaterSimulationParameters> CurrentAuthoredProfiles;
    TArray<FWetnessProfileParameters> CurrentResolvedProfiles;
    CurrentAuthoredProfiles.SetNum(WetPartData.Profiles.Num());
    CurrentResolvedProfiles.SetNum(WetPartData.Profiles.Num());
    for (int32 ProfileIndex = 0; ProfileIndex < WetPartData.Profiles.Num(); ++ProfileIndex)
    {
        FWetnessProfileParameters& ResolvedProfile = CurrentResolvedProfiles[ProfileIndex];
        ResolvedProfile =
            ResolveRuntimeWetnessProfileParameters(*Asset, ProfileIndex);
        CurrentAuthoredProfiles[ProfileIndex] =
            MakeRuntimeAbsorbedWaterSimulationProfile(ResolvedProfile);
    }
    float MaxSpreadRate = 0.0f;
    float MaxDryRate = 0.0f;
    float MaxGravityFlowStrength = 0.0f;
    if (Data->TriangleCount <= 0)
    {
        return false;
    }

    Data->TriangleProfileIndices.Init(MAX_uint32, Data->TriangleCount);
    Data->TriangleDataToSurfaceWaterNormalUV.Init(
        FVector4f(1.0f, 0.0f, 0.0f, 1.0f),
        Data->TriangleCount);
    Data->TriangleUV01.Init(FVector4f(0, 0, 0, 0), Data->TriangleCount);
    Data->TriangleUV2AndDroplet.Init(FVector4f(0, 0, 0, 0), Data->TriangleCount);
    Data->TriangleFlowDropletSettings.Init(FVector4f(0, 0, 0, 0), Data->TriangleCount);
    Data->TriangleFlowSpawnPositionSpread.Init(0.0f, Data->TriangleCount);
    Data->TriangleSurfaceMetadata.Init(FUint4GPU(), Data->TriangleCount);

    int32 MaxSectionIndex = INDEX_NONE;
    for (const FDWCGPUBakedTriangle& Triangle : Baked.Triangles)
    {
        if (Triangle.TriangleID != INDEX_NONE)
        {
            MaxSectionIndex = FMath::Max(MaxSectionIndex, Triangle.RenderSectionIndex);
        }
    }
    Data->Sections.SetNum(FMath::Max(0, MaxSectionIndex + 1));

    const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
    const FSkeletalMeshLODRenderData* LODData =
        RenderData && RenderData->LODRenderData.IsValidIndex(LODIndex)
            ? &RenderData->LODRenderData[LODIndex]
            : nullptr;
    const int32 VertexCount = LODData
        ? static_cast<int32>(LODData->StaticVertexBuffers.PositionVertexBuffer.GetNumVertices())
        : 0;
    TBitArray<> UsedSections(false, Data->Sections.Num());

    for (const FDWCGPUBakedTriangle& Triangle : Baked.Triangles)
    {
        if (Triangle.TriangleID == INDEX_NONE || !Data->TriangleProfileIndices.IsValidIndex(Triangle.TriangleID) ||
            !Data->Sections.IsValidIndex(Triangle.RenderSectionIndex))
        {
            return false;
        }
        if (Triangle.UVChannelIndex != ExpectedDWCDataUVChannel)
        {
            UE_LOG(
                LogDWCGPU,
                Warning,
                TEXT("DWCGPU: Baked triangle %d in '%s' uses UV%d, but the asset now uses DWC Data UV%d. Create a new WCA if the sealed Data UV layout changed, then use Build for Runtime > Build GPU Runtime Data."),
                Triangle.TriangleID,
                *GetNameSafe(Asset),
                Triangle.UVChannelIndex,
                ExpectedDWCDataUVChannel);
            return false;
        }
        if (!ExpectedWettableSlots.Contains(Triangle.MaterialSlotIndex))
        {
            UE_LOG(
                LogDWCGPU,
                Warning,
                TEXT("DWCGPU: Baked triangle %d in '%s' targets material slot %d, which is no longer marked wettable. Save the Wet Clothing Asset and use Build for Runtime > Build GPU Runtime Data again."),
                Triangle.TriangleID,
                *GetNameSafe(Asset),
                Triangle.MaterialSlotIndex);
            return false;
        }

        const int32 AuthoredProfileIndex = ResolveAuthoredProfileIndexForBakedTriangle(*Asset, Triangle);
        if (AuthoredProfileIndex == INDEX_NONE ||
            !CurrentResolvedProfiles.IsValidIndex(AuthoredProfileIndex))
        {
            UE_LOG(
                LogDWCGPU,
                Warning,
                TEXT("DWCGPU: Could not resolve current Wetness Profile for baked triangle %d in '%s'. Save the Wet Clothing Asset and use Build for Runtime > Build GPU Runtime Data again."),
                Triangle.TriangleID,
                *GetNameSafe(Asset));
            return false;
        }
        const FWetClothingWetPartEntry* WetPart =
            ResolveWetPartForBakedTriangle(*Asset, Triangle);
        const FWetnessProfileParameters& ResolvedProfile =
            CurrentResolvedProfiles[AuthoredProfileIndex];
        const FSurfaceWaterProfileParameters& Surface = ResolvedProfile.SurfaceWater;
        const float DropletRadiusScale = WetPart != nullptr
            ? WetPart->SurfaceWater.GetResolvedDropletStampSizeScale()
            : 1.0f;
        const float DropletRadiusPixels =
            Surface.bEnabled
                ? FMath::Max(0.0f, Surface.DropletRadiusPixels * DropletRadiusScale)
                : 0.0f;
        const float DropletSpawnProbability =
            Surface.bEnabled ? FMath::Clamp(Surface.DropletSpawnProbability, 0.0f, 1.0f) : 0.0f;
        const float DropletFlowSizeScale = WetPart != nullptr
            ? WetPart->SurfaceWater.GetResolvedDropletFlowStampSizeScale()
            : 1.0f;
        const bool bEnableDroplet2 = Surface.bEnabled;
        const float DropletFlowRadiusPixels =
            bEnableDroplet2
                ? FMath::Max(0.0f, Surface.DropletFlowRadiusPixels * DropletFlowSizeScale)
                : 0.0f;
        const float DropletFlowHeightPixels =
            bEnableDroplet2
                ? FMath::Max(0.0f, Surface.DropletFlowHeightPixels * DropletFlowSizeScale)
                : 0.0f;
        const float DropletFlowSpawnProbability =
            bEnableDroplet2
                ? FMath::Clamp(Surface.DropletFlowSpawnProbability, 0.0f, 1.0f)
                : 0.0f;
        const float RejectedWaterFraction =
            Surface.bEnabled ? FMath::Clamp(ResolvedProfile.GetRejectedWaterFraction(), 0.0f, 1.0f) : 0.0f;

        Data->TriangleUV01[Triangle.TriangleID] = FVector4f(
            static_cast<float>(Triangle.UV0.X),
            static_cast<float>(Triangle.UV0.Y),
            static_cast<float>(Triangle.UV1.X),
            static_cast<float>(Triangle.UV1.Y));
        Data->TriangleUV2AndDroplet[Triangle.TriangleID] = FVector4f(
            static_cast<float>(Triangle.UV2.X),
            static_cast<float>(Triangle.UV2.Y),
            DropletRadiusPixels,
            Surface.bEnabled
                ? FMath::Max(0.0f, Surface.DropletHeightPixels * DropletRadiusScale)
                : 0.0f);
        Data->TriangleFlowDropletSettings[Triangle.TriangleID] = FVector4f(
            DropletFlowRadiusPixels,
            DropletFlowHeightPixels,
            0.0f,
            DropletFlowSpawnProbability);
        Data->TriangleFlowSpawnPositionSpread[Triangle.TriangleID] =
            bEnableDroplet2
                ? FMath::Clamp(Surface.DropletFlowSpawnPositionSpread, 0.0f, 1.0f)
                : 0.0f;
        Data->TriangleSurfaceMetadata[Triangle.TriangleID] = FUint4GPU(
            static_cast<uint32>(Triangle.MaterialSlotIndex),
            static_cast<uint32>(FMath::Max(0, Triangle.UVIslandID) + 1),
            FloatToBits(DropletSpawnProbability),
            FloatToBits(RejectedWaterFraction));

        const FResolvedAbsorbedWaterSimulationParameters& CurrentProfile =
            CurrentAuthoredProfiles[AuthoredProfileIndex];
        const int32 RuntimeProfileIndex = FindOrAddGPUProfile(
            Data->Profiles,
            CurrentProfile,
            ResolvedProfile.GetDropletDryRatePerSecond(),
            SpreadRateScale,
            DryRateScale,
            GravityFlowStrengthScale,
            MaxSpreadRate,
            MaxDryRate,
            MaxGravityFlowStrength);

        Data->TriangleProfileIndices[Triangle.TriangleID] = static_cast<uint32>(RuntimeProfileIndex);
        Data->TriangleDataToSurfaceWaterNormalUV[Triangle.TriangleID] = FVector4f(
            static_cast<float>(Triangle.DataToSurfaceWaterNormalUV.X),
            static_cast<float>(Triangle.DataToSurfaceWaterNormalUV.Y),
            static_cast<float>(Triangle.DataToSurfaceWaterNormalUV.Z),
            static_cast<float>(Triangle.DataToSurfaceWaterNormalUV.W));

        FStaticSimulationData::FSectionData& Section = Data->Sections[Triangle.RenderSectionIndex];
        UsedSections[Triangle.RenderSectionIndex] = true;
        Section.TriangleIndices.Add(FUint4GPU(
            static_cast<uint32>(Triangle.VertexIndices.X),
            static_cast<uint32>(Triangle.VertexIndices.Y),
            static_cast<uint32>(Triangle.VertexIndices.Z),
            static_cast<uint32>(Triangle.TriangleID)));
        Section.TriangleUV01.Add(FVector4f(
            static_cast<float>(Triangle.UV0.X), static_cast<float>(Triangle.UV0.Y),
            static_cast<float>(Triangle.UV1.X), static_cast<float>(Triangle.UV1.Y)));
        Section.TriangleUV2RestArea.Add(FVector4f(
            static_cast<float>(Triangle.UV2.X), static_cast<float>(Triangle.UV2.Y),
            Triangle.RestSurfaceArea, 0.0f));
    }
    if (LODData && VertexCount > 0)
    {
        const FPositionVertexBuffer& PositionBuffer = LODData->StaticVertexBuffers.PositionVertexBuffer;
        for (int32 SectionIndex = 0; SectionIndex < Data->Sections.Num(); ++SectionIndex)
        {
            if (!UsedSections[SectionIndex])
            {
                continue;
            }

            FStaticSimulationData::FSectionData& Section = Data->Sections[SectionIndex];
            Section.RestPositions.SetNumUninitialized(VertexCount);
            for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
            {
                const FVector3f Position = PositionBuffer.VertexPosition(VertexIndex);
                Section.RestPositions[VertexIndex] = FVector4f(Position.X, Position.Y, Position.Z, 1.0f);
            }
        }
    }
    if (Data->Profiles.IsEmpty())
    {
        return false;
    }
    if (MaxSpreadRate <= 0.0f && MaxDryRate <= 0.0f)
    {
        UE_LOG(
            LogDWCGPU,
            Warning,
            TEXT("DWCGPU: All current GPU wetness profiles for '%s' have zero spread and dry rates after runtime scales. Absorption can still appear, but absorbed wetness will not diffuse or dry. Profiles=%d, SpreadScale=%.3f, DryScale=%.3f, GravityScale=%.3f."),
            *GetNameSafe(Asset),
            Data->Profiles.Num(),
            SpreadRateScale,
            DryRateScale,
            GravityFlowStrengthScale);
    }

    TSet<int32> SeenMaterialSlots;
    for (const FDWCGPUMaterialSlotBakeData& BakedSlot : Baked.MaterialSlots)
    {
        const int64 ExpectedCount64 = static_cast<int64>(BakedSlot.Resolution) * static_cast<int64>(BakedSlot.Resolution);
        if (BakedSlot.Resolution <= 0 || ExpectedCount64 > MAX_int32 || SeenMaterialSlots.Contains(BakedSlot.MaterialSlotIndex))
        {
            return false;
        }
        if (!ExpectedWettableSlots.Contains(BakedSlot.MaterialSlotIndex))
        {
            UE_LOG(
                LogDWCGPU,
                Warning,
                TEXT("DWCGPU: Baked GPU map for '%s' targets material slot %d, which is no longer marked wettable. Save the Wet Clothing Asset and use Build for Runtime > Build GPU Runtime Data again."),
                *GetNameSafe(Asset),
                BakedSlot.MaterialSlotIndex);
            return false;
        }
        if (BakedSlot.UVChannelIndex != ExpectedDWCDataUVChannel)
        {
            UE_LOG(
                LogDWCGPU,
                Warning,
                TEXT("DWCGPU: Baked GPU map for '%s' slot %d uses UV%d, but the asset now uses DWC Data UV%d. Create a new WCA if the sealed Data UV layout changed, then use Build for Runtime > Build GPU Runtime Data."),
                *GetNameSafe(Asset),
                BakedSlot.MaterialSlotIndex,
                BakedSlot.UVChannelIndex,
                ExpectedDWCDataUVChannel);
            return false;
        }
        SeenMaterialSlots.Add(BakedSlot.MaterialSlotIndex);
        const int32 ExpectedCount = static_cast<int32>(ExpectedCount64);
        if (BakedSlot.Resolution <= 0 || BakedSlot.TexelTriangleIDs.Num() != ExpectedCount ||
            BakedSlot.PackedTexelBarycentricXY.Num() != ExpectedCount ||
            BakedSlot.RestTexelAreas.Num() != ExpectedCount || BakedSlot.ValidMask.Num() != ExpectedCount)
        {
            return false;
        }

        FStaticSimulationData::FSlotData& Slot = Data->Slots.AddDefaulted_GetRef();
        Slot.MaterialSlotIndex = BakedSlot.MaterialSlotIndex;
        Slot.Resolution = BakedSlot.Resolution;

        const auto BuildLookup =
            [&Baked, &BakedSlot](
                const TArray<int32>& TriangleIDs,
                const TArray<uint32>& PackedBarycentricXY,
                const TArray<float>& RestTexelAreas,
                const TArray<uint8>& ValidMask,
                TArray<FUint4GPU>& OutLookup)
            {
                if (TriangleIDs.Num() != PackedBarycentricXY.Num() ||
                    TriangleIDs.Num() != RestTexelAreas.Num() ||
                    TriangleIDs.Num() != ValidMask.Num())
                {
                    return false;
                }

                OutLookup.Reset(TriangleIDs.Num());
                for (int32 TexelIndex = 0; TexelIndex < TriangleIDs.Num(); ++TexelIndex)
                {
                    const int32 SignedTriangleID = TriangleIDs[TexelIndex];
                    const bool bValidTexel = ValidMask[TexelIndex] != 0;
                    if ((bValidTexel && (!Baked.Triangles.IsValidIndex(SignedTriangleID) ||
                                        Baked.Triangles[SignedTriangleID].MaterialSlotIndex != BakedSlot.MaterialSlotIndex ||
                                        RestTexelAreas[TexelIndex] <= 0.0f)) ||
                        (!bValidTexel && SignedTriangleID != INDEX_NONE))
                    {
                        return false;
                    }

                    const uint32 TriangleID = SignedTriangleID == INDEX_NONE
                        ? MAX_uint32
                        : static_cast<uint32>(SignedTriangleID);
                    uint32 IslandAndValid = 0u;
                    if (bValidTexel)
                    {
                        IslandAndValid = static_cast<uint32>(
                            FMath::Max(0, Baked.Triangles[SignedTriangleID].UVIslandID) + 1);
                    }
                    OutLookup.Add(FUint4GPU(
                        TriangleID,
                        PackedBarycentricXY[TexelIndex],
                        FloatToBits(RestTexelAreas[TexelIndex]),
                        IslandAndValid));
                }
                return true;
            };

        if (!BuildLookup(
                BakedSlot.TexelTriangleIDs,
                BakedSlot.PackedTexelBarycentricXY,
                BakedSlot.RestTexelAreas,
                BakedSlot.ValidMask,
                Slot.TexelLookup))
        {
            return false;
        }

        const bool bUsesSurfaceWater = Asset->DoesMaterialSlotUseSurfaceWater(BakedSlot.MaterialSlotIndex);
        if (bUsesSurfaceWater)
        {
            const int64 ExpectedSurfaceCount64 =
                static_cast<int64>(BakedSlot.SurfaceWaterResolution) * BakedSlot.SurfaceWaterResolution;
            if (BakedSlot.SurfaceWaterResolution <= 0 || ExpectedSurfaceCount64 > MAX_int32)
            {
                return false;
            }
            const int32 ExpectedSurfaceCount = static_cast<int32>(ExpectedSurfaceCount64);
            if (BakedSlot.SurfaceTexelTriangleIDs.Num() != ExpectedSurfaceCount ||
                BakedSlot.SurfacePackedTexelBarycentricXY.Num() != ExpectedSurfaceCount ||
                BakedSlot.SurfaceRestTexelAreas.Num() != ExpectedSurfaceCount ||
                BakedSlot.SurfaceValidMask.Num() != ExpectedSurfaceCount)
            {
                return false;
            }

            Slot.SurfaceWaterResolution = BakedSlot.SurfaceWaterResolution;
            if (!BuildLookup(
                    BakedSlot.SurfaceTexelTriangleIDs,
                    BakedSlot.SurfacePackedTexelBarycentricXY,
                    BakedSlot.SurfaceRestTexelAreas,
                    BakedSlot.SurfaceValidMask,
                    Slot.SurfaceTexelLookup))
            {
                return false;
            }
        }
        else if (BakedSlot.SurfaceWaterResolution != 0 ||
                 !BakedSlot.SurfaceTexelTriangleIDs.IsEmpty() ||
                 !BakedSlot.SurfacePackedTexelBarycentricXY.IsEmpty() ||
                 !BakedSlot.SurfaceRestTexelAreas.IsEmpty() ||
                 !BakedSlot.SurfaceValidMask.IsEmpty())
        {
            return false;
        }

        for (const FDWCGPUSeamDestination& Destination : BakedSlot.SeamDestinations)
        {
            if (Destination.DestinationTexelIndex < 0 || Destination.DestinationTexelIndex >= ExpectedCount ||
                Destination.IncomingStartIndex < 0 || Destination.IncomingCount <= 0 ||
                Destination.IncomingStartIndex + Destination.IncomingCount > BakedSlot.SeamIncoming.Num())
            {
                return false;
            }

            const uint32 CompactStart = static_cast<uint32>(Slot.SeamIncoming.Num());
            for (int32 IncomingOffset = 0; IncomingOffset < Destination.IncomingCount; ++IncomingOffset)
            {
                const FDWCGPUSeamIncoming& Incoming = BakedSlot.SeamIncoming[Destination.IncomingStartIndex + IncomingOffset];
                if (Incoming.SourceTexelIndex < 0 || Incoming.SourceTexelIndex >= ExpectedCount || Incoming.Weight <= 0.0f)
                {
                    return false;
                }
                Slot.SeamIncoming.Add(FVector4f(
                    static_cast<float>(Incoming.SourceTexelIndex), Incoming.Weight, 0.0f, 0.0f));
            }

            const uint32 CompactCount = static_cast<uint32>(Slot.SeamIncoming.Num()) - CompactStart;
            if (CompactCount > 0)
            {
                Slot.SeamDestinations.Add(FUint4GPU(
                    static_cast<uint32>(Destination.DestinationTexelIndex), CompactStart, CompactCount, 0u));
            }
        }
    }

    if (SeenMaterialSlots.Num() != ExpectedWettableSlots.Num())
    {
        UE_LOG(
            LogDWCGPU,
            Warning,
            TEXT("DWCGPU: Baked GPU material-slot maps for '%s' do not match the current wettable slots. Baked=[%s], Expected=[%s]. Use Build for Runtime > Build GPU Runtime Data again."),
            *GetNameSafe(Asset),
            *JoinSortedSlotSet(SeenMaterialSlots),
            *JoinSortedSlotSet(ExpectedWettableSlots));
        return false;
    }

    if (Data->Slots.IsEmpty() || Data->TriangleProfileIndices.Contains(MAX_uint32))
    {
        return false;
    }

    StaticSimulationData = Data;
    RenderState = MakeShared<FRenderState, ESPMode::ThreadSafe>();
    if (UE_LOG_ACTIVE(LogDWCGPU, VeryVerbose))
    {
        UE_LOG(
            LogDWCGPU,
            Log,
            TEXT("DWCGPU: Static data ready for asset '%s'. Sections=%d, materialSlots=%d, triangles=%d, profiles=%d, DWCDataUV=%d."),
            *GetNameSafe(Asset),
            Data->Sections.Num(),
            Data->Slots.Num(),
            Data->TriangleCount,
            Data->Profiles.Num(),
            ExpectedDWCDataUVChannel);
        UE_LOG(
            LogDWCGPU,
            Log,
            TEXT("DWCGPU: Profile ranges for '%s'. MaxSpread=%.5f/s, MaxDry=%.5f/s, MaxGravity=%.5f."),
            *GetNameSafe(Asset),
            MaxSpreadRate,
            MaxDryRate,
            MaxGravityFlowStrength);
    }
    return true;
}


bool FDWCGPUBackend::AcquireSharedStaticResources()
{
    SharedStaticSlotResources.Reset();

    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const UDynamicWetClothesComponent* Component = OwnerComponent.Get();
    UWorld* World = Component != nullptr ? Component->GetWorld() : nullptr;
    if (Asset == nullptr || World == nullptr || !StaticSimulationData.IsValid())
    {
        return false;
    }

    UDWCGPUResourceSubsystem* ResourceSubsystem =
        World->GetSubsystem<UDWCGPUResourceSubsystem>();
    if (ResourceSubsystem == nullptr)
    {
        UE_LOG(LogDWCGPU, Warning, TEXT("DWCGPU: Could not acquire the world GPU resource subsystem."));
        return false;
    }

    const FDWCGPULODBakeData& Baked = Asset->GetGPUWetMapRuntimeData(LODIndex);
    if (Baked.MapSignature.IsEmpty())
    {
        UE_LOG(LogDWCGPU, Warning, TEXT("DWCGPU: Asset '%s' has no GPU map build signature."), *GetNameSafe(Asset));
        return false;
    }

    SharedStaticSlotResources.SetNum(StaticSimulationData->Slots.Num());
    for (int32 StaticSlotIndex = 0; StaticSlotIndex < StaticSimulationData->Slots.Num(); ++StaticSlotIndex)
    {
        const FStaticSimulationData::FSlotData& Slot = StaticSimulationData->Slots[StaticSlotIndex];
        TSharedPtr<FDWCGPUStaticSlotResources, ESPMode::ThreadSafe> Shared =
            ResourceSubsystem->AcquireStaticSlotResources(
                Asset,
                Slot.MaterialSlotIndex,
                Baked.MapSignature,
                FIntPoint(Slot.Resolution, Slot.Resolution),
                static_cast<uint32>(Slot.TexelLookup.Num()),
                Slot.SurfaceWaterResolution > 0
                    ? FIntPoint(Slot.SurfaceWaterResolution, Slot.SurfaceWaterResolution)
                    : FIntPoint::ZeroValue,
                static_cast<uint32>(Slot.SurfaceTexelLookup.Num()),
                static_cast<uint32>(StaticSimulationData->TriangleCount),
                StaticSimulationData->Sections.Num());
        if (!Shared.IsValid())
        {
            UE_LOG(
                LogDWCGPU,
                Warning,
                TEXT("DWCGPU: Could not acquire shared static resources for asset '%s' slot %d."),
                *GetNameSafe(Asset),
                Slot.MaterialSlotIndex);
            SharedStaticSlotResources.Reset();
            return false;
        }
        Shared->SeamDestinationCount = static_cast<uint32>(Slot.SeamDestinations.Num());
        Shared->SeamIncomingCount = static_cast<uint32>(Slot.SeamIncoming.Num());
        Shared->Sections.SetNum(StaticSimulationData->Sections.Num());
        for (int32 SectionIndex = 0; SectionIndex < StaticSimulationData->Sections.Num(); ++SectionIndex)
        {
            Shared->Sections[SectionIndex].TriangleCount =
                static_cast<uint32>(StaticSimulationData->Sections[SectionIndex].TriangleIndices.Num());
        }
        SharedStaticSlotResources[StaticSlotIndex] = MoveTemp(Shared);
    }

    return !SharedStaticSlotResources.IsEmpty();
}

bool FDWCGPUBackend::BuildDebugVertexLookup()
{
    DebugVertexDataUVs.Reset();
    DebugVertexMaterialSlots.Reset();

    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return false;
    }

    FDWCDataUVBufferView DataUVView;
    if (!DataUVView.Initialize(
            Asset->GetRuntimeSkeletalMesh(),
            LODIndex,
            Asset->GetDWCDataUVChannelIndex()))
    {
        UE_LOG(LogDWCGPU, Warning, TEXT("DWCGPU: Could not build debug vertex lookup because the DWC Data UV buffer is unavailable."));
        return false;
    }

    const FWetClothingPrecomputedSimulationData& Precomputed = Asset->GetPrecomputedSimulationData();
    const int32 VertexCount = DataUVView.NumVertices();
    if (VertexCount <= 0 || Precomputed.Vertices.Num() != VertexCount)
    {
        UE_LOG(
            LogDWCGPU,
            Warning,
            TEXT("DWCGPU: Debug vertex lookup count mismatch. UV vertices=%d, precomputed vertices=%d."),
            VertexCount,
            Precomputed.Vertices.Num());
        return false;
    }

    DebugVertexDataUVs.SetNumZeroed(VertexCount);
    DebugVertexMaterialSlots.Init(INDEX_NONE, VertexCount);
    for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
    {
        const FWetClothingPrecomputedVertexData& VertexData = Precomputed.Vertices[VertexIndex];
        if (!VertexData.IsWettable())
        {
            continue;
        }

        DebugVertexDataUVs[VertexIndex] = DataUVView.GetUV(VertexIndex);
        DebugVertexMaterialSlots[VertexIndex] = VertexData.MaterialSlotIndex;
    }

    return true;
}

bool FDWCGPUBackend::BindMaterialSlot(FMaterialSlotRuntime& Slot)
{
    USkeletalMeshComponent* MeshComponent = TargetSkeletalMesh.Get();
    if (MeshComponent == nullptr || WetMaterialInstances == nullptr)
    {
        return false;
    }

    if (Slot.MaterialSlotIndex < 0 || Slot.MaterialSlotIndex >= MeshComponent->GetNumMaterials())
    {
        UE_LOG(
            LogDWCGPU,
            Warning,
            TEXT("DWCGPU: Baked material slot %d is out of range for mesh '%s' (%d materials). Use Build for Runtime > Build GPU Runtime Data again for the current runtime mesh."),
            Slot.MaterialSlotIndex,
            *GetNameSafe(MeshComponent),
            MeshComponent->GetNumMaterials());
        return false;
    }

    UMaterialInterface* CurrentMaterial = MeshComponent->GetMaterial(Slot.MaterialSlotIndex);
    UMaterialInstanceDynamic* MID = WetMaterialInstances->IsValidIndex(Slot.MaterialSlotIndex)
        ? (*WetMaterialInstances)[Slot.MaterialSlotIndex]
        : nullptr;
    if (MID == nullptr || CurrentMaterial != MID)
    {
        MID = UMaterialInstanceDynamic::Create(CurrentMaterial, MeshComponent);
        if (MID != nullptr)
        {
            MeshComponent->SetMaterial(Slot.MaterialSlotIndex, MID);
        }
        if (WetMaterialInstances->IsValidIndex(Slot.MaterialSlotIndex))
        {
            (*WetMaterialInstances)[Slot.MaterialSlotIndex] = MID;
        }
    }

    if (MID == nullptr)
    {
        UE_LOG(
            LogDWCGPU,
            Warning,
            TEXT("DWCGPU: Could not create a dynamic material instance for mesh '%s' slot %d."),
            *GetNameSafe(MeshComponent),
            Slot.MaterialSlotIndex);
        return false;
    }

    const bool bHasWetnessMapParameter = MaterialHasTextureParameter(MID, WetnessMapParameterName);
    const bool bHasDroplet1RTParameter = MaterialHasTextureParameter(MID, DWCWetMaterialParameters::SurfaceDroplet1RT());
    const bool bHasDroplet2RTParameter =
        MaterialHasTextureParameter(MID, DWCWetMaterialParameters::SurfaceDroplet2RT());
    const bool bHasWetPartDataParameter = MaterialHasTextureParameter(MID, DWCWetMaterialParameters::WetPartDataTexture());
    const bool bHasProfileRemapParameter = MaterialHasTextureParameter(MID, DWCWetMaterialParameters::ProfileRemapLUT());
    const bool bHasGlobalProfileParameter = MaterialHasTextureParameter(MID, DWCWetMaterialParameters::GlobalRenderProfileLUT());
    const bool bHasGlobalTexelSizeParameter = MaterialHasScalarParameter(MID, DWCWetMaterialParameters::GlobalRenderProfileTexelSize());
    const bool bHasUseGPUBackendParameter = MaterialHasScalarParameter(MID, DWCWetMaterialParameters::UseGPUBackend());

    TArray<FString> MissingParameters;
    if (!bHasWetnessMapParameter) MissingParameters.Add(WetnessMapParameterName.ToString());
    if (Slot.bUsesSurfaceWater && !bHasDroplet1RTParameter)
    {
        MissingParameters.Add(DWCWetMaterialParameters::SurfaceDroplet1RT().ToString());
    }
    if (Slot.bUsesSurfaceWater && !bHasDroplet2RTParameter)
    {
        MissingParameters.Add(DWCWetMaterialParameters::SurfaceDroplet2RT().ToString());
    }
    if (!bHasWetPartDataParameter) MissingParameters.Add(DWCWetMaterialParameters::WetPartDataTexture().ToString());
    if (!bHasProfileRemapParameter) MissingParameters.Add(DWCWetMaterialParameters::ProfileRemapLUT().ToString());
    if (!bHasGlobalProfileParameter) MissingParameters.Add(DWCWetMaterialParameters::GlobalRenderProfileLUT().ToString());
    if (!bHasGlobalTexelSizeParameter) MissingParameters.Add(DWCWetMaterialParameters::GlobalRenderProfileTexelSize().ToString());
    if (!bHasUseGPUBackendParameter) MissingParameters.Add(DWCWetMaterialParameters::UseGPUBackend().ToString());

    if (UE_LOG_ACTIVE(LogDWCGPU, VeryVerbose))
    {
        UE_LOG(
            LogDWCGPU,
            Log,
            TEXT("DWCGPU: Slot %d material binding check. MID='%s', resolution=%d, missingParameters=%d, currentMap='%s'."),
            Slot.MaterialSlotIndex,
            *GetNameSafe(MID),
            Slot.Resolution,
            MissingParameters.Num(),
            *GetNameSafe(Slot.GetCurrentMap()));
    }
    if (!MissingParameters.IsEmpty())
    {
        UE_LOG(
            LogDWCGPU,
            Warning,
            TEXT("DWCGPU: Material '%s' on mesh '%s' slot %d does not satisfy the GPU wetness/profile contract. Missing parameters: %s. Run the three DWC material-function Python scripts, validate the functions, and regenerate the DWC material so Wet Part data and all dynamic RTs use the WCA DWC Data UV channel."),
            *GetNameSafe(MID),
            *GetNameSafe(MeshComponent),
            Slot.MaterialSlotIndex,
            *FString::Join(MissingParameters, TEXT(", ")));
        return false;
    }

    MID->SetTextureParameterValue(WetnessMapParameterName, Slot.GetCurrentMap());
    MID->SetTextureParameterValue(
        DWCWetMaterialParameters::SurfaceDroplet1RT(),
        Slot.bUsesSurfaceWater ? Slot.SurfaceDroplet1RT.Get() : nullptr);
    MID->SetTextureParameterValue(
        DWCWetMaterialParameters::SurfaceDroplet2RT(),
        Slot.bUsesSurfaceWater ? Slot.SurfaceDroplet2RT.Get() : nullptr);
    MID->SetScalarParameterValue(
        DWCWetMaterialParameters::SurfaceWaterTexelSize(),
        Slot.bUsesSurfaceWater && Slot.SurfaceWaterResolution > 0
            ? 1.0f / static_cast<float>(Slot.SurfaceWaterResolution)
            : 0.0f);
    MID->SetScalarParameterValue(DWCWetMaterialParameters::UseGPUBackend(), 1.0f);
    Slot.MaterialInstance = MID;
    UE_LOG(
        LogDWCGPU,
        Log,
        TEXT("DWCGPU: Render binding active. Mesh='%s', slot=%d, MID='%s', wetnessRT='%s', resolution=%d, textureParam='%s'."),
        *GetNameSafe(MeshComponent),
        Slot.MaterialSlotIndex,
        *GetNameSafe(MID),
        *GetNameSafe(Slot.GetCurrentMap()),
        Slot.Resolution,
        *WetnessMapParameterName.ToString());
    if (UE_LOG_ACTIVE(LogDWCGPU, VeryVerbose))
    {
        UE_LOG(
            LogDWCGPU,
            Log,
            TEXT("DWCGPU: Slot %d bound GPU wetness render target '%s' to '%s'."),
            Slot.MaterialSlotIndex,
            *GetNameSafe(Slot.GetCurrentMap()),
            *WetnessMapParameterName.ToString());
    }

    return true;
}

bool FDWCGPUBackend::CreateSlotResources()
{
    USkeletalMeshComponent* MeshComponent = TargetSkeletalMesh.Get();
    UDynamicWetClothesComponent* Component = OwnerComponent.Get();
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (!MeshComponent || !Component || !Asset || !WetMaterialInstances || !StaticSimulationData.IsValid())
    {
        return false;
    }

    if (WetMaterialInstances->Num() < MeshComponent->GetNumMaterials())
    {
        WetMaterialInstances->SetNum(MeshComponent->GetNumMaterials());
    }

    MaterialSlots.Reset();
    bool bAllMaterialBindingsValid = true;
    for (int32 StaticSlotIndex = 0; StaticSlotIndex < StaticSimulationData->Slots.Num(); ++StaticSlotIndex)
    {
        const FStaticSimulationData::FSlotData& StaticSlot = StaticSimulationData->Slots[StaticSlotIndex];
        FMaterialSlotRuntime& Slot = MaterialSlots.AddDefaulted_GetRef();
        Slot.MaterialSlotIndex = StaticSlot.MaterialSlotIndex;
        Slot.StaticSlotIndex = StaticSlotIndex;
        Slot.Resolution = StaticSlot.Resolution;
        Slot.SurfaceWaterResolution = StaticSlot.SurfaceWaterResolution;
        Slot.bUsesSurfaceWater =
            StaticSlot.SurfaceWaterResolution > 0 &&
            !StaticSlot.SurfaceTexelLookup.IsEmpty() &&
            Asset->DoesMaterialSlotUseSurfaceWater(Slot.MaterialSlotIndex);
        Slot.WetnessMaps.Reserve(2);
        Slot.PendingWetnessMaps.Reserve(2);

        for (int32 TextureIndex = 0; TextureIndex < 2; ++TextureIndex)
        {
            UTextureRenderTarget2D* WetnessMap = NewObject<UTextureRenderTarget2D>(Component);
            WetnessMap->RenderTargetFormat = ETextureRenderTargetFormat::RTF_R16f;
            WetnessMap->ClearColor = FLinearColor::Black;
            WetnessMap->bAutoGenerateMips = false;
            WetnessMap->bCanCreateUAV = true;
            WetnessMap->Filter = TF_Nearest;
            WetnessMap->AddressX = TA_Clamp;
            WetnessMap->AddressY = TA_Clamp;

            WetnessMap->InitCustomFormat(Slot.Resolution, Slot.Resolution, PF_R16F, false);
            WetnessMap->UpdateResourceImmediate(true);
            Slot.WetnessMaps.Add(TStrongObjectPtr<UTextureRenderTarget2D>(WetnessMap));

            UTextureRenderTarget2D* PendingWetnessMap = NewObject<UTextureRenderTarget2D>(
                Component,
                FName(*FString::Printf(
                    TEXT("DWC_PendingWetnessMap_Slot%d_%d"),
                    Slot.MaterialSlotIndex,
                    TextureIndex)));
            PendingWetnessMap->RenderTargetFormat = ETextureRenderTargetFormat::RTF_R16f;
            PendingWetnessMap->ClearColor = FLinearColor::Black;
            PendingWetnessMap->bAutoGenerateMips = false;
            PendingWetnessMap->bCanCreateUAV = true;
            PendingWetnessMap->Filter = TF_Nearest;
            PendingWetnessMap->AddressX = TA_Clamp;
            PendingWetnessMap->AddressY = TA_Clamp;
            PendingWetnessMap->InitCustomFormat(Slot.Resolution, Slot.Resolution, PF_R16F, false);
            PendingWetnessMap->UpdateResourceImmediate(true);
            Slot.PendingWetnessMaps.Add(
                TStrongObjectPtr<UTextureRenderTarget2D>(PendingWetnessMap));
        }

        if (Slot.bUsesSurfaceWater)
        {
            auto CreateSurfaceRenderTarget = [Component, &Slot](const FName DebugName)
            {
                UTextureRenderTarget2D* SurfaceRT = NewObject<UTextureRenderTarget2D>(Component, DebugName);
                SurfaceRT->RenderTargetFormat = ETextureRenderTargetFormat::RTF_R16f;
                SurfaceRT->ClearColor = FLinearColor::Black;
                SurfaceRT->bAutoGenerateMips = false;
                SurfaceRT->bCanCreateUAV = true;
                SurfaceRT->Filter = TF_Bilinear;
                SurfaceRT->AddressX = TA_Clamp;
                SurfaceRT->AddressY = TA_Clamp;
                SurfaceRT->InitCustomFormat(
                    Slot.SurfaceWaterResolution,
                    Slot.SurfaceWaterResolution,
                    PF_R16F,
                    false);
                SurfaceRT->UpdateResourceImmediate(true);
                return TStrongObjectPtr<UTextureRenderTarget2D>(SurfaceRT);
            };
            Slot.SurfaceDroplet1RT = CreateSurfaceRenderTarget(
                FName(*FString::Printf(TEXT("DWC_SurfaceDroplet1RT_Slot%d"), Slot.MaterialSlotIndex)));
            Slot.SurfaceDroplet2RT = CreateSurfaceRenderTarget(
                FName(*FString::Printf(TEXT("DWC_SurfaceDroplet2RT_Slot%d"), Slot.MaterialSlotIndex)));
        }

        if (!BindMaterialSlot(Slot))
        {
            bAllMaterialBindingsValid = false;
        }
    }

    return !MaterialSlots.IsEmpty() && bAllMaterialBindingsValid;
}

bool FDWCGPUBackend::EnqueueResolvedContacts(const TArray<FDWCResolvedSurfaceContact>& Contacts)
{
    if (!bInitialized || Contacts.IsEmpty())
    {
        if (UE_LOG_ACTIVE(LogDWCGPU, VeryVerbose))
        {
            UE_LOG(
                LogDWCGPU,
                Warning,
                TEXT("DWCGPU: EnqueueResolvedContacts ignored. initialized=%s, contacts=%d."),
                bInitialized ? TEXT("true") : TEXT("false"),
                Contacts.Num());
        }
        return false;
    }
    PendingContacts.Append(Contacts);
    if (UE_LOG_ACTIVE(LogDWCGPU, VeryVerbose))
    {
        UE_LOG(
            LogDWCGPU,
            Log,
            TEXT("DWCGPU: Queued resolved contacts. Added=%d, pending=%d."),
            Contacts.Num(),
            PendingContacts.Num());
    }
    return true;
}

bool FDWCGPUBackend::EnqueueSurfaceStamps(const TArray<FDWCSurfaceStampRequest>& Stamps)
{
    if (!bInitialized || Stamps.IsEmpty())
    {
        return false;
    }

    int32 AddedCount = 0;
    for (const FDWCSurfaceStampRequest& Stamp : Stamps)
    {
        if (Stamp.MaterialSlotIndex == INDEX_NONE ||
            Stamp.UV.ContainsNaN() ||
            !FMath::IsFinite(Stamp.UV.X) ||
            !FMath::IsFinite(Stamp.UV.Y) ||
            Stamp.Amount <= 0.0f)
        {
            continue;
        }

        PendingSurfaceStamps.Add(Stamp);
        ++AddedCount;
    }
    return AddedCount > 0;
}

bool FDWCGPUBackend::ApplyWetAll(const float Amount)
{
    if (!bInitialized || FMath::IsNearlyZero(Amount))
    {
        if (UE_LOG_ACTIVE(LogDWCGPU, VeryVerbose))
        {
            UE_LOG(
                LogDWCGPU,
                Warning,
                TEXT("DWCGPU: ApplyWetAll ignored. initialized=%s, amount=%.4f."),
                bInitialized ? TEXT("true") : TEXT("false"),
                Amount);
        }
        return false;
    }
    PendingWetAllAmount += Amount;
    if (UE_LOG_ACTIVE(LogDWCGPU, VeryVerbose))
    {
        UE_LOG(
            LogDWCGPU,
            Log,
            TEXT("DWCGPU: Queued WetAll. Added=%.4f, pending=%.4f."),
            Amount,
            PendingWetAllAmount);
    }
    return true;
}

void FDWCGPUBackend::ClearPendingWetnessMaps()
{
    if (!bInitialized)
    {
        return;
    }

    PendingContacts.Reset();
    PendingWetAllAmount = 0.0f;

    TArray<FTextureRenderTargetResource*> PendingResources;
    PendingResources.Reserve(MaterialSlots.Num() * 2);
    for (FMaterialSlotRuntime& Slot : MaterialSlots)
    {
        for (const TStrongObjectPtr<UTextureRenderTarget2D>& PendingMap : Slot.PendingWetnessMaps)
        {
            if (UTextureRenderTarget2D* RenderTarget = PendingMap.Get())
            {
                if (FTextureRenderTargetResource* Resource = RenderTarget->GameThread_GetRenderTargetResource())
                {
                    PendingResources.Add(Resource);
                }
            }
        }
    }

    if (PendingResources.IsEmpty())
    {
        return;
    }

    ENQUEUE_RENDER_COMMAND(DWCClearPendingWetnessMaps)(
        [PendingResources = MoveTemp(PendingResources)](FRHICommandListImmediate& RHICmdList) mutable
        {
            FRDGBuilder GraphBuilder(RHICmdList);
            for (FTextureRenderTargetResource* Resource : PendingResources)
            {
                if (Resource == nullptr || Resource->GetRenderTargetTexture() == nullptr)
                {
                    continue;
                }

                TRefCountPtr<IPooledRenderTarget> External = CreateRenderTarget(
                    Resource->GetRenderTargetTexture(),
                    TEXT("DWC.ClearPendingWetnessMap"));
                FRDGTextureRef Texture = GraphBuilder.RegisterExternalTexture(External);
                AddClearUAVPass(
                    GraphBuilder,
                    GraphBuilder.CreateUAV(Texture),
                    FLinearColor::Black);
            }
            GraphBuilder.Execute();
        });
}

void FDWCGPUBackend::Update(const float DeltaSeconds)
{
    if (!bInitialized)
    {
        return;
    }

    TArray<FDWCResolvedSurfaceContact> Contacts;
    Swap(Contacts, PendingContacts);
    TArray<FDWCSurfaceStampRequest> SurfaceStamps;
    Swap(SurfaceStamps, PendingSurfaceStamps);
    const float WetAllAmount = PendingWetAllAmount;
    PendingWetAllAmount = 0.0f;
    if (UE_LOG_ACTIVE(LogDWCGPU, VeryVerbose) &&
        (!Contacts.IsEmpty() || !SurfaceStamps.IsEmpty() || !FMath::IsNearlyZero(WetAllAmount) || DebugDispatchLogCount < 3))
    {
        UE_LOG(
            LogDWCGPU,
            Log,
            TEXT("DWCGPU: Update dispatch. contacts=%d, surfaceStamps=%d, wetAll=%.4f, delta=%.4f."),
            Contacts.Num(),
            SurfaceStamps.Num(),
            WetAllAmount,
            DeltaSeconds);
        ++DebugDispatchLogCount;
    }
    DispatchSimulation(
        MoveTemp(Contacts),
        MoveTemp(SurfaceStamps),
        WetAllAmount,
        FMath::Clamp(DeltaSeconds, 0.0f, 0.25f));
}

void FDWCGPUBackend::DispatchSimulation(
    TArray<FDWCResolvedSurfaceContact>&& Contacts,
    TArray<FDWCSurfaceStampRequest>&& SurfaceStamps,
    const float WetAllAmount,
    const float DeltaSeconds)
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    USkeletalMeshComponent* MeshComponent = TargetSkeletalMesh.Get();
    if (!Asset || !MeshComponent || !StaticSimulationData.IsValid() || !RenderState.IsValid())
    {
        return;
    }

    const FDWCGPULODBakeData& BakedData = Asset->GetGPUWetMapRuntimeData(LODIndex);
    TArray<FSlotRenderDispatch> SlotDispatches;
    SlotDispatches.Reserve(MaterialSlots.Num());
    const bool bHadWetInput = !Contacts.IsEmpty() || !FMath::IsNearlyZero(WetAllAmount);
    const bool bHadSurfaceInput = !SurfaceStamps.IsEmpty();
    int32 TotalAbsorptionDispatches = 0;
    int32 TotalBinnedAbsorptionContacts = 0;
    int32 TotalSurfaceStampDispatches = 0;
    const UDynamicWetClothesComponent* Component = OwnerComponent.Get();
    const float DryRateScaleValue = Component != nullptr
                                        ? FMath::Max(0.0f, Component->WetnessSettings.DryRateScale)
                                        : FMath::Max(0.0f, DryRateScale);

    for (int32 SlotRuntimeIndex = 0; SlotRuntimeIndex < MaterialSlots.Num(); ++SlotRuntimeIndex)
    {
        FMaterialSlotRuntime& Slot = MaterialSlots[SlotRuntimeIndex];
        UTextureRenderTarget2D* CurrentMap = Slot.GetCurrentMap();
        UTextureRenderTarget2D* NextMap = Slot.GetNextMap();
        UTextureRenderTarget2D* CurrentPendingMap = Slot.GetCurrentPendingMap();
        UTextureRenderTarget2D* NextPendingMap = Slot.GetNextPendingMap();
        if (!CurrentMap || !NextMap || !CurrentPendingMap || !NextPendingMap)
        {
            continue;
        }
        if (!StaticSimulationData->Slots.IsValidIndex(Slot.StaticSlotIndex))
        {
            continue;
        }
        const FStaticSimulationData::FSlotData& StaticSlot = StaticSimulationData->Slots[Slot.StaticSlotIndex];

        FSlotRenderDispatch& SlotDispatch = SlotDispatches.AddDefaulted_GetRef();
        SlotDispatch.SlotRuntimeIndex = SlotRuntimeIndex;
        SlotDispatch.StaticSlotIndex = Slot.StaticSlotIndex;
        SlotDispatch.Resolution = Slot.Resolution;
        SlotDispatch.SurfaceWaterResolution = Slot.bUsesSurfaceWater
            ? Slot.SurfaceWaterResolution
            : 0;
        SlotDispatch.CurrentResource = CurrentMap->GameThread_GetRenderTargetResource();
        SlotDispatch.NextResource = NextMap->GameThread_GetRenderTargetResource();
        SlotDispatch.CurrentPendingResource =
            CurrentPendingMap->GameThread_GetRenderTargetResource();
        SlotDispatch.NextPendingResource =
            NextPendingMap->GameThread_GetRenderTargetResource();
        SlotDispatch.SurfaceDroplet1Resource = Slot.SurfaceDroplet1RT.IsValid()
            ? Slot.SurfaceDroplet1RT->GameThread_GetRenderTargetResource()
            : nullptr;
        SlotDispatch.SurfaceDroplet2Resource = Slot.SurfaceDroplet2RT.IsValid()
            ? Slot.SurfaceDroplet2RT->GameThread_GetRenderTargetResource()
            : nullptr;

        for (const FDWCSurfaceStampRequest& Request : SurfaceStamps)
        {
            if (Request.MaterialSlotIndex != Slot.MaterialSlotIndex)
            {
                continue;
            }
            if (!Slot.bUsesSurfaceWater)
            {
                continue;
            }

            FSurfaceStampDispatch StampDispatch;
            if (BuildSurfaceStampDispatch(Request, Slot.SurfaceWaterResolution, StampDispatch))
            {
                SlotDispatch.SurfaceStampDispatches.Add(StampDispatch);
            }
        }

        for (const FDWCResolvedSurfaceContact& Contact : Contacts)
        {
            if (Contact.MaterialSlotIndex != Slot.MaterialSlotIndex)
            {
                continue;
            }

            if (!BakedData.Triangles.IsValidIndex(Contact.TriangleID))
            {
                continue;
            }

            const FDWCGPUBakedTriangle& Triangle = BakedData.Triangles[Contact.TriangleID];
            FTriangleAbsorptionDispatch Dispatch;
            const bool bUseUVStamp = IsSampledWetAreaContact(Contact);
            const bool bBuiltDispatch = bUseUVStamp
                ? BuildAbsorptionStampDispatch(
                      Contact,
                      Triangle,
                      CVarDWCGPUWetAreaStampRadiusPixels.GetValueOnAnyThread(),
                      Slot.Resolution,
                      Dispatch)
                : BuildDispatchBounds(Triangle, Slot.Resolution, Dispatch.DispatchMin, Dispatch.DispatchSize);
            if (!bBuiltDispatch)
            {
                continue;
            }

            if (!bUseUVStamp)
            {
                FillTriangleUVs(Triangle, Dispatch);
                Dispatch.UV2AndSettings.Z = FMath::Max(0.0f, Contact.DistanceToSurface);
            }
            Dispatch.ContactAndRadius = MakePositionAndValue(Contact.ContactWorldPosition, FMath::Max(Contact.Radius, KINDA_SMALL_NUMBER));
            const float AppliedAmount = Contact.Amount > 0.0f ? Contact.Amount * Contact.AbsorptionMultiplier : Contact.Amount;
            Dispatch.P0AndAmount = MakePositionAndValue(Contact.WorldTrianglePosition0, AppliedAmount);
            Dispatch.P1AndMaxWetness = MakePositionAndValue(Contact.WorldTrianglePosition1, MaxWetness);
            Dispatch.P2AndMode = MakePositionAndValue(Contact.WorldTrianglePosition2, bUseUVStamp ? 2.0f : 0.0f);
            SlotDispatch.AbsorptionDispatches.Add(Dispatch);
        }

        if (!FMath::IsNearlyZero(WetAllAmount))
        {
            for (const FDWCGPUBakedTriangle& Triangle : BakedData.Triangles)
            {
                if (Triangle.TriangleID == INDEX_NONE || Triangle.MaterialSlotIndex != Slot.MaterialSlotIndex)
                {
                    continue;
                }
                FTriangleAbsorptionDispatch Dispatch;
                if (BuildDispatchBounds(Triangle, Slot.Resolution, Dispatch.DispatchMin, Dispatch.DispatchSize))
                {
                    FillTriangleUVs(Triangle, Dispatch);
                    Dispatch.P0AndAmount.W = WetAllAmount;
                    Dispatch.P1AndMaxWetness.W = MaxWetness;
                    Dispatch.P2AndMode.W = 1.0f;
                    SlotDispatch.AbsorptionDispatches.Add(Dispatch);
                }
            }
        }
        BuildBinnedAbsorptionDispatches(SlotDispatch);
        TotalAbsorptionDispatches += SlotDispatch.AbsorptionDispatches.Num();
        TotalBinnedAbsorptionContacts += SlotDispatch.BinnedAbsorptionContacts.Num() / DWCAbsorptionContactFloat4Count;
        TotalSurfaceStampDispatches += SlotDispatch.SurfaceStampDispatches.Num();
    }

    if (SlotDispatches.IsEmpty())
    {
        if (UE_LOG_ACTIVE(LogDWCGPU, VeryVerbose) && (bHadWetInput || bHadSurfaceInput))
        {
            UE_LOG(
                LogDWCGPU,
                Warning,
                TEXT("DWCGPU: No slot dispatches were built. contacts=%d, surfaceStamps=%d, wetAll=%.4f, materialSlots=%d, asset='%s', mesh='%s'."),
                Contacts.Num(),
                SurfaceStamps.Num(),
                WetAllAmount,
                MaterialSlots.Num(),
                *GetNameSafe(Asset),
                *GetNameSafe(MeshComponent));
        }
        return;
    }

    if (bHadWetInput && TotalAbsorptionDispatches <= 0 && TotalBinnedAbsorptionContacts <= 0)
    {
        UE_LOG(
            LogDWCGPU,
            Warning,
            TEXT("DWCGPU: Wet input reached GPU backend, but no absorption dispatches were built. contacts=%d, wetAll=%.4f, slots=%d, asset='%s', mesh='%s'. Check contact material slots, triangle IDs, DWC Data UV bake, and wettable slot setup."),
            Contacts.Num(),
            WetAllAmount,
            SlotDispatches.Num(),
            *GetNameSafe(Asset),
            *GetNameSafe(MeshComponent));
    }


    if (bHadSurfaceInput && TotalSurfaceStampDispatches <= 0)
    {
        UE_LOG(
            LogDWCGPU,
            Warning,
            TEXT("DWCGPU: Surface input reached GPU backend, but no surface stamp dispatches were built. stamps=%d, slots=%d, asset='%s', mesh='%s'. Check stamp material-slot routing, Data UV lookup data, and surface RT initialization."),
            SurfaceStamps.Num(),
            SlotDispatches.Num(),
            *GetNameSafe(Asset),
            *GetNameSafe(MeshComponent));
    }

    if (UE_LOG_ACTIVE(LogDWCGPU, VeryVerbose) && (bHadWetInput || bHadSurfaceInput || DebugDispatchLogCount <= 3))
    {
        TArray<FString> SlotSummaries;
        SlotSummaries.Reserve(SlotDispatches.Num());
        for (const FSlotRenderDispatch& SlotDispatch : SlotDispatches)
        {
            const int32 MaterialSlotIndex = MaterialSlots.IsValidIndex(SlotDispatch.SlotRuntimeIndex)
                ? MaterialSlots[SlotDispatch.SlotRuntimeIndex].MaterialSlotIndex
                : INDEX_NONE;
            SlotSummaries.Add(FString::Printf(
                TEXT("slot%d:absorb%d:binned%d:bins%d:surface%d:res%d"),
                MaterialSlotIndex,
                SlotDispatch.AbsorptionDispatches.Num(),
                SlotDispatch.BinnedAbsorptionContacts.Num() / DWCAbsorptionContactFloat4Count,
                SlotDispatch.BinnedAbsorptionTileContactIndices.Num(),
                SlotDispatch.SurfaceStampDispatches.Num(),
                SlotDispatch.Resolution));
        }

        UE_LOG(
            LogDWCGPU,
            Log,
            TEXT("DWCGPU: Built slot dispatches. contacts=%d, surfaceStamps=%d, wetAll=%.4f, slots=%d, absorptionDispatches=%d, binnedAbsorptionContacts=%d, surfaceDispatches=%d, [%s]."),
            Contacts.Num(),
            SurfaceStamps.Num(),
            WetAllAmount,
            SlotDispatches.Num(),
            TotalAbsorptionDispatches,
            TotalBinnedAbsorptionContacts,
            TotalSurfaceStampDispatches,
            *FString::Join(SlotSummaries, TEXT(", ")));
    }

    const FSkeletalMeshObject* MeshObject = MeshComponent->GetMeshObject();
    FVector GravityDirection(0.0, 0.0, -1.0);
    if (const UWorld* World = OwnerComponent.IsValid() ? OwnerComponent->GetWorld() : nullptr)
    {
        GravityDirection = FVector(0.0, 0.0, World->GetGravityZ()).GetSafeNormal();
        if (GravityDirection.IsNearlyZero())
        {
            GravityDirection = FVector::DownVector;
        }
    }
    const FVector4f WorldGravityDirection(
        static_cast<float>(GravityDirection.X),
        static_cast<float>(GravityDirection.Y),
        static_cast<float>(GravityDirection.Z),
        0.0f);
    const FBox ReceiverWorldBounds = MeshComponent->Bounds.GetBox();
    const FVector3f ReceiverBoundsMinValue(ReceiverWorldBounds.Min);
    const FVector3f ReceiverBoundsMaxValue(ReceiverWorldBounds.Max);
    const FMatrix44f ReceiverLocalToWorldValue(MeshComponent->GetComponentTransform().ToMatrixWithScale());

    const TSharedPtr<const FStaticSimulationData, ESPMode::ThreadSafe> StaticData = StaticSimulationData;
    const TSharedPtr<FRenderState, ESPMode::ThreadSafe> RTState = RenderState;
    const TArray<TSharedPtr<FDWCGPUStaticSlotResources, ESPMode::ThreadSafe>> SharedStaticResources =
        SharedStaticSlotResources;
    const float MaxWetnessValue = MaxWetness;
    const float CapillaryImmediateAbsorptionFractionValue =
        CapillaryImmediateAbsorptionFraction;
    const int32 SimulationLODIndex = LODIndex;

    // The game-thread-facing material switches to the destination map immediately.
    // The render command is queued before the next scene render and writes that resource in order.
    for (const FSlotRenderDispatch& SlotDispatch : SlotDispatches)
    {
        if (!MaterialSlots.IsValidIndex(SlotDispatch.SlotRuntimeIndex))
        {
            continue;
        }
        FMaterialSlotRuntime& Slot = MaterialSlots[SlotDispatch.SlotRuntimeIndex];
        Slot.SwapMaps();
        Slot.SwapPendingMaps();
        if (MeshComponent->GetMaterial(Slot.MaterialSlotIndex) != Slot.MaterialInstance.Get() &&
            !BindMaterialSlot(Slot))
        {
            continue;
        }
        if (UMaterialInstanceDynamic* MID = Slot.MaterialInstance.Get())
        {
            MID->SetTextureParameterValue(WetnessMapParameterName, Slot.GetCurrentMap());
            if (UE_LOG_ACTIVE(LogDWCGPU, VeryVerbose) && (bHadWetInput || bHadSurfaceInput || DebugDispatchLogCount <= 3))
            {
                UE_LOG(
                    LogDWCGPU,
                    Log,
                    TEXT("DWCGPU: Swapped slot %d to render target '%s' on MID '%s'."),
                    Slot.MaterialSlotIndex,
                    *GetNameSafe(Slot.GetCurrentMap()),
                    *GetNameSafe(MID));
            }
        }
        else if (UE_LOG_ACTIVE(LogDWCGPU, VeryVerbose))
        {
            UE_LOG(
                LogDWCGPU,
                Warning,
                TEXT("DWCGPU: Slot %d has no live MID when swapping wetness map."),
                Slot.MaterialSlotIndex);
        }
    }

    FDWCWorkloadStats::RecordGPUBackendUpdateSubmitted();
    ENQUEUE_RENDER_COMMAND(DWCFullWetMapSimulation)(
        [MeshObject, StaticData, RTState, SharedStaticResources, SlotDispatches = MoveTemp(SlotDispatches), DeltaSeconds, MaxWetnessValue, DryRateScaleValue, CapillaryImmediateAbsorptionFractionValue, WorldGravityDirection, ReceiverBoundsMinValue, ReceiverBoundsMaxValue, ReceiverLocalToWorldValue, SimulationLODIndex, ReceiverGPUIdValue = ReceiverGPUId, bUseEightDirectionDiffusion = bUseEightDirectionDiffusion](FRHICommandListImmediate& RHICmdList) mutable
        {
            if (!StaticData.IsValid() || !RTState.IsValid())
            {
                return;
            }

            FRDGBuilder GraphBuilder(RHICmdList);

            if (SharedStaticResources.IsEmpty() || !SharedStaticResources[0].IsValid())
            {
                return;
            }
            FRDGBufferRef ProfileBuffer = RegisterOrUploadStructuredBuffer(
                GraphBuilder,
                RTState->Profiles,
                TEXT("DWC.InstanceProfiles"),
                StaticData->Profiles);
            FRDGBufferRef TriangleProfileIndexBuffer = RegisterOrUploadStructuredBuffer(
                GraphBuilder,
                RTState->TriangleProfileIndices,
                TEXT("DWC.InstanceTriangleProfileIndices"),
                StaticData->TriangleProfileIndices);
            FRDGBufferRef TriangleDataToNormalUVBuffer = RegisterOrUploadStructuredBuffer(
                GraphBuilder,
                SharedStaticResources[0]->TriangleDataToSurfaceWaterNormalUV,
                TEXT("DWC.SharedTriangleDataToSurfaceWaterNormalUV"),
                StaticData->TriangleDataToSurfaceWaterNormalUV);
            FRDGBufferRef NiagaraTriangleUV01Buffer = RegisterOrUploadStructuredBuffer(
                GraphBuilder,
                RTState->NiagaraTriangleUV01,
                TEXT("DWC.NiagaraTriangleUV01"),
                StaticData->TriangleUV01);
            FRDGBufferRef NiagaraTriangleUV2AndDropletBuffer = RegisterOrUploadStructuredBuffer(
                GraphBuilder,
                RTState->NiagaraTriangleUV2AndDroplet,
                TEXT("DWC.NiagaraTriangleUV2AndDroplet"),
                StaticData->TriangleUV2AndDroplet);
            FRDGBufferRef NiagaraTriangleFlowDropletSettingsBuffer = RegisterOrUploadStructuredBuffer(
                GraphBuilder,
                RTState->NiagaraTriangleFlowDropletSettings,
                TEXT("DWC.NiagaraTriangleFlowDropletSettings"),
                StaticData->TriangleFlowDropletSettings);
            FRDGBufferRef NiagaraTriangleFlowSpawnPositionSpreadBuffer = RegisterOrUploadStructuredBuffer(
                GraphBuilder,
                RTState->NiagaraTriangleFlowSpawnPositionSpread,
                TEXT("DWC.NiagaraTriangleFlowSpawnPositionSpread"),
                StaticData->TriangleFlowSpawnPositionSpread);
            FRDGBufferRef NiagaraTriangleSurfaceMetadataBuffer = RegisterOrUploadStructuredBuffer(
                GraphBuilder,
                RTState->NiagaraTriangleSurfaceMetadata,
                TEXT("DWC.NiagaraTriangleSurfaceMetadata"),
                StaticData->TriangleSurfaceMetadata);
            if (!ProfileBuffer ||
                !TriangleProfileIndexBuffer ||
                !TriangleDataToNormalUVBuffer ||
                !NiagaraTriangleUV01Buffer ||
                !NiagaraTriangleUV2AndDropletBuffer ||
                !NiagaraTriangleFlowDropletSettingsBuffer ||
                !NiagaraTriangleFlowSpawnPositionSpreadBuffer ||
                !NiagaraTriangleSurfaceMetadataBuffer)
            {
                return;
            }
            FRDGBufferSRVRef ProfileSRV = GraphBuilder.CreateSRV(ProfileBuffer);
            FRDGBufferSRVRef TriangleProfileIndexSRV = GraphBuilder.CreateSRV(TriangleProfileIndexBuffer);
            FRDGBufferSRVRef TriangleDataToNormalUVSRV =
                GraphBuilder.CreateSRV(TriangleDataToNormalUVBuffer);
            FRDGBufferSRVRef NiagaraTriangleUV01SRV =
                GraphBuilder.CreateSRV(NiagaraTriangleUV01Buffer);
            FRDGBufferSRVRef NiagaraTriangleUV2AndDropletSRV =
                GraphBuilder.CreateSRV(NiagaraTriangleUV2AndDropletBuffer);
            FRDGBufferSRVRef NiagaraTriangleFlowDropletSettingsSRV =
                GraphBuilder.CreateSRV(NiagaraTriangleFlowDropletSettingsBuffer);
            FRDGBufferSRVRef NiagaraTriangleFlowSpawnPositionSpreadSRV =
                GraphBuilder.CreateSRV(NiagaraTriangleFlowSpawnPositionSpreadBuffer);
            FRDGBufferSRVRef NiagaraTriangleSurfaceMetadataSRV =
                GraphBuilder.CreateSRV(NiagaraTriangleSurfaceMetadataBuffer);

            FRDGBufferRef TriangleFlowBuffer = nullptr;
            if (RTState->TriangleFlow.IsValid())
            {
                TriangleFlowBuffer = GraphBuilder.RegisterExternalBuffer(RTState->TriangleFlow, TEXT("DWC.TriangleFlow"));
            }
            else
            {
                TArray<FVector4f> DefaultFlow;
                DefaultFlow.Init(FVector4f(0, 0, 1, 1), StaticData->TriangleCount);
                TriangleFlowBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("DWC.TriangleFlow"), DefaultFlow);
                GraphBuilder.QueueBufferExtraction(TriangleFlowBuffer, &RTState->TriangleFlow);
            }

            FRDGBufferRef TriangleMetricBuffer = nullptr;
            if (RTState->TriangleMetric.IsValid())
            {
                TriangleMetricBuffer = GraphBuilder.RegisterExternalBuffer(RTState->TriangleMetric, TEXT("DWC.TriangleMetric"));
            }
            else
            {
                TArray<FVector4f> DefaultMetric;
                DefaultMetric.Init(FVector4f(1, 0, 1, 1), StaticData->TriangleCount);
                TriangleMetricBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("DWC.TriangleMetric"), DefaultMetric);
                GraphBuilder.QueueBufferExtraction(TriangleMetricBuffer, &RTState->TriangleMetric);
            }

            FRDGBufferRef TrianglePositionsBuffer = nullptr;
            if (RTState->TrianglePositions.IsValid())
            {
                TrianglePositionsBuffer = GraphBuilder.RegisterExternalBuffer(RTState->TrianglePositions, TEXT("DWC.TrianglePositions"));
            }
            else
            {
                TArray<FVector4f> DefaultPositions;
                DefaultPositions.Init(FVector4f::Zero(), FMath::Max(StaticData->TriangleCount * 3, 1));
                TrianglePositionsBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("DWC.TrianglePositions"), DefaultPositions);
                GraphBuilder.QueueBufferExtraction(TrianglePositionsBuffer, &RTState->TrianglePositions);
            }

            FCachedGeometry CachedGeometry;
            const bool bHasCachedGeometry =
                MeshObject &&
                MeshObject->GetCachedGeometry(GraphBuilder, CachedGeometry) &&
                CachedGeometry.LODIndex == SimulationLODIndex;
            if (bHasCachedGeometry)
            {
                FRDGBufferUAVRef FlowUAV = GraphBuilder.CreateUAV(TriangleFlowBuffer);
                FRDGBufferUAVRef MetricUAV = GraphBuilder.CreateUAV(TriangleMetricBuffer);
                FRDGBufferUAVRef PositionsUAV = GraphBuilder.CreateUAV(TrianglePositionsBuffer);
                for (const FCachedGeometry::Section& CachedSection : CachedGeometry.Sections)
                {
                    const int32 SectionIndex = static_cast<int32>(CachedSection.SectionIndex);
                    if (CachedSection.LODIndex != SimulationLODIndex || !StaticData->Sections.IsValidIndex(SectionIndex) ||
                        !CachedSection.PositionBuffer)
                    {
                        continue;
                    }

                    const FStaticSimulationData::FSectionData& SectionData = StaticData->Sections[SectionIndex];
                    if (SectionData.TriangleIndices.IsEmpty())
                    {
                        continue;
                    }

                    if (!SharedStaticResources[0]->Sections.IsValidIndex(SectionIndex))
                    {
                        continue;
                    }
                    FDWCGPUStaticSectionResources& SectionBuffers =
                        SharedStaticResources[0]->Sections[SectionIndex];
                    FRDGBufferRef IndicesBuffer = RegisterOrUploadStructuredBuffer(
                        GraphBuilder, SectionBuffers.TriangleIndices, TEXT("DWC.SharedFlowTriangleIndices"), SectionData.TriangleIndices);
                    FRDGBufferRef UV01Buffer = RegisterOrUploadStructuredBuffer(
                        GraphBuilder, SectionBuffers.TriangleUV01, TEXT("DWC.SharedFlowTriangleUV01"), SectionData.TriangleUV01);
                    FRDGBufferRef UV2Buffer = RegisterOrUploadStructuredBuffer(
                        GraphBuilder, SectionBuffers.TriangleUV2RestArea, TEXT("DWC.SharedFlowTriangleUV2Rest"), SectionData.TriangleUV2RestArea);
                    if (!IndicesBuffer || !UV01Buffer || !UV2Buffer)
                    {
                        continue;
                    }

                    FDWCUpdateTriangleFlowCS::FParameters* Parameters =
                        GraphBuilder.AllocParameters<FDWCUpdateTriangleFlowCS::FParameters>();
                    Parameters->TriangleCount = static_cast<uint32>(SectionData.TriangleIndices.Num());
                    Parameters->PositionIndexBase =
                        (CachedSection.TotalVertexCount == CachedSection.NumVertices && CachedSection.VertexBaseIndex > 0)
                            ? CachedSection.VertexBaseIndex
                            : 0u;
                    Parameters->LocalToWorld = ReceiverLocalToWorldValue;
                    Parameters->WorldGravityDirection = WorldGravityDirection;
                    Parameters->PositionBuffer = CachedSection.PositionBuffer;
                    Parameters->TriangleIndices = GraphBuilder.CreateSRV(IndicesBuffer);
                    Parameters->TriangleUV01 = GraphBuilder.CreateSRV(UV01Buffer);
                    Parameters->TriangleUV2RestArea = GraphBuilder.CreateSRV(UV2Buffer);
                    Parameters->TriangleFlow = FlowUAV;
                    Parameters->TriangleMetric = MetricUAV;
                    Parameters->TrianglePositions = PositionsUAV;

                    TShaderMapRef<FDWCUpdateTriangleFlowCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
                    FDWCWorkloadStats::RecordGPUBackendDispatch();
                    RDG_EVENT_SCOPE_STAT(GraphBuilder, DWC_UpdateTriangleFlow, "DWC UpdateTriangleFlow");
                    FComputeShaderUtils::AddPass(
                        GraphBuilder,
                        RDG_EVENT_NAME("DWC Update Triangle Flow Section %d", SectionIndex),
                        Shader,
                        Parameters,
                        FIntVector(FMath::DivideAndRoundUp(SectionData.TriangleIndices.Num(), 64), 1, 1));
                }
            }
            else
            {
                FRDGBufferUAVRef FlowUAV = GraphBuilder.CreateUAV(TriangleFlowBuffer);
                FRDGBufferUAVRef MetricUAV = GraphBuilder.CreateUAV(TriangleMetricBuffer);
                FRDGBufferUAVRef PositionsUAV = GraphBuilder.CreateUAV(TrianglePositionsBuffer);
                if (RTState->RestPositionBuffers.Num() < StaticData->Sections.Num())
                {
                    RTState->RestPositionBuffers.SetNum(StaticData->Sections.Num());
                }

                for (int32 SectionIndex = 0; SectionIndex < StaticData->Sections.Num(); ++SectionIndex)
                {
                    const FStaticSimulationData::FSectionData& SectionData = StaticData->Sections[SectionIndex];
                    if (SectionData.TriangleIndices.IsEmpty() || SectionData.RestPositions.IsEmpty())
                    {
                        continue;
                    }

                    if (!SharedStaticResources[0]->Sections.IsValidIndex(SectionIndex))
                    {
                        continue;
                    }

                    FDWCGPUStaticSectionResources& SectionBuffers =
                        SharedStaticResources[0]->Sections[SectionIndex];
                    FRDGBufferRef IndicesBuffer = RegisterOrUploadStructuredBuffer(
                        GraphBuilder, SectionBuffers.TriangleIndices, TEXT("DWC.SharedRestTriangleIndices"), SectionData.TriangleIndices);
                    FRDGBufferRef UV01Buffer = RegisterOrUploadStructuredBuffer(
                        GraphBuilder, SectionBuffers.TriangleUV01, TEXT("DWC.SharedRestTriangleUV01"), SectionData.TriangleUV01);
                    FRDGBufferRef UV2Buffer = RegisterOrUploadStructuredBuffer(
                        GraphBuilder, SectionBuffers.TriangleUV2RestArea, TEXT("DWC.SharedRestTriangleUV2Rest"), SectionData.TriangleUV2RestArea);
                    FRDGBufferRef RestPositionsBuffer = RegisterOrUploadStructuredBuffer(
                        GraphBuilder,
                        RTState->RestPositionBuffers[SectionIndex],
                        TEXT("DWC.RestPositions"),
                        SectionData.RestPositions);
                    if (!IndicesBuffer || !UV01Buffer || !UV2Buffer || !RestPositionsBuffer)
                    {
                        continue;
                    }

                    FDWCUpdateRestTriangleFlowCS::FParameters* Parameters =
                        GraphBuilder.AllocParameters<FDWCUpdateRestTriangleFlowCS::FParameters>();
                    Parameters->TriangleCount = static_cast<uint32>(SectionData.TriangleIndices.Num());
                    Parameters->PositionCount = static_cast<uint32>(SectionData.RestPositions.Num());
                    Parameters->LocalToWorld = ReceiverLocalToWorldValue;
                    Parameters->WorldGravityDirection = WorldGravityDirection;
                    Parameters->RestPositions = GraphBuilder.CreateSRV(RestPositionsBuffer);
                    Parameters->TriangleIndices = GraphBuilder.CreateSRV(IndicesBuffer);
                    Parameters->TriangleUV01 = GraphBuilder.CreateSRV(UV01Buffer);
                    Parameters->TriangleUV2RestArea = GraphBuilder.CreateSRV(UV2Buffer);
                    Parameters->TriangleFlow = FlowUAV;
                    Parameters->TriangleMetric = MetricUAV;
                    Parameters->TrianglePositions = PositionsUAV;

                    TShaderMapRef<FDWCUpdateRestTriangleFlowCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
                    FDWCWorkloadStats::RecordGPUBackendDispatch();
                    RDG_EVENT_SCOPE_STAT(GraphBuilder, DWC_UpdateTriangleFlow, "DWC UpdateRestTriangleFlow");
                    FComputeShaderUtils::AddPass(
                        GraphBuilder,
                        RDG_EVENT_NAME("DWC Update Rest Triangle Flow Section %d", SectionIndex),
                        Shader,
                        Parameters,
                        FIntVector(FMath::DivideAndRoundUp(SectionData.TriangleIndices.Num(), 64), 1, 1));
                }

            }

            FRDGBufferSRVRef FlowSRV = GraphBuilder.CreateSRV(TriangleFlowBuffer);
            FRDGBufferSRVRef MetricSRV = GraphBuilder.CreateSRV(TriangleMetricBuffer);
            FRDGBufferSRVRef TrianglePositionsSRV = GraphBuilder.CreateSRV(TrianglePositionsBuffer);
            TShaderMapRef<FDWCApplyTriangleAbsorptionCS> AbsorptionShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
            TShaderMapRef<FDWCApplyBinnedAbsorptionCS> BinnedAbsorptionShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
            TShaderMapRef<FDWCApplyNiagaraWetCollisionCS> NiagaraWetCollisionShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
            TShaderMapRef<FDWCResolveNiagaraDropletContactsCS> ResolveNiagaraDropletContactsShader(
                GetGlobalShaderMap(GMaxRHIFeatureLevel));
            TShaderMapRef<FDWCStampNiagaraDropletsCS> StampNiagaraDropletsShader(
                GetGlobalShaderMap(GMaxRHIFeatureLevel));
            TShaderMapRef<FDWCDiffuseDryCS> DiffuseDry4Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
            TShaderMapRef<FDWCDiffuseDry8CS> DiffuseDry8Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
            TShaderMapRef<FDWCSeamGatherCS> SeamShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
            TShaderMapRef<FDWCSurfaceDropletStampCS> DropletStampShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
            TShaderMapRef<FDWCSurfaceWetnessDryInPlaceCS> SurfaceDryShader(
                GetGlobalShaderMap(GMaxRHIFeatureLevel));

            TArray<FDWCGPUNiagaraWetCollisionBuffer> NiagaraWetCollisionBuffers;
            DWCGPUNiagaraWetCollisionBridge::CollectBuffers_RenderThread(NiagaraWetCollisionBuffers);

            struct FPreparedNiagaraWetCollisionResources
            {
                int32 MaxContacts = 0;
                FRDGBufferSRVRef Contacts = nullptr;
                FRDGBufferSRVRef ContactCount = nullptr;
                FRDGBufferSRVRef ResolvedStaticDropletContacts = nullptr;
                FRDGBufferSRVRef ResolvedFlowDropletContacts = nullptr;
            };

            TArray<FPreparedNiagaraWetCollisionResources> PreparedNiagaraWetCollisionResources;
            if (ReceiverGPUIdValue != 0)
            {
                PreparedNiagaraWetCollisionResources.Reserve(NiagaraWetCollisionBuffers.Num());
                for (const FDWCGPUNiagaraWetCollisionBuffer& CollisionBuffer : NiagaraWetCollisionBuffers)
                {
                    if (!CollisionBuffer.ContactBuffer.IsValid() ||
                        !CollisionBuffer.ContactCountBuffer.IsValid() ||
                        CollisionBuffer.MaxContacts <= 0)
                    {
                        continue;
                    }
                    if (CollisionBuffer.bRestrictToTargetReceiverGPUIds &&
                        !CollisionBuffer.TargetReceiverGPUIds.Contains(ReceiverGPUIdValue))
                    {
                        continue;
                    }

                    FRDGBufferRef ContactBuffer = GraphBuilder.RegisterExternalBuffer(
                        CollisionBuffer.ContactBuffer,
                        TEXT("DWC.NiagaraWetCollision.Contacts.External"));
                    FRDGBufferRef ContactCountBuffer = GraphBuilder.RegisterExternalBuffer(
                        CollisionBuffer.ContactCountBuffer,
                        TEXT("DWC.NiagaraWetCollision.ContactCount.External"));
                    FRDGBufferRef ResolvedStaticDropletContactsBuffer = GraphBuilder.CreateBuffer(
                        FRDGBufferDesc::CreateStructuredDesc(
                            sizeof(FUint4GPU),
                            static_cast<uint32>(CollisionBuffer.MaxContacts)),
                        TEXT("DWC.NiagaraWetCollision.ResolvedStaticDropletContacts"));
                    FRDGBufferRef ResolvedFlowDropletContactsBuffer = GraphBuilder.CreateBuffer(
                        FRDGBufferDesc::CreateStructuredDesc(
                            sizeof(FUint4GPU),
                            static_cast<uint32>(CollisionBuffer.MaxContacts)),
                        TEXT("DWC.NiagaraWetCollision.ResolvedFlowDropletContacts"));
                    FRDGBufferUAVRef ResolvedStaticDropletContactsUAV =
                        GraphBuilder.CreateUAV(ResolvedStaticDropletContactsBuffer);
                    FRDGBufferUAVRef ResolvedFlowDropletContactsUAV =
                        GraphBuilder.CreateUAV(ResolvedFlowDropletContactsBuffer);

                    FPreparedNiagaraWetCollisionResources& Prepared =
                        PreparedNiagaraWetCollisionResources.AddDefaulted_GetRef();
                    Prepared.MaxContacts = CollisionBuffer.MaxContacts;
                    Prepared.Contacts =
                        GraphBuilder.CreateSRV(ContactBuffer, PF_A32B32G32R32F);
                    Prepared.ContactCount =
                        GraphBuilder.CreateSRV(ContactCountBuffer, PF_R32_SINT);
                    Prepared.ResolvedStaticDropletContacts =
                        GraphBuilder.CreateSRV(ResolvedStaticDropletContactsBuffer);
                    Prepared.ResolvedFlowDropletContacts =
                        GraphBuilder.CreateSRV(ResolvedFlowDropletContactsBuffer);

                    for (int32 ContactOffset = 0;
                         ContactOffset < CollisionBuffer.MaxContacts;
                         ContactOffset += DWCMaxComputeGroupsPerDimension)
                    {
                        const int32 ResolveBatchCount = FMath::Min(
                            DWCMaxComputeGroupsPerDimension,
                            CollisionBuffer.MaxContacts - ContactOffset);
                        FDWCResolveNiagaraDropletContactsCS::FParameters* Parameters =
                            GraphBuilder.AllocParameters<FDWCResolveNiagaraDropletContactsCS::FParameters>();
                        Parameters->TriangleCount =
                            static_cast<uint32>(StaticData->TriangleCount);
                        Parameters->ContactIndexOffset =
                            static_cast<uint32>(ContactOffset);
                        Parameters->MaxContacts = CollisionBuffer.MaxContacts;
                        Parameters->Contacts = Prepared.Contacts;
                        Parameters->ContactCount = Prepared.ContactCount;
                        Parameters->TrianglePositions = TrianglePositionsSRV;
                        Parameters->TriangleUV01 = NiagaraTriangleUV01SRV;
                        Parameters->TriangleUV2AndDroplet =
                            NiagaraTriangleUV2AndDropletSRV;
                        Parameters->TriangleFlowDropletSettings =
                            NiagaraTriangleFlowDropletSettingsSRV;
                        Parameters->TriangleFlowSpawnPositionSpread =
                            NiagaraTriangleFlowSpawnPositionSpreadSRV;
                        Parameters->TriangleSurfaceMetadata =
                            NiagaraTriangleSurfaceMetadataSRV;
                        Parameters->ResolvedStaticContacts =
                            ResolvedStaticDropletContactsUAV;
                        Parameters->ResolvedFlowContacts =
                            ResolvedFlowDropletContactsUAV;
                        FDWCWorkloadStats::RecordGPUBackendDispatch();
                        RDG_EVENT_SCOPE_STAT(
                            GraphBuilder,
                            DWC_NiagaraDropletResolve,
                            "DWC NiagaraDropletResolve");
                        FComputeShaderUtils::AddPass(
                            GraphBuilder,
                            RDG_EVENT_NAME(
                                "DWC Resolve Niagara Droplet Contacts Offset %d Count %d",
                                ContactOffset,
                                ResolveBatchCount),
                            ResolveNiagaraDropletContactsShader,
                            Parameters,
                            FIntVector(ResolveBatchCount, 1, 1));
                    }
                }
            }

            for (FSlotRenderDispatch& SlotDispatch : SlotDispatches)
            {
                if (!StaticData->Slots.IsValidIndex(SlotDispatch.StaticSlotIndex) ||
                    !SharedStaticResources.IsValidIndex(SlotDispatch.StaticSlotIndex) ||
                    !SharedStaticResources[SlotDispatch.StaticSlotIndex].IsValid() ||
                    !SlotDispatch.CurrentResource || !SlotDispatch.NextResource ||
                    !SlotDispatch.CurrentResource->GetRenderTargetTexture() || !SlotDispatch.NextResource->GetRenderTargetTexture())
                {
                    continue;
                }

                const FStaticSimulationData::FSlotData& StaticSlot = StaticData->Slots[SlotDispatch.StaticSlotIndex];
                TSharedPtr<FDWCGPUStaticSlotResources, ESPMode::ThreadSafe> SharedSlot =
                    SharedStaticResources[SlotDispatch.StaticSlotIndex];
                FRDGBufferRef LookupBuffer = RegisterOrUploadStructuredBuffer(
                    GraphBuilder,
                    SharedSlot->TexelLookup,
                    TEXT("DWC.SharedTexelLookup"),
                    StaticSlot.TexelLookup);
                if (!LookupBuffer)
                {
                    continue;
                }
                FRDGBufferSRVRef LookupSRV = GraphBuilder.CreateSRV(LookupBuffer);

                FRDGBufferSRVRef SurfaceLookupSRV = nullptr;
                const bool bHasDroplet1Resource =
                    SlotDispatch.SurfaceDroplet1Resource != nullptr &&
                    SlotDispatch.SurfaceDroplet1Resource->GetRenderTargetTexture() != nullptr;
                const bool bHasDroplet2Resource =
                    SlotDispatch.SurfaceDroplet2Resource != nullptr &&
                    SlotDispatch.SurfaceDroplet2Resource->GetRenderTargetTexture() != nullptr;
                if ((bHasDroplet1Resource ||
                      !SlotDispatch.SurfaceStampDispatches.IsEmpty() ||
                     bHasDroplet2Resource ||
                     !PreparedNiagaraWetCollisionResources.IsEmpty()) &&
                    !StaticSlot.SurfaceTexelLookup.IsEmpty())
                {
                    FRDGBufferRef SurfaceLookupBuffer = RegisterOrUploadStructuredBuffer(
                        GraphBuilder,
                        SharedSlot->SurfaceTexelLookup,
                        TEXT("DWC.SharedSurfaceTexelLookup"),
                        StaticSlot.SurfaceTexelLookup);
                    if (SurfaceLookupBuffer != nullptr)
                    {
                        SurfaceLookupSRV = GraphBuilder.CreateSRV(SurfaceLookupBuffer);
                    }
                }

                FRDGTextureUAVRef Droplet1SurfaceUAV = nullptr;
                if (SurfaceLookupSRV != nullptr &&
                    bHasDroplet1Resource)
                {
                    TRefCountPtr<IPooledRenderTarget> Droplet1External = CreateRenderTarget(
                        SlotDispatch.SurfaceDroplet1Resource->GetRenderTargetTexture(),
                        TEXT("DWC.SurfaceDroplet1"));
                    Droplet1SurfaceUAV = GraphBuilder.CreateUAV(
                        GraphBuilder.RegisterExternalTexture(Droplet1External));
                }
                FRDGTextureUAVRef Droplet2SurfaceUAV = nullptr;
                if (SurfaceLookupSRV != nullptr &&
                    bHasDroplet2Resource)
                {
                    TRefCountPtr<IPooledRenderTarget> Droplet2External = CreateRenderTarget(
                        SlotDispatch.SurfaceDroplet2Resource->GetRenderTargetTexture(),
                        TEXT("DWC.SurfaceDroplet2"));
                    Droplet2SurfaceUAV = GraphBuilder.CreateUAV(
                        GraphBuilder.RegisterExternalTexture(Droplet2External));
                }

                auto AddSurfaceDryPass =
                    [&](FRDGTextureUAVRef SurfaceUAV, const TCHAR* SurfaceName)
                    {
                        if (SurfaceUAV == nullptr)
                        {
                            return;
                        }

                        FDWCSurfaceWetnessDryInPlaceCS::FParameters* DryParameters =
                            GraphBuilder.AllocParameters<FDWCSurfaceWetnessDryInPlaceCS::FParameters>();
                        DryParameters->TextureSize = FIntPoint(
                            SlotDispatch.SurfaceWaterResolution,
                            SlotDispatch.SurfaceWaterResolution);
                        DryParameters->DeltaSeconds = DeltaSeconds;
                        DryParameters->Surface = SurfaceUAV;
                        DryParameters->TexelLookup = SurfaceLookupSRV;
                        DryParameters->Profiles = ProfileSRV;
                        DryParameters->TriangleProfileIndices = TriangleProfileIndexSRV;
                        FDWCWorkloadStats::RecordGPUBackendDispatch();
                        RDG_EVENT_SCOPE_STAT(GraphBuilder, DWC_SurfaceDry, "DWC SurfaceDry");
                        FComputeShaderUtils::AddPass(
                            GraphBuilder,
                            RDG_EVENT_NAME(
                                "DWC Surface Dry %s Slot %d",
                                SurfaceName,
                                StaticSlot.MaterialSlotIndex),
                            SurfaceDryShader,
                            DryParameters,
                            FIntVector(
                                FMath::DivideAndRoundUp(SlotDispatch.SurfaceWaterResolution, 8),
                                FMath::DivideAndRoundUp(SlotDispatch.SurfaceWaterResolution, 8),
                                1));
                    };
                AddSurfaceDryPass(Droplet1SurfaceUAV, TEXT("Droplet1"));
                AddSurfaceDryPass(Droplet2SurfaceUAV, TEXT("Droplet2"));

                for (int32 StampIndex = 0; StampIndex < SlotDispatch.SurfaceStampDispatches.Num(); ++StampIndex)
                {
                    const FSurfaceStampDispatch& Stamp = SlotDispatch.SurfaceStampDispatches[StampIndex];
                    const FIntVector GroupCount(
                        FMath::DivideAndRoundUp(Stamp.DispatchSize.X, 8),
                        FMath::DivideAndRoundUp(Stamp.DispatchSize.Y, 8),
                        1);

                    FRDGTextureUAVRef TargetSurfaceUAV =
                        Stamp.bDroplet2 ? Droplet2SurfaceUAV : Droplet1SurfaceUAV;
                    if (TargetSurfaceUAV != nullptr)
                    {
                        FDWCSurfaceDropletStampCS::FParameters* Parameters =
                            GraphBuilder.AllocParameters<FDWCSurfaceDropletStampCS::FParameters>();
                        Parameters->TextureSize = FIntPoint(
                            SlotDispatch.SurfaceWaterResolution,
                            SlotDispatch.SurfaceWaterResolution);
                        Parameters->TriangleCount = static_cast<uint32>(StaticData->TriangleCount);
                        Parameters->StampMinPixel = Stamp.DispatchMin;
                        Parameters->StampDispatchSize = Stamp.DispatchSize;
                        Parameters->StampUV = Stamp.UV;
                        Parameters->StampCenterPixels = Stamp.CenterPixels;
                        Parameters->StampHalfSizePixels = Stamp.HalfSizePixels;
                        Parameters->StampAmount = Stamp.Amount;
                        Parameters->TexelLookup = SurfaceLookupSRV;
                        Parameters->TargetSurface = TargetSurfaceUAV;
                        FDWCWorkloadStats::RecordGPUBackendDispatch();
                        RDG_EVENT_SCOPE_STAT(GraphBuilder, DWC_SurfaceStamp, "DWC SurfaceStamp");
                        FComputeShaderUtils::AddPass(
                            GraphBuilder,
                            RDG_EVENT_NAME(
                                "DWC Surface %s Droplet Slot %d Stamp %d",
                                Stamp.bDroplet2 ? TEXT("Droplet2") : TEXT("Droplet1"),
                                StaticSlot.MaterialSlotIndex,
                                StampIndex),
                            DropletStampShader,
                            Parameters,
                            GroupCount);
                    }
                }

                TRefCountPtr<IPooledRenderTarget> CurrentExternal = CreateRenderTarget(
                    SlotDispatch.CurrentResource->GetRenderTargetTexture(), TEXT("DWC.AbsorbedWetness.Current"));
                TRefCountPtr<IPooledRenderTarget> NextExternal = CreateRenderTarget(
                    SlotDispatch.NextResource->GetRenderTargetTexture(), TEXT("DWC.AbsorbedWetness.Next"));
                TRefCountPtr<IPooledRenderTarget> CurrentPendingExternal = CreateRenderTarget(
                    SlotDispatch.CurrentPendingResource->GetRenderTargetTexture(),
                    TEXT("DWC.AbsorbedWetness.PendingCurrent"));
                TRefCountPtr<IPooledRenderTarget> NextPendingExternal = CreateRenderTarget(
                    SlotDispatch.NextPendingResource->GetRenderTargetTexture(),
                    TEXT("DWC.AbsorbedWetness.PendingNext"));
                FRDGTextureRef CurrentTexture = GraphBuilder.RegisterExternalTexture(CurrentExternal);
                FRDGTextureRef NextTexture = GraphBuilder.RegisterExternalTexture(NextExternal);
                FRDGTextureRef CurrentPendingTexture =
                    GraphBuilder.RegisterExternalTexture(CurrentPendingExternal);
                FRDGTextureRef NextPendingTexture =
                    GraphBuilder.RegisterExternalTexture(NextPendingExternal);

                FRDGTextureDesc WorkingDesc = CurrentTexture->Desc;
                WorkingDesc.ClearValue = FClearValueBinding::Black;
                FRDGTextureRef InputAppliedTexture = GraphBuilder.CreateTexture(WorkingDesc, TEXT("DWC.AbsorbedWetness.InputApplied"));
                AddCopyTexturePass(GraphBuilder, CurrentTexture, InputAppliedTexture);
                FRDGTextureRef InputPendingTexture = GraphBuilder.CreateTexture(WorkingDesc, TEXT("DWC.AbsorbedWetness.PendingInputApplied"));
                AddCopyTexturePass(GraphBuilder, CurrentPendingTexture, InputPendingTexture);
                FRDGTextureUAVRef InputUAV = GraphBuilder.CreateUAV(InputAppliedTexture);
                FRDGTextureUAVRef PendingInputUAV = GraphBuilder.CreateUAV(InputPendingTexture);

                if (!SlotDispatch.BinnedAbsorptionContacts.IsEmpty() &&
                    !SlotDispatch.BinnedAbsorptionTileBins.IsEmpty() &&
                    !SlotDispatch.BinnedAbsorptionTileContactIndices.IsEmpty())
                {
                    FRDGBufferRef BinnedContactsBuffer = CreateStructuredBuffer(
                        GraphBuilder,
                        TEXT("DWC.AbsorptionBinned.Contacts"),
                        SlotDispatch.BinnedAbsorptionContacts);
                    FRDGBufferRef BinnedTileBinsBuffer = CreateStructuredBuffer(
                        GraphBuilder,
                        TEXT("DWC.AbsorptionBinned.TileBins"),
                        SlotDispatch.BinnedAbsorptionTileBins);
                    FRDGBufferRef BinnedTileContactIndicesBuffer = CreateStructuredBuffer(
                        GraphBuilder,
                        TEXT("DWC.AbsorptionBinned.TileContactIndices"),
                        SlotDispatch.BinnedAbsorptionTileContactIndices);

                    FDWCApplyBinnedAbsorptionCS::FParameters* Parameters =
                        GraphBuilder.AllocParameters<FDWCApplyBinnedAbsorptionCS::FParameters>();
                    Parameters->TextureSize = FIntPoint(SlotDispatch.Resolution, SlotDispatch.Resolution);
                    Parameters->TileGridSize = SlotDispatch.BinnedAbsorptionTileGridSize;
                    Parameters->TileSize = static_cast<uint32>(DWCAbsorptionBinTileSize);
                    Parameters->Contacts = GraphBuilder.CreateSRV(BinnedContactsBuffer);
                    Parameters->TileBins = GraphBuilder.CreateSRV(BinnedTileBinsBuffer);
                    Parameters->TileContactIndices = GraphBuilder.CreateSRV(BinnedTileContactIndicesBuffer);
                    Parameters->TexelLookup = LookupSRV;
                    Parameters->PendingWetnessTexture = PendingInputUAV;
                    FDWCWorkloadStats::RecordGPUBackendDispatch();
                    RDG_EVENT_SCOPE_STAT(GraphBuilder, DWC_ApplyAbsorption, "DWC ApplyAbsorption");
                    FComputeShaderUtils::AddPass(
                        GraphBuilder,
                        RDG_EVENT_NAME(
                            "DWC Apply Binned Absorption Slot %d Contacts %d",
                            StaticSlot.MaterialSlotIndex,
                            SlotDispatch.BinnedAbsorptionContacts.Num() / DWCAbsorptionContactFloat4Count),
                        BinnedAbsorptionShader,
                        Parameters,
                        FIntVector(
                            FMath::DivideAndRoundUp(SlotDispatch.Resolution, 8),
                            FMath::DivideAndRoundUp(SlotDispatch.Resolution, 8),
                            1));
                }

                for (int32 DispatchIndex = 0; DispatchIndex < SlotDispatch.AbsorptionDispatches.Num(); ++DispatchIndex)
                {
                    const FTriangleAbsorptionDispatch& Dispatch = SlotDispatch.AbsorptionDispatches[DispatchIndex];
                    FDWCApplyTriangleAbsorptionCS::FParameters* Parameters =
                        GraphBuilder.AllocParameters<FDWCApplyTriangleAbsorptionCS::FParameters>();
                    Parameters->TextureSize = FIntPoint(SlotDispatch.Resolution, SlotDispatch.Resolution);
                    Parameters->DispatchMin = Dispatch.DispatchMin;
                    Parameters->DispatchSize = Dispatch.DispatchSize;
                    Parameters->UV01 = Dispatch.UV01;
                    Parameters->UV2AndSettings = Dispatch.UV2AndSettings;
                    Parameters->ContactAndRadius = Dispatch.ContactAndRadius;
                    Parameters->P0AndAmount = Dispatch.P0AndAmount;
                    Parameters->P1AndMaxWetness = Dispatch.P1AndMaxWetness;
                    Parameters->P2AndMode = Dispatch.P2AndMode;
                    Parameters->TexelLookup = LookupSRV;
                    Parameters->WetnessTexture = InputUAV;
                    Parameters->PendingWetnessTexture = PendingInputUAV;
                    FDWCWorkloadStats::RecordGPUBackendDispatch();
                    RDG_EVENT_SCOPE_STAT(GraphBuilder, DWC_ApplyAbsorption, "DWC ApplyAbsorption");
                    FComputeShaderUtils::AddPass(
                        GraphBuilder,
                        RDG_EVENT_NAME("DWC Apply Absorption %d", DispatchIndex),
                        AbsorptionShader,
                        Parameters,
                        FIntVector(
                            FMath::DivideAndRoundUp(Dispatch.DispatchSize.X, 8),
                            FMath::DivideAndRoundUp(Dispatch.DispatchSize.Y, 8), 1));
                }

                if (!PreparedNiagaraWetCollisionResources.IsEmpty())
                {
                    for (const FPreparedNiagaraWetCollisionResources& CollisionResources :
                         PreparedNiagaraWetCollisionResources)
                    {
                        FDWCApplyNiagaraWetCollisionCS::FParameters* Parameters =
                            GraphBuilder.AllocParameters<FDWCApplyNiagaraWetCollisionCS::FParameters>();
                        Parameters->TextureSize = FIntPoint(SlotDispatch.Resolution, SlotDispatch.Resolution);
                        Parameters->MaxContacts = CollisionResources.MaxContacts;
                        Parameters->ReceiverBoundsMin = ReceiverBoundsMinValue;
                        Parameters->ReceiverBoundsMax = ReceiverBoundsMaxValue;
                        Parameters->Contacts = CollisionResources.Contacts;
                        Parameters->ContactCount = CollisionResources.ContactCount;
                        Parameters->TexelLookup = LookupSRV;
                        Parameters->TrianglePositions = TrianglePositionsSRV;
                        Parameters->PendingWetnessTexture = PendingInputUAV;
                        FDWCWorkloadStats::RecordGPUBackendDispatch();
                        FComputeShaderUtils::AddPass(
                            GraphBuilder,
                            RDG_EVENT_NAME(
                                "DWC Apply Niagara Wet Collision Slot %d MaxContacts %d",
                                StaticSlot.MaterialSlotIndex,
                                CollisionResources.MaxContacts),
                            NiagaraWetCollisionShader,
                            Parameters,
                            FIntVector(
                                FMath::DivideAndRoundUp(SlotDispatch.Resolution, 8),
                                FMath::DivideAndRoundUp(SlotDispatch.Resolution, 8),
                                1));

                        if (SurfaceLookupSRV != nullptr &&
                            SlotDispatch.SurfaceWaterResolution > 0)
                        {
                            const auto AddNiagaraDropletStampPass =
                                [&](const bool bDroplet2,
                                    FRDGBufferSRVRef ResolvedContacts,
                                    FRDGTextureUAVRef TargetSurfaceUAV)
                                {
                                    if (ResolvedContacts == nullptr || TargetSurfaceUAV == nullptr)
                                    {
                                        return;
                                    }

                                    FDWCStampNiagaraDropletsCS::FParameters* DropletParameters =
                                        GraphBuilder.AllocParameters<FDWCStampNiagaraDropletsCS::FParameters>();
                                    DropletParameters->TextureSize = FIntPoint(
                                        SlotDispatch.SurfaceWaterResolution,
                                        SlotDispatch.SurfaceWaterResolution);
                                    DropletParameters->TriangleCount =
                                        static_cast<uint32>(StaticData->TriangleCount);
                                    DropletParameters->MaterialSlotIndex =
                                        static_cast<uint32>(StaticSlot.MaterialSlotIndex);
                                    DropletParameters->bDroplet2 = bDroplet2 ? 1u : 0u;
                                    DropletParameters->MaxContacts =
                                        CollisionResources.MaxContacts;
                                    DropletParameters->Contacts =
                                        CollisionResources.Contacts;
                                    DropletParameters->ContactCount =
                                        CollisionResources.ContactCount;
                                    DropletParameters->ResolvedContacts =
                                        ResolvedContacts;
                                    DropletParameters->TriangleUV2AndDroplet =
                                        NiagaraTriangleUV2AndDropletSRV;
                                    DropletParameters->TriangleFlowDropletSettings =
                                        NiagaraTriangleFlowDropletSettingsSRV;
                                    DropletParameters->TriangleSurfaceMetadata =
                                        NiagaraTriangleSurfaceMetadataSRV;
                                    DropletParameters->TexelLookup = SurfaceLookupSRV;
                                    DropletParameters->TargetSurface = TargetSurfaceUAV;
                                    FDWCWorkloadStats::RecordGPUBackendDispatch();
                                    RDG_EVENT_SCOPE_STAT(
                                        GraphBuilder,
                                        DWC_NiagaraDropletStamp,
                                        "DWC NiagaraDropletStamp");
                                    FComputeShaderUtils::AddPass(
                                        GraphBuilder,
                                        RDG_EVENT_NAME(
                                            "DWC Stamp Niagara %s Droplets Slot %d MaxContacts %d",
                                            bDroplet2 ? TEXT("Droplet2") : TEXT("Droplet1"),
                                            StaticSlot.MaterialSlotIndex,
                                            CollisionResources.MaxContacts),
                                        StampNiagaraDropletsShader,
                                        DropletParameters,
                                        FIntVector(
                                            FMath::DivideAndRoundUp(
                                                SlotDispatch.SurfaceWaterResolution,
                                                8),
                                            FMath::DivideAndRoundUp(
                                                SlotDispatch.SurfaceWaterResolution,
                                                8),
                                            1));
                                };

                            AddNiagaraDropletStampPass(
                                false,
                                CollisionResources.ResolvedStaticDropletContacts,
                                Droplet1SurfaceUAV);
                            AddNiagaraDropletStampPass(
                                true,
                                CollisionResources.ResolvedFlowDropletContacts,
                                Droplet2SurfaceUAV);
                        }
                    }
                }

                if (bUseEightDirectionDiffusion)
                {
                    FDWCDiffuseDry8CS::FParameters* DiffuseParameters =
                        GraphBuilder.AllocParameters<FDWCDiffuseDry8CS::FParameters>();
                    DiffuseParameters->TextureSize = FIntPoint(SlotDispatch.Resolution, SlotDispatch.Resolution);
                    DiffuseParameters->DeltaSeconds = DeltaSeconds;
                    DiffuseParameters->MaxWetness = MaxWetnessValue;
                    DiffuseParameters->DryRateScale = DryRateScaleValue;
                    DiffuseParameters->CapillaryImmediateAbsorptionFraction =
                        CapillaryImmediateAbsorptionFractionValue;
                    DiffuseParameters->SourceWetnessTexture = InputAppliedTexture;
                    DiffuseParameters->PendingWetnessTexture = InputPendingTexture;
                    DiffuseParameters->DestinationWetnessTexture = GraphBuilder.CreateUAV(NextTexture);
                    DiffuseParameters->DestinationPendingWetnessTexture =
                        GraphBuilder.CreateUAV(NextPendingTexture);
                    DiffuseParameters->TexelLookup = LookupSRV;
                    DiffuseParameters->TriangleFlow = FlowSRV;
                    DiffuseParameters->TriangleMetric = MetricSRV;
                    DiffuseParameters->Profiles = ProfileSRV;
                    DiffuseParameters->TriangleProfileIndices = TriangleProfileIndexSRV;
                    FDWCWorkloadStats::RecordGPUBackendDispatch();
                    RDG_EVENT_SCOPE_STAT(GraphBuilder, DWC_DiffuseDry, "DWC DiffuseDry");
                    FComputeShaderUtils::AddPass(
                        GraphBuilder,
                        RDG_EVENT_NAME("DWC Diffuse Gravity Dry 8 Slot %d", StaticSlot.MaterialSlotIndex),
                        DiffuseDry8Shader,
                        DiffuseParameters,
                        FIntVector(
                            FMath::DivideAndRoundUp(SlotDispatch.Resolution, 8),
                            FMath::DivideAndRoundUp(SlotDispatch.Resolution, 8), 1));
                }
                else
                {
                    FDWCDiffuseDryCS::FParameters* DiffuseParameters =
                        GraphBuilder.AllocParameters<FDWCDiffuseDryCS::FParameters>();
                    DiffuseParameters->TextureSize = FIntPoint(SlotDispatch.Resolution, SlotDispatch.Resolution);
                    DiffuseParameters->DeltaSeconds = DeltaSeconds;
                    DiffuseParameters->MaxWetness = MaxWetnessValue;
                    DiffuseParameters->DryRateScale = DryRateScaleValue;
                    DiffuseParameters->CapillaryImmediateAbsorptionFraction =
                        CapillaryImmediateAbsorptionFractionValue;
                    DiffuseParameters->SourceWetnessTexture = InputAppliedTexture;
                    DiffuseParameters->PendingWetnessTexture = InputPendingTexture;
                    DiffuseParameters->DestinationWetnessTexture = GraphBuilder.CreateUAV(NextTexture);
                    DiffuseParameters->DestinationPendingWetnessTexture =
                        GraphBuilder.CreateUAV(NextPendingTexture);
                    DiffuseParameters->TexelLookup = LookupSRV;
                    DiffuseParameters->TriangleFlow = FlowSRV;
                    DiffuseParameters->TriangleMetric = MetricSRV;
                    DiffuseParameters->Profiles = ProfileSRV;
                    DiffuseParameters->TriangleProfileIndices = TriangleProfileIndexSRV;
                    FDWCWorkloadStats::RecordGPUBackendDispatch();
                    RDG_EVENT_SCOPE_STAT(GraphBuilder, DWC_DiffuseDry, "DWC DiffuseDry");
                    FComputeShaderUtils::AddPass(
                        GraphBuilder,
                        RDG_EVENT_NAME("DWC Diffuse Gravity Dry 4 Slot %d", StaticSlot.MaterialSlotIndex),
                        DiffuseDry4Shader,
                        DiffuseParameters,
                        FIntVector(
                            FMath::DivideAndRoundUp(SlotDispatch.Resolution, 8),
                            FMath::DivideAndRoundUp(SlotDispatch.Resolution, 8), 1));
                }

                if (!StaticSlot.SeamDestinations.IsEmpty() && !StaticSlot.SeamIncoming.IsEmpty())
                {
                    FRDGBufferRef SeamDestBuffer = RegisterOrUploadStructuredBuffer(
                        GraphBuilder, SharedSlot->SeamDestinations, TEXT("DWC.SharedSeamDestinations"), StaticSlot.SeamDestinations);
                    FRDGBufferRef SeamIncomingBuffer = RegisterOrUploadStructuredBuffer(
                        GraphBuilder, SharedSlot->SeamIncoming, TEXT("DWC.SharedSeamIncoming"), StaticSlot.SeamIncoming);
                    if (SeamDestBuffer && SeamIncomingBuffer)
                    {
                        FRDGTextureRef SeamResolvedTexture = GraphBuilder.CreateTexture(WorkingDesc, TEXT("DWC.AbsorbedWetness.SeamResolved"));
                        FRDGTextureRef SeamResolvedPendingTexture =
                            GraphBuilder.CreateTexture(
                                WorkingDesc,
                                TEXT("DWC.AbsorbedWetness.SeamPendingResolved"));
                        AddCopyTexturePass(GraphBuilder, NextTexture, SeamResolvedTexture);
                        AddCopyTexturePass(
                            GraphBuilder,
                            NextPendingTexture,
                            SeamResolvedPendingTexture);

                        FDWCSeamGatherCS::FParameters* SeamParameters =
                            GraphBuilder.AllocParameters<FDWCSeamGatherCS::FParameters>();
                        SeamParameters->TextureSize = FIntPoint(SlotDispatch.Resolution, SlotDispatch.Resolution);
                        SeamParameters->SeamDestinationCount = static_cast<uint32>(StaticSlot.SeamDestinations.Num());
                        SeamParameters->DeltaSeconds = DeltaSeconds;
                        SeamParameters->SeamTransferScale = DWCSeamTransferScale;
                        SeamParameters->MaxWetness = MaxWetnessValue;
                        SeamParameters->SourceWetnessTexture = NextTexture;
                        SeamParameters->SourcePendingWetnessTexture = NextPendingTexture;
                        SeamParameters->DestinationWetnessTexture = GraphBuilder.CreateUAV(SeamResolvedTexture);
                        SeamParameters->DestinationPendingWetnessTexture =
                            GraphBuilder.CreateUAV(SeamResolvedPendingTexture);
                        SeamParameters->SeamDestinations = GraphBuilder.CreateSRV(SeamDestBuffer);
                        SeamParameters->SeamIncoming = GraphBuilder.CreateSRV(SeamIncomingBuffer);
                        SeamParameters->TexelLookup = LookupSRV;
                        SeamParameters->TriangleFlow = FlowSRV;
                        SeamParameters->Profiles = ProfileSRV;
                        SeamParameters->TriangleProfileIndices = TriangleProfileIndexSRV;
                        FDWCWorkloadStats::RecordGPUBackendDispatch();
                        RDG_EVENT_SCOPE_STAT(GraphBuilder, DWC_SeamGather, "DWC SeamGather");
                        FComputeShaderUtils::AddPass(
                            GraphBuilder,
                            RDG_EVENT_NAME("DWC Seam Destination Gather Slot %d", StaticSlot.MaterialSlotIndex),
                            SeamShader,
                            SeamParameters,
                            FIntVector(FMath::DivideAndRoundUp(StaticSlot.SeamDestinations.Num(), 64), 1, 1));
                        AddCopyTexturePass(GraphBuilder, SeamResolvedTexture, NextTexture);
                        AddCopyTexturePass(
                            GraphBuilder,
                            SeamResolvedPendingTexture,
                            NextPendingTexture);
                    }
                }
            }

            GraphBuilder.Execute();
        });

}


FDWCGPUBackendStats FDWCGPUBackend::GetStats() const
{
    FDWCGPUBackendStats Stats;
    Stats.ActiveMaterialCount = static_cast<uint32>(MaterialSlots.Num());
    Stats.PendingSurfaceStampCount = static_cast<uint32>(PendingSurfaceStamps.Num());
    Stats.CPUBytes = sizeof(*this) +
                     MaterialSlots.GetAllocatedSize() +
                     PendingContacts.GetAllocatedSize() +
                     PendingSurfaceStamps.GetAllocatedSize() +
                     DebugVertexDataUVs.GetAllocatedSize() +
                     DebugVertexMaterialSlots.GetAllocatedSize();

    for (const FMaterialSlotRuntime& Slot : MaterialSlots)
    {
        Stats.CPUBytes += Slot.WetnessMaps.GetAllocatedSize();
        Stats.CPUBytes += Slot.PendingWetnessMaps.GetAllocatedSize();
        if (Slot.Resolution > 0)
        {
            const uint64 PixelCount = static_cast<uint64>(Slot.Resolution) * Slot.Resolution;
            Stats.GPUBytes += PixelCount * sizeof(uint16) *
                              (Slot.WetnessMaps.Num() + Slot.PendingWetnessMaps.Num());
        }
        if (Slot.bUsesSurfaceWater && Slot.SurfaceWaterResolution > 0)
        {
            const uint64 SurfacePixelCount =
                static_cast<uint64>(Slot.SurfaceWaterResolution) * Slot.SurfaceWaterResolution;
            const uint64 SurfaceRenderTargetCount =
                (Slot.SurfaceDroplet1RT.IsValid() ? 1ull : 0ull) +
                (Slot.SurfaceDroplet2RT.IsValid() ? 1ull : 0ull);
            Stats.GPUBytes +=
                SurfacePixelCount * sizeof(uint16) * SurfaceRenderTargetCount;
        }
    }

    if (StaticSimulationData.IsValid())
    {
        const FStaticSimulationData& Data = *StaticSimulationData;
        Stats.CPUBytes += sizeof(Data) +
                          Data.Sections.GetAllocatedSize() +
                           Data.Profiles.GetAllocatedSize() +
                           Data.TriangleProfileIndices.GetAllocatedSize() +
                           Data.TriangleDataToSurfaceWaterNormalUV.GetAllocatedSize() +
                           Data.TriangleUV01.GetAllocatedSize() +
                           Data.TriangleUV2AndDroplet.GetAllocatedSize() +
                           Data.TriangleFlowDropletSettings.GetAllocatedSize() +
                           Data.TriangleFlowSpawnPositionSpread.GetAllocatedSize() +
                           Data.TriangleSurfaceMetadata.GetAllocatedSize() +
                           Data.Slots.GetAllocatedSize();

        // Lookup/section/profile-index buffers are owned by the world subsystem.
        // Profile values include per-component simulation scale overrides, so the
        // profile buffer remains instance-local together with flow/metric buffers.
        Stats.GPUBytes += static_cast<uint64>(Data.Profiles.Num()) * sizeof(FVector4f);
        Stats.GPUBytes += static_cast<uint64>(Data.TriangleCount) * sizeof(FVector4f) * 2ull;
        Stats.GPUBytes += static_cast<uint64>(Data.TriangleCount) *
                          (sizeof(FVector4f) * 3ull + sizeof(float) + sizeof(FUint4GPU));

        for (const FStaticSimulationData::FSectionData& Section : Data.Sections)
        {
            Stats.CPUBytes += Section.TriangleIndices.GetAllocatedSize() +
                              Section.TriangleUV01.GetAllocatedSize() +
                              Section.TriangleUV2RestArea.GetAllocatedSize() +
                              Section.RestPositions.GetAllocatedSize();
        }

        for (const FStaticSimulationData::FSlotData& Slot : Data.Slots)
        {
            Stats.CPUBytes += Slot.TexelLookup.GetAllocatedSize() +
                              Slot.SurfaceTexelLookup.GetAllocatedSize() +
                              Slot.SeamDestinations.GetAllocatedSize() +
                              Slot.SeamIncoming.GetAllocatedSize();
        }
    }

    if (RenderState.IsValid())
    {
        Stats.CPUBytes += sizeof(FRenderState);
    }

    return Stats;
}

void FDWCGPUBackend::GetDebugRenderTargets(TArray<FDWCGPURenderTargetDebugSnapshot>& OutSnapshots) const
{
    for (const FMaterialSlotRuntime& Slot : MaterialSlots)
    {
        FDWCGPURenderTargetDebugSnapshot& Snapshot = OutSnapshots.AddDefaulted_GetRef();
        Snapshot.ReceiverGPUId = ReceiverGPUId;
        Snapshot.MaterialSlotIndex = Slot.MaterialSlotIndex;
        Snapshot.WetnessMapResolution = Slot.Resolution;
        Snapshot.SurfaceWaterResolution = Slot.bUsesSurfaceWater ? Slot.SurfaceWaterResolution : 0;
        Snapshot.WetnessMap = Slot.GetCurrentMap();
        Snapshot.Droplet1Map = Slot.bUsesSurfaceWater ? Slot.SurfaceDroplet1RT.Get() : nullptr;
        Snapshot.Droplet2Map = Slot.bUsesSurfaceWater ? Slot.SurfaceDroplet2RT.Get() : nullptr;
    }
}

void FDWCGPUBackend::Shutdown()
{
    PendingContacts.Reset();
    PendingSurfaceStamps.Reset();
    DebugVertexDataUVs.Reset();
    DebugVertexMaterialSlots.Reset();
    PendingWetAllAmount = 0.0f;

    for (FMaterialSlotRuntime& Slot : MaterialSlots)
    {
        if (UMaterialInstanceDynamic* MID = Slot.MaterialInstance.Get())
        {
            MID->SetTextureParameterValue(WetnessMapParameterName, nullptr);
            MID->SetTextureParameterValue(DWCWetMaterialParameters::SurfaceDroplet1RT(), nullptr);
            MID->SetTextureParameterValue(DWCWetMaterialParameters::SurfaceDroplet2RT(), nullptr);
        }
    }

    if (RenderState.IsValid())
    {
        FlushRenderingCommands();
    }

    MaterialSlots.Reset();
    SharedStaticSlotResources.Reset();
    StaticSimulationData.Reset();
    RenderState.Reset();
    OwnerComponent.Reset();
    TargetSkeletalMesh.Reset();
    WetClothingAsset.Reset();
    WetMaterialInstances = nullptr;
    WetnessMapParameterName = DWCWetMaterialParameters::WetnessMap();
    MaxWetness = 1.0f;
    SpreadRateScale = 1.0f;
    DryRateScale = 1.0f;
    GravityFlowStrengthScale = 1.0f;
    CapillaryImmediateAbsorptionFraction = 0.65f;
    LODIndex = 0;
    DebugDispatchLogCount = 0;
    bInitialized = false;
}
