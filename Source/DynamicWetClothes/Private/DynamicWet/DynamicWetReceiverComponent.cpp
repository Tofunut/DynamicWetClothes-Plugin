// Fill out your copyright notice in the Description page of Project Settings.

#include "DynamicWet/DynamicWetReceiverComponent.h"

#include "Components/SkeletalMeshComponent.h"

#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"

#include "Engine/EngineTypes.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "WetClothingProfile.h"
#include "WetnessProfile.h"

namespace DynamicWetReceiverRuntime
{
    static constexpr double UVQuantizeScale = 100000.0;

    struct FQuantizedUV
    {
        int64 U = 0;
        int64 V = 0;

        FQuantizedUV() = default;

        explicit FQuantizedUV(const FVector2D& InUV)
            : U(FMath::RoundToInt64(InUV.X * UVQuantizeScale))
            , V(FMath::RoundToInt64(InUV.Y * UVQuantizeScale))
        {
        }

        bool operator==(const FQuantizedUV& Other) const
        {
            return U == Other.U && V == Other.V;
        }
    };

    static uint32 HashInt64(const int64 Value)
    {
        const uint64 UnsignedValue = static_cast<uint64>(Value);
        return HashCombine(
            ::GetTypeHash(static_cast<uint32>(UnsignedValue & 0xFFFFFFFFull)),
            ::GetTypeHash(static_cast<uint32>((UnsignedValue >> 32) & 0xFFFFFFFFull)));
    }

    static uint32 GetTypeHash(const FQuantizedUV& Value)
    {
        return HashCombine(HashInt64(Value.U), HashInt64(Value.V));
    }

    static bool LessUV(const FQuantizedUV& A, const FQuantizedUV& B)
    {
        return A.U != B.U ? A.U < B.U : A.V < B.V;
    }

    struct FUVEdgeKey
    {
        FQuantizedUV A;
        FQuantizedUV B;

        FUVEdgeKey() = default;

        FUVEdgeKey(const FVector2D& InA, const FVector2D& InB)
        {
            FQuantizedUV QuantizedA(InA);
            FQuantizedUV QuantizedB(InB);

            if (LessUV(QuantizedB, QuantizedA))
            {
                A = QuantizedB;
                B = QuantizedA;
            }
            else
            {
                A = QuantizedA;
                B = QuantizedB;
            }
        }

        bool operator==(const FUVEdgeKey& Other) const
        {
            return A == Other.A && B == Other.B;
        }
    };

    static uint32 GetTypeHash(const FUVEdgeKey& Key)
    {
        return HashCombine(GetTypeHash(Key.A), GetTypeHash(Key.B));
    }

    struct FRuntimeUVTriangle
    {
        int32 TriangleID = INDEX_NONE;
        int32 VertexIndices[3] = { INDEX_NONE, INDEX_NONE, INDEX_NONE };
        FVector2D UVs[3];
    };

    static int32 FindParent(TArray<int32>& Parents, const int32 Index)
    {
        if (Parents[Index] == Index)
        {
            return Index;
        }

        Parents[Index] = FindParent(Parents, Parents[Index]);
        return Parents[Index];
    }

    static void UnionParents(TArray<int32>& Parents, const int32 A, const int32 B)
    {
        const int32 RootA = FindParent(Parents, A);
        const int32 RootB = FindParent(Parents, B);

        if (RootA != RootB)
        {
            Parents[RootB] = RootA;
        }
    }

    static bool BuildRuntimeIslandVertexMap(
        const FSkeletalMeshLODRenderData& LODData,
        const int32 UVChannelIndex,
        const int32 MaterialSlotIndex,
        TMap<int32, TArray<int32>>& OutIslandVertices)
    {
        OutIslandVertices.Reset();

        if (UVChannelIndex < 0 || UVChannelIndex >= static_cast<int32>(LODData.GetNumTexCoords()))
        {
            return false;
        }

        TArray<uint32> IndexBuffer;
        LODData.MultiSizeIndexContainer.GetIndexBuffer(IndexBuffer);
        if (IndexBuffer.Num() == 0)
        {
            return false;
        }

        const int32 VertexCount = LODData.GetNumVertices();
        TArray<FRuntimeUVTriangle> Triangles;

        for (const FSkelMeshRenderSection& Section : LODData.RenderSections)
        {
            if (!Section.IsValid() || Section.MaterialIndex != MaterialSlotIndex)
            {
                continue;
            }

            const int32 FirstIndex = static_cast<int32>(Section.BaseIndex);
            const int32 LastIndex = FMath::Min(FirstIndex + static_cast<int32>(Section.NumTriangles * 3), IndexBuffer.Num());

            for (int32 Index = FirstIndex; Index + 2 < LastIndex; Index += 3)
            {
                const uint32 Index0 = IndexBuffer[Index];
                const uint32 Index1 = IndexBuffer[Index + 1];
                const uint32 Index2 = IndexBuffer[Index + 2];

                if (Index0 >= static_cast<uint32>(VertexCount) ||
                    Index1 >= static_cast<uint32>(VertexCount) ||
                    Index2 >= static_cast<uint32>(VertexCount))
                {
                    continue;
                }

                FRuntimeUVTriangle Triangle;
                Triangle.TriangleID = Triangles.Num();
                Triangle.VertexIndices[0] = static_cast<int32>(Index0);
                Triangle.VertexIndices[1] = static_cast<int32>(Index1);
                Triangle.VertexIndices[2] = static_cast<int32>(Index2);
                Triangle.UVs[0] = FVector2D(LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(Index0, UVChannelIndex));
                Triangle.UVs[1] = FVector2D(LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(Index1, UVChannelIndex));
                Triangle.UVs[2] = FVector2D(LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(Index2, UVChannelIndex));
                Triangles.Add(Triangle);
            }
        }

        if (Triangles.Num() == 0)
        {
            return true;
        }

        TArray<int32> Parents;
        Parents.SetNum(Triangles.Num());
        for (int32 TriangleIndex = 0; TriangleIndex < Triangles.Num(); ++TriangleIndex)
        {
            Parents[TriangleIndex] = TriangleIndex;
        }

        TMap<FUVEdgeKey, TArray<int32>> EdgeToTriangles;
        for (int32 TriangleIndex = 0; TriangleIndex < Triangles.Num(); ++TriangleIndex)
        {
            const FRuntimeUVTriangle& Triangle = Triangles[TriangleIndex];
            EdgeToTriangles.FindOrAdd(FUVEdgeKey(Triangle.UVs[0], Triangle.UVs[1])).Add(TriangleIndex);
            EdgeToTriangles.FindOrAdd(FUVEdgeKey(Triangle.UVs[1], Triangle.UVs[2])).Add(TriangleIndex);
            EdgeToTriangles.FindOrAdd(FUVEdgeKey(Triangle.UVs[2], Triangle.UVs[0])).Add(TriangleIndex);
        }

        for (const TPair<FUVEdgeKey, TArray<int32>>& Pair : EdgeToTriangles)
        {
            const TArray<int32>& ConnectedTriangles = Pair.Value;
            if (ConnectedTriangles.Num() <= 1)
            {
                continue;
            }

            const int32 FirstTriangle = ConnectedTriangles[0];
            for (int32 ConnectedIndex = 1; ConnectedIndex < ConnectedTriangles.Num(); ++ConnectedIndex)
            {
                UnionParents(Parents, FirstTriangle, ConnectedTriangles[ConnectedIndex]);
            }
        }

        TMap<int32, int32> RootToIslandID;
        TMap<int32, TSet<int32>> IslandVertexSets;
        for (int32 TriangleIndex = 0; TriangleIndex < Triangles.Num(); ++TriangleIndex)
        {
            const int32 Root = FindParent(Parents, TriangleIndex);
            int32* ExistingIslandID = RootToIslandID.Find(Root);
            if (ExistingIslandID == nullptr)
            {
                RootToIslandID.Add(Root, RootToIslandID.Num());
                ExistingIslandID = RootToIslandID.Find(Root);
            }

            TSet<int32>& VertexSet = IslandVertexSets.FindOrAdd(*ExistingIslandID);
            const FRuntimeUVTriangle& Triangle = Triangles[TriangleIndex];
            VertexSet.Add(Triangle.VertexIndices[0]);
            VertexSet.Add(Triangle.VertexIndices[1]);
            VertexSet.Add(Triangle.VertexIndices[2]);
        }

        for (const TPair<int32, TSet<int32>>& Pair : IslandVertexSets)
        {
            TArray<int32>& IslandVertices = OutIslandVertices.FindOrAdd(Pair.Key);
            IslandVertices.Reserve(Pair.Value.Num());
            for (const int32 VertexIndex : Pair.Value)
            {
                IslandVertices.Add(VertexIndex);
            }
        }

        return true;
    }
} // namespace DynamicWetReceiverRuntime

// Sets default values for this component's properties
UDynamicWetReceiverComponent::UDynamicWetReceiverComponent()
{
    // Wetness is updated by timer; per-frame ticking is too expensive while in water.
    PrimaryComponentTick.bCanEverTick = false;

    // ...
}

// Called when the game starts
void UDynamicWetReceiverComponent::BeginPlay()
{
    Super::BeginPlay();

    TargetSkeletalMesh = ResolveTargetSkeletalMesh();
    if (!TargetSkeletalMesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("DynamicWetReceiverComponent: Target SkeletalMesh not found"));
        return;
    }

    InitializeWetnessData();
    InitializeWetPartVertexData();
    BuildNeighborGraph();
    InitializeWetMaterialInstance();
    ApplyWetMaterialParameters();

    GetWorld()->GetTimerManager().SetTimer(
        WetnessUpdateTimer,
        this,
        &UDynamicWetReceiverComponent::UpdateWetness,
        WetnessUpdateInterval,
        true);
}

void UDynamicWetReceiverComponent::InitializeWetnessData()
{
    FSkeletalMeshLODRenderData* LODData = nullptr;
    if (!GetLODRenderData(0, LODData))
    {
        return;
    }

    const int32 VertexCount = LODData->GetNumVertices();

    WetnessPerVertex.SetNumZeroed(VertexCount);
    Updating_Pending_Wetness_Amounts.SetNumZeroed(VertexCount);
    WetnessDryHoldTimePerVertex.SetNumZeroed(VertexCount);
    Updating_Pending_Wetness_Vertex_IndexQueue.Reset();
    Current_Pending_Wetness_Vertex_IndexQueue.Reset();
    Current_Pending_Wetness_Amounts.Reset();
    bPendingWetnessQueued.Init(false, VertexCount);
    CachedWetVertexColors.Init(FLinearColor::Black, VertexCount);
    DirtyWetVertexIndices.Reset();

    TargetSkeletalMesh->SetVertexColorOverride_LinearColor(0, CachedWetVertexColors);
    TargetSkeletalMesh->MarkRenderStateDirty();
}

void UDynamicWetReceiverComponent::InitializeWetPartVertexData()
{
    FSkeletalMeshLODRenderData* LODData = nullptr;
    if (!GetLODRenderData(0, LODData))
    {
        return;
    }

    const int32 VertexCount = LODData->GetNumVertices();
    VertexWetPartIDs.Init(INDEX_NONE, VertexCount);
    VertexWetnessProfileParameters.SetNum(VertexCount);

    FWetnessProfileParameters DefaultParameters;
    if (const UWetnessProfile* MaterialPreset = GetActiveMaterialProfile())
    {
        DefaultParameters = MaterialPreset->GetParameters();
    }

    for (FWetnessProfileParameters& VertexParameters : VertexWetnessProfileParameters)
    {
        VertexParameters = DefaultParameters;
    }

    if (!WetClothingProfile)
    {
        return;
    }

    USkeletalMesh* SkeletalMesh = TargetSkeletalMesh ? TargetSkeletalMesh->GetSkeletalMeshAsset() : nullptr;
    if (WetClothingProfile->TargetMesh && WetClothingProfile->TargetMesh != SkeletalMesh)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("DynamicWetReceiverComponent: WetClothingProfile TargetMesh does not match the receiver mesh on %s."),
            *GetNameSafe(GetOwner()));
    }

    TMap<FIntPoint, TMap<int32, TArray<int32>>> IslandVertexMapCache;
    for (const FWetClothingProfileWetPartEntry& WetPartEntry : WetClothingProfile->WetPartEntries)
    {
        if (WetPartEntry.MaterialSlotIndex == INDEX_NONE ||
            WetPartEntry.UVChannelIndex < 0 ||
            WetPartEntry.AssignedIslandIDs.Num() == 0)
        {
            continue;
        }

        const FIntPoint CacheKey(WetPartEntry.MaterialSlotIndex, WetPartEntry.UVChannelIndex);
        TMap<int32, TArray<int32>>* IslandVertexMap = IslandVertexMapCache.Find(CacheKey);
        if (IslandVertexMap == nullptr)
        {
            TMap<int32, TArray<int32>> NewIslandVertexMap;
            DynamicWetReceiverRuntime::BuildRuntimeIslandVertexMap(
                *LODData,
                WetPartEntry.UVChannelIndex,
                WetPartEntry.MaterialSlotIndex,
                NewIslandVertexMap);

            IslandVertexMapCache.Add(CacheKey, MoveTemp(NewIslandVertexMap));
            IslandVertexMap = IslandVertexMapCache.Find(CacheKey);
        }

        if (IslandVertexMap == nullptr)
        {
            continue;
        }

        for (const int32 IslandID : WetPartEntry.AssignedIslandIDs)
        {
            const TArray<int32>* IslandVertices = IslandVertexMap->Find(IslandID);
            if (IslandVertices == nullptr)
            {
                continue;
            }

            for (const int32 VertexIndex : *IslandVertices)
            {
                if (!VertexWetPartIDs.IsValidIndex(VertexIndex) ||
                    !VertexWetnessProfileParameters.IsValidIndex(VertexIndex))
                {
                    continue;
                }

                VertexWetPartIDs[VertexIndex] = WetPartEntry.WetPartID;
                VertexWetnessProfileParameters[VertexIndex] = WetPartEntry.ProfileAssignment.Parameters;
            }
        }
    }
}

void UDynamicWetReceiverComponent::BuildNeighborGraph()
{
    FSkeletalMeshLODRenderData* LODData = nullptr;
    if (!GetLODRenderData(0, LODData))
    {
        return;
    }

    const int32 VertexCount = LODData->GetNumVertices();

    if (NeighborGraph.Num() != VertexCount)
    {
        NeighborGraph.Empty();
        NeighborGraph.SetNum(VertexCount);
    }

    for (FVertexNeighbors& VertexNeighbors : NeighborGraph)
    {
        VertexNeighbors.Neighbors.Reset();
    }

    const FRawStaticIndexBuffer16or32Interface* IndexBuffer =
        LODData->MultiSizeIndexContainer.GetIndexBuffer();

    if (!IndexBuffer)
    {
        return;
    }

    const int32 IndexCount = IndexBuffer->Num();
    for (int32 Index = 0; Index + 2 < IndexCount; Index += 3)
    {
        const int32 V0 = IndexBuffer->Get(Index);
        const int32 V1 = IndexBuffer->Get(Index + 1);
        const int32 V2 = IndexBuffer->Get(Index + 2);

        AddNeighbor(V0, V1);
        AddNeighbor(V1, V0);

        AddNeighbor(V1, V2);
        AddNeighbor(V2, V1);

        AddNeighbor(V2, V0);
        AddNeighbor(V0, V2);
    }
}

void UDynamicWetReceiverComponent::InitializeWetMaterialInstance()
{
    WetMaterialInstances.Reset();

    if (!TargetSkeletalMesh)
    {
        return;
    }

    const int32 MaterialCount = TargetSkeletalMesh->GetNumMaterials();
    WetMaterialInstances.SetNum(MaterialCount);

    for (int32 MaterialIdx = 0; MaterialIdx < MaterialCount; ++MaterialIdx)
    {
        UMaterialInstanceDynamic* MID =
            TargetSkeletalMesh->CreateAndSetMaterialInstanceDynamic(MaterialIdx);

        WetMaterialInstances[MaterialIdx] = MID;
    }
}

void UDynamicWetReceiverComponent::ApplyWetMaterialParameters()
{
    for (UMaterialInstanceDynamic* MID : WetMaterialInstances)
    {
        if (!MID)
        {
            continue;
        }

        MID->SetVectorParameterValue(
            TEXT("FallbackUnderColor"),
            FallbackUnderColor);

        MID->SetScalarParameterValue(
            TEXT("WetUnderColorBlendStrength"),
            WetUnderColorBlendStrength);
    }
}

void UDynamicWetReceiverComponent::AddNeighbor(int32 VertexIndex, int32 NeighborIndex)
{
    if (!NeighborGraph.IsValidIndex(VertexIndex))
    {
        return;
    }

    if (!NeighborGraph.IsValidIndex(NeighborIndex))
    {
        return;
    }

    if (VertexIndex == NeighborIndex)
    {
        return;
    }

    TArray<int32>& Neighbors = NeighborGraph[VertexIndex].Neighbors;

    if (!Neighbors.Contains(NeighborIndex))
    {
        Neighbors.Add(NeighborIndex);
    }
}

const UWetnessProfile* UDynamicWetReceiverComponent::GetActiveMaterialProfile() const
{
    for (const UWetnessProfile* MaterialPreset : MaterialProfiles)
    {
        if (MaterialPreset)
        {
            return MaterialPreset;
        }
    }

    return nullptr;
}

float UDynamicWetReceiverComponent::GetAbsorptionMultiplier() const
{
    const UWetnessProfile* MaterialPreset = GetActiveMaterialProfile();
    return MaterialPreset ? MaterialPreset->GetAbsorptionMultiplier() : 1.0f;
}

float UDynamicWetReceiverComponent::GetDryRatePerSecond() const
{
    const UWetnessProfile* MaterialPreset = GetActiveMaterialProfile();
    return MaterialPreset ? MaterialPreset->GetDryRatePerSecond() : 1.0f;
}

float UDynamicWetReceiverComponent::GetSpreadRatePerSecond() const
{
    const UWetnessProfile* MaterialPreset = GetActiveMaterialProfile();
    return MaterialPreset ? MaterialPreset->GetSpreadRatePerSecond() : 0.0f;
}

float UDynamicWetReceiverComponent::GetGravityFlowStrength() const
{
    const UWetnessProfile* MaterialPreset = GetActiveMaterialProfile();
    return MaterialPreset ? MaterialPreset->GetGravityFlowStrength() : 0.0f;
}

float UDynamicWetReceiverComponent::GetAbsorptionMultiplierForVertex(const int32 VertexIndex) const
{
    return VertexWetnessProfileParameters.IsValidIndex(VertexIndex)
               ? VertexWetnessProfileParameters[VertexIndex].GetAbsorptionMultiplier()
               : GetAbsorptionMultiplier();
}

float UDynamicWetReceiverComponent::GetDryRatePerSecondForVertex(const int32 VertexIndex) const
{
    return VertexWetnessProfileParameters.IsValidIndex(VertexIndex)
               ? VertexWetnessProfileParameters[VertexIndex].GetDryRatePerSecond()
               : GetDryRatePerSecond();
}

float UDynamicWetReceiverComponent::GetSpreadRatePerSecondForVertex(const int32 VertexIndex) const
{
    return VertexWetnessProfileParameters.IsValidIndex(VertexIndex)
               ? VertexWetnessProfileParameters[VertexIndex].GetSpreadRatePerSecond()
               : GetSpreadRatePerSecond();
}

float UDynamicWetReceiverComponent::GetGravityFlowStrengthForVertex(const int32 VertexIndex) const
{
    return VertexWetnessProfileParameters.IsValidIndex(VertexIndex)
               ? VertexWetnessProfileParameters[VertexIndex].GetGravityFlowStrength()
               : GetGravityFlowStrength();
}

void UDynamicWetReceiverComponent::EnsureWetnessBufferSize(const int32 VertexCount)
{
    if (VertexCount <= 0)
    {
        WetnessPerVertex.Reset();
        VertexWetPartIDs.Reset();
        VertexWetnessProfileParameters.Reset();
        Updating_Pending_Wetness_Amounts.Reset();
        WetnessDryHoldTimePerVertex.Reset();
        Updating_Pending_Wetness_Vertex_IndexQueue.Reset();
        Current_Pending_Wetness_Vertex_IndexQueue.Reset();
        Current_Pending_Wetness_Amounts.Reset();
        bPendingWetnessQueued.Reset();
        return;
    }

    if (WetnessPerVertex.Num() != VertexCount)
    {
        WetnessPerVertex.SetNumZeroed(VertexCount);
    }

    if (VertexWetPartIDs.Num() != VertexCount ||
        VertexWetnessProfileParameters.Num() != VertexCount)
    {
        InitializeWetPartVertexData();
    }

    if (Updating_Pending_Wetness_Amounts.Num() != VertexCount)
    {
        Updating_Pending_Wetness_Amounts.SetNumZeroed(VertexCount);
        Updating_Pending_Wetness_Vertex_IndexQueue.Reset();
    }

    if (WetnessDryHoldTimePerVertex.Num() != VertexCount)
    {
        WetnessDryHoldTimePerVertex.SetNumZeroed(VertexCount);
    }

    if (bPendingWetnessQueued.Num() != VertexCount)
    {
        bPendingWetnessQueued.Init(false, VertexCount);
        Updating_Pending_Wetness_Vertex_IndexQueue.Reset();
    }
}

float UDynamicWetReceiverComponent::AbsorbWetnessAtVertex(const int32 VertexIndex, const float Amount, bool& bDirty)
{
    if (!WetnessPerVertex.IsValidIndex(VertexIndex) || FMath::IsNearlyZero(Amount))
    {
        return 0.0f;
    }

    if (Amount > MinPendingWetnessAmount)
    {
        RefreshWetnessDryHold(VertexIndex);
    }

    float&      Wetness = WetnessPerVertex[VertexIndex];
    const float OldWetness = Wetness;
    const float NewWetness = FMath::Clamp(
        Wetness + Amount,
        0.0f,
        FMath::Max(0.0f, MaxStoredWetness));
    const float AbsorbedAmount = NewWetness - OldWetness;

    if (!FMath::IsNearlyEqual(OldWetness, NewWetness))
    {
        Wetness = NewWetness;
        DirtyWetVertexIndices.Add(VertexIndex);
        bDirty = true;
    }

    return AbsorbedAmount;
}

void UDynamicWetReceiverComponent::QueuePendingWetness(const int32 VertexIndex, const float Amount)
{
    if (!WetnessPerVertex.IsValidIndex(VertexIndex) ||
        !Updating_Pending_Wetness_Amounts.IsValidIndex(VertexIndex) ||
        !bPendingWetnessQueued.IsValidIndex(VertexIndex) ||
        Amount <= MinPendingWetnessAmount)
    {
        return;
    }

    Updating_Pending_Wetness_Amounts[VertexIndex] += Amount;
    RefreshWetnessDryHold(VertexIndex);

    if (!bPendingWetnessQueued[VertexIndex])
    {
        bPendingWetnessQueued[VertexIndex] = true;
        Updating_Pending_Wetness_Vertex_IndexQueue.Add(VertexIndex);
    }
}

void UDynamicWetReceiverComponent::RefreshWetnessDryHold(const int32 VertexIndex)
{
    if (!WetnessDryHoldTimePerVertex.IsValidIndex(VertexIndex) ||
        WetnessDryHoldDuration <= 0.0f)
    {
        return;
    }

    WetnessDryHoldTimePerVertex[VertexIndex] = FMath::Max(
        WetnessDryHoldTimePerVertex[VertexIndex],
        WetnessDryHoldDuration);
}

void UDynamicWetReceiverComponent::ClearPendingWetness()
{
    for (float& PendingWetness : Updating_Pending_Wetness_Amounts)
    {
        PendingWetness = 0.0f;
    }

    Updating_Pending_Wetness_Vertex_IndexQueue.Reset();
    Current_Pending_Wetness_Vertex_IndexQueue.Reset();
    Current_Pending_Wetness_Amounts.Reset();

    for (bool& bQueued : bPendingWetnessQueued)
    {
        bQueued = false;
    }
}

void UDynamicWetReceiverComponent::DryOutWetness(bool& bDirty, const float EffectiveDryRatePerSecond)
{
    for (int32 VertexIndex = 0; VertexIndex < WetnessPerVertex.Num(); ++VertexIndex)
    {
        if (WetnessDryHoldTimePerVertex.IsValidIndex(VertexIndex) &&
            WetnessDryHoldTimePerVertex[VertexIndex] > 0.0f)
        {
            WetnessDryHoldTimePerVertex[VertexIndex] = FMath::Max(
                0.0f,
                WetnessDryHoldTimePerVertex[VertexIndex] - WetnessUpdateInterval);
            continue;
        }

        float& Wetness = WetnessPerVertex[VertexIndex];
        if (Wetness > 0.0f)
        {
            const float VertexDryRate = VertexWetnessProfileParameters.IsValidIndex(VertexIndex)
                                            ? GetDryRatePerSecondForVertex(VertexIndex)
                                            : EffectiveDryRatePerSecond;
            const float DryMultiplier = FMath::Exp(
                -FMath::Max(0.0f, VertexDryRate) * WetnessUpdateInterval);
            const float OldWetness = Wetness;
            Wetness *= DryMultiplier;
            if (Wetness <= MinPendingWetnessAmount)
            {
                Wetness = 0.0f;
            }

            if (!FMath::IsNearlyEqual(OldWetness, Wetness))
            {
                DirtyWetVertexIndices.Add(VertexIndex);
                bDirty = true;
            }
        }
    }
}

bool UDynamicWetReceiverComponent::PreparePendingWetnessProcessing(
    const float EffectiveSpreadRatePerSecond,
    float&      OutSpreadAlpha,
    float&      OutGravityFlowStrength,
    bool&       bOutUseGravityBias,
    bool&       bOutCanSpread)
{
    if (WetnessPerVertex.Num() == 0)
    {
        return false;
    }

    if (Updating_Pending_Wetness_Amounts.Num() != WetnessPerVertex.Num() ||
        bPendingWetnessQueued.Num() != WetnessPerVertex.Num())
    {
        EnsureWetnessBufferSize(WetnessPerVertex.Num());
    }

    if (Updating_Pending_Wetness_Vertex_IndexQueue.Num() == 0)
    {
        return false;
    }

    OutGravityFlowStrength = GetGravityFlowStrength();
    bOutUseGravityBias = OutGravityFlowStrength > 0.0f;
    if (!bOutUseGravityBias)
    {
        for (const int32 VertexIndex : Updating_Pending_Wetness_Vertex_IndexQueue)
        {
            if (GetGravityFlowStrengthForVertex(VertexIndex) > 0.0f)
            {
                bOutUseGravityBias = true;
                break;
            }
        }
    }

    bOutCanSpread =
        NeighborGraph.Num() == WetnessPerVertex.Num();

    if (bOutUseGravityBias)
    {
        bOutUseGravityBias =
            UpdateSkinnedPositions() &&
            CachedSkinnedPositions.Num() == WetnessPerVertex.Num();
    }

    OutSpreadAlpha = FMath::Clamp(
        EffectiveSpreadRatePerSecond * WetnessUpdateInterval,
        0.0f,
        1.0f);

    return true;
}

void UDynamicWetReceiverComponent::SnapshotPendingWetnessForCurrentUpdate()
{
    Current_Pending_Wetness_Vertex_IndexQueue.Reset();
    Current_Pending_Wetness_Amounts.Reset();
    Swap(Current_Pending_Wetness_Vertex_IndexQueue, Updating_Pending_Wetness_Vertex_IndexQueue);
    Current_Pending_Wetness_Amounts.Reserve(Current_Pending_Wetness_Vertex_IndexQueue.Num());

    for (const int32 VertexIndex : Current_Pending_Wetness_Vertex_IndexQueue)
    {
        float PendingWater = 0.0f;
        if (Updating_Pending_Wetness_Amounts.IsValidIndex(VertexIndex))
        {
            PendingWater = Updating_Pending_Wetness_Amounts[VertexIndex];
            Updating_Pending_Wetness_Amounts[VertexIndex] = 0.0f;
        }

        Current_Pending_Wetness_Amounts.Add(PendingWater);

        if (bPendingWetnessQueued.IsValidIndex(VertexIndex))
        {
            bPendingWetnessQueued[VertexIndex] = false;
        }
    }
}

int32 UDynamicWetReceiverComponent::ProcessCurrentPendingWetness(
    bool&       bDirty,
    const float SpreadAlpha,
    const float GravityFlowStrength,
    const bool  bUseGravityBias,
    const bool  bCanSpread)
{
    (void)SpreadAlpha;
    (void)GravityFlowStrength;

    int32 QueueReadIndex = 0;
    int32 ProcessedVertices = 0;

    while (QueueReadIndex < Current_Pending_Wetness_Vertex_IndexQueue.Num() &&
           ProcessedVertices < MaxPendingWetnessVerticesPerUpdate)
    {
        const int32 VertexIndex = Current_Pending_Wetness_Vertex_IndexQueue[QueueReadIndex++];
        const int32 CurrentAmountIndex = QueueReadIndex - 1;
        ++ProcessedVertices;

        if (!WetnessPerVertex.IsValidIndex(VertexIndex) ||
            !Current_Pending_Wetness_Amounts.IsValidIndex(CurrentAmountIndex))
        {
            continue;
        }

        const float PendingWater = Current_Pending_Wetness_Amounts[CurrentAmountIndex];

        if (PendingWater <= MinPendingWetnessAmount)
        {
            continue;
        }

        const float SafeImmediateAbsorptionFraction = FMath::Clamp(
            CapillaryImmediateAbsorptionFraction,
            0.0f,
            MaxStoredWetness);

        const float DesiredAbsorption = PendingWater * SafeImmediateAbsorptionFraction;
        const float AbsorbedWetness = AbsorbWetnessAtVertex(VertexIndex, DesiredAbsorption, bDirty);
        const float OverflowWetness = FMath::Max(0.0f, DesiredAbsorption - AbsorbedWetness); // MaxStored를 넘어서 흡수하지 못하고 나온 양
        const float CapillaryWetness = FMath::Max(0.0f, PendingWater - DesiredAbsorption);   // MaxStored를 넘지는 않았지만 흡수력이 딸려서 흡수하지 못한 양
        const float SpreadableWetness = CapillaryWetness + OverflowWetness;
        const float VertexSpreadAlpha = FMath::Clamp(
            GetSpreadRatePerSecondForVertex(VertexIndex) * WetnessUpdateInterval,
            0.0f,
            1.0f);
        const float VertexGravityFlowStrength = GetGravityFlowStrengthForVertex(VertexIndex);

        if (!bCanSpread ||
            VertexSpreadAlpha <= 0.0f ||
            SpreadableWetness <= MinPendingWetnessAmount)
        {
            continue;
        }

        SpreadPendingWetnessToNeighbors(
            VertexIndex,
            SpreadableWetness,
            VertexSpreadAlpha,
            VertexGravityFlowStrength,
            bUseGravityBias && VertexGravityFlowStrength > 0.0f);
    }

    return QueueReadIndex;
}

void UDynamicWetReceiverComponent::SpreadPendingWetnessToNeighbors(
    const int32 VertexIndex,
    const float SpreadableWetness,
    const float SpreadAlpha,
    const float GravityFlowStrength,
    const bool  bUseGravityBias)
{
    if (!NeighborGraph.IsValidIndex(VertexIndex))
    {
        return;
    }

    const TArray<int32>& Neighbors = NeighborGraph[VertexIndex].Neighbors;
    if (Neighbors.Num() == 0)
    {
        return;
    }

    const FTransform ComponentTransform = TargetSkeletalMesh->GetComponentTransform();
    const FVector    GravityDirection = FVector::DownVector;

    TArray<float, TInlineAllocator<16>> NeighborWeights;
    NeighborWeights.SetNumZeroed(Neighbors.Num());
    float TotalWeight = 0.0f;

    for (int32 NeighborArrayIndex = 0; NeighborArrayIndex < Neighbors.Num(); ++NeighborArrayIndex)
    {
        const int32 NeighborIndex = Neighbors[NeighborArrayIndex];
        if (!WetnessPerVertex.IsValidIndex(NeighborIndex))
        {
            continue;
        }

        const float TargetCapacity = MaxStoredWetness - WetnessPerVertex[NeighborIndex];
        if (TargetCapacity <= MinPendingWetnessAmount)
        {
            continue;
        }

        const float GravityBias =
            bUseGravityBias
                ? CalculateNeighborGravityBias(
                      VertexIndex,
                      NeighborIndex,
                      GravityFlowStrength,
                      ComponentTransform,
                      GravityDirection)
                : 1.0f;

        float PartBoundaryScale = 1.0f;
        if (VertexWetPartIDs.IsValidIndex(VertexIndex) &&
            VertexWetPartIDs.IsValidIndex(NeighborIndex) &&
            VertexWetPartIDs[VertexIndex] != VertexWetPartIDs[NeighborIndex])
        {
            PartBoundaryScale = FMath::Clamp(CrossWetPartSpreadScale, 0.0f, 1.0f);
        }

        const float Weight = TargetCapacity * GravityBias * PartBoundaryScale;
        if (Weight <= KINDA_SMALL_NUMBER)
        {
            continue;
        }

        NeighborWeights[NeighborArrayIndex] = Weight;
        TotalWeight += Weight;
    }

    if (TotalWeight <= KINDA_SMALL_NUMBER)
    {
        return;
    }

    const float TotalFlowAmount = SpreadableWetness * SpreadAlpha;

    for (int32 NeighborArrayIndex = 0; NeighborArrayIndex < Neighbors.Num(); ++NeighborArrayIndex)
    {
        const float Weight = NeighborWeights[NeighborArrayIndex];
        if (Weight <= KINDA_SMALL_NUMBER)
        {
            continue;
        }

        const int32 NeighborIndex = Neighbors[NeighborArrayIndex];
        const float FlowAmount = TotalFlowAmount * (Weight / TotalWeight);

        if (FlowAmount > MinPendingWetnessAmount)
        {
            QueuePendingWetness(NeighborIndex, FlowAmount);
        }
    }
}

float UDynamicWetReceiverComponent::CalculateNeighborGravityBias(
    const int32       SourceVertexIndex,
    const int32       NeighborIndex,
    const float       GravityFlowStrength,
    const FTransform& ComponentTransform,
    const FVector&    GravityDirection) const
{
    if (!CachedSkinnedPositions.IsValidIndex(SourceVertexIndex) ||
        !CachedSkinnedPositions.IsValidIndex(NeighborIndex))
    {
        return 1.0f;
    }

    const FVector SourceWorldPosition = ComponentTransform.TransformPosition(
        FVector(CachedSkinnedPositions[SourceVertexIndex]));

    const FVector TargetWorldPosition = ComponentTransform.TransformPosition(
        FVector(CachedSkinnedPositions[NeighborIndex]));

    const FVector FlowDirection =
        (TargetWorldPosition - SourceWorldPosition).GetSafeNormal();

    const float GravityAlignment =
        FVector::DotProduct(FlowDirection, GravityDirection);

    return FMath::Clamp(
        1.0f + GravityAlignment * GravityFlowStrength,
        0.0f,
        2.0f);
}

void UDynamicWetReceiverComponent::RequeueUnprocessedPendingWetness(const int32 QueueReadIndex)
{
    for (int32 RemainingQueueIndex = QueueReadIndex;
         RemainingQueueIndex < Current_Pending_Wetness_Vertex_IndexQueue.Num();
         ++RemainingQueueIndex)
    {
        const int32 VertexIndex = Current_Pending_Wetness_Vertex_IndexQueue[RemainingQueueIndex];
        if (Current_Pending_Wetness_Amounts.IsValidIndex(RemainingQueueIndex))
        {
            QueuePendingWetness(VertexIndex, Current_Pending_Wetness_Amounts[RemainingQueueIndex]);
        }
    }
}

void UDynamicWetReceiverComponent::ProcessPendingWetness(bool& bDirty, const float EffectiveSpreadRatePerSecond)
{
    float SpreadAlpha = 0.0f;
    float GravityFlowStrength = 0.0f;
    bool  bUseGravityBias = false;
    bool  bCanSpread = false;

    if (!PreparePendingWetnessProcessing(
            EffectiveSpreadRatePerSecond,
            SpreadAlpha,
            GravityFlowStrength,
            bUseGravityBias,
            bCanSpread))
    {
        return;
    }

    SnapshotPendingWetnessForCurrentUpdate();

    const int32 QueueReadIndex = ProcessCurrentPendingWetness(
        bDirty,
        SpreadAlpha,
        GravityFlowStrength,
        bUseGravityBias,
        bCanSpread);

    RequeueUnprocessedPendingWetness(QueueReadIndex);

    Current_Pending_Wetness_Vertex_IndexQueue.Reset();
    Current_Pending_Wetness_Amounts.Reset();
}

void UDynamicWetReceiverComponent::UpdateWetness()
{
    bool        bDirty = false;
    const float EffectiveDryRatePerSecond = GetDryRatePerSecond();
    const float EffectiveSpreadRatePerSecond = GetSpreadRatePerSecond();

    if (Updating_Pending_Wetness_Vertex_IndexQueue.Num() > 0)
    {
        ProcessPendingWetness(bDirty, EffectiveSpreadRatePerSecond);
    }
    else
    {
        ClearPendingWetness();
    }

    DryOutWetness(bDirty, EffectiveDryRatePerSecond);

    if (bDirty)
    {
        ApplyWetnessToMaterial();
    }
}
void UDynamicWetReceiverComponent::ApplyWetnessToMaterial()
{
    FSkeletalMeshLODRenderData* LODData = nullptr;
    if (!GetLODRenderData(0, LODData))
    {
        return;
    }

    const int32 VertexCount = LODData->GetNumVertices();
    if (WetnessPerVertex.Num() != VertexCount)
    {
        // Vertex SafeCode
        EnsureWetnessBufferSize(VertexCount);
        DirtyWetVertexIndices.Reset();
        CachedWetVertexColors.Init(FLinearColor::Black, VertexCount);
        for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
        {
            DirtyWetVertexIndices.Add(VertexIndex);
        }
    }

    if (CachedWetVertexColors.Num() != VertexCount)
    {
        CachedWetVertexColors.Init(FLinearColor::Black, VertexCount);
        DirtyWetVertexIndices.Reset();
        for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
        {
            DirtyWetVertexIndices.Add(VertexIndex);
        }
    }

    if (DirtyWetVertexIndices.Num() == 0)
    {
        return;
    }

    for (int32 VertexIndex : DirtyWetVertexIndices)
    {
        if (!WetnessPerVertex.IsValidIndex(VertexIndex) ||
            !CachedWetVertexColors.IsValidIndex(VertexIndex))
        {
            continue;
        }

        const float SafeVisualSaturationWetness = FMath::Max(VisualSaturationWetness, KINDA_SMALL_NUMBER);
        const float Wetness = FMath::Clamp(
            WetnessPerVertex[VertexIndex] / SafeVisualSaturationWetness,
            0.0f,
            1.0f);

        CachedWetVertexColors[VertexIndex] = FLinearColor(
            Wetness,
            0.0f,
            0.0f,
            1.0f);
    }

    DirtyWetVertexIndices.Reset();

    TargetSkeletalMesh->SetVertexColorOverride_LinearColor(0, CachedWetVertexColors);
    TargetSkeletalMesh->MarkRenderStateDirty();
}

USkeletalMeshComponent* UDynamicWetReceiverComponent::ResolveTargetSkeletalMesh() const
{
    AActor* Owner = GetOwner();
    if (!Owner)
        return nullptr;

    TArray<USkeletalMeshComponent*> Meshes;
    Owner->GetComponents<USkeletalMeshComponent>(Meshes);

    if (!TargetSkeletalMeshName.IsNone())
    {
        for (USkeletalMeshComponent* Mesh : Meshes)
        {
            if (Mesh && Mesh->GetFName() == TargetSkeletalMeshName)
            {
                return Mesh;
            }
        }

        return nullptr;
    }

    return Meshes.Num() > 0 ? Meshes[0] : nullptr;
}

bool UDynamicWetReceiverComponent::UpdateSkinnedPositions()
{
    if (!TargetSkeletalMesh)
    {
        return false;
    }

    FSkeletalMeshLODRenderData* LODData = nullptr;
    if (!GetLODRenderData(0, LODData))
    {
        return false;
    }

    const FSkinWeightVertexBuffer* SkinWeightBuffer =
        TargetSkeletalMesh->GetSkinWeightBuffer(0);

    if (!SkinWeightBuffer)
    {
        UE_LOG(LogTemp, Warning, TEXT("DynamicWetReceiverComponent: SkinWeightBuffer is null."));
        return false;
    }

    TargetSkeletalMesh->CacheRefToLocalMatrices(
        CachedRefToLocalMatrices);

    CachedSkinnedPositions.Reset();

    USkeletalMeshComponent::ComputeSkinnedPositions(
        TargetSkeletalMesh,
        CachedSkinnedPositions,
        CachedRefToLocalMatrices,
        *LODData,
        *SkinWeightBuffer);

    return CachedSkinnedPositions.Num() > 0;
}

bool UDynamicWetReceiverComponent::UpdateSkinnedNormals()
{
    if (!TargetSkeletalMesh)
    {
        return false;
    }

    FSkeletalMeshLODRenderData* LODData = nullptr;
    if (!GetLODRenderData(0, LODData))
    {
        return false;
    }

    const FSkinWeightVertexBuffer* SkinWeightBuffer =
        TargetSkeletalMesh->GetSkinWeightBuffer(0);

    if (!SkinWeightBuffer)
    {
        UE_LOG(LogTemp, Warning, TEXT("DynamicWetReceiverComponent: SkinWeightBuffer is null."));
        return false;
    }

    TargetSkeletalMesh->CacheRefToLocalMatrices(
        CachedRefToLocalMatrices);

    const int32 VertexCount = LODData->GetNumVertices();
    if (VertexCount <= 0)
    {
        CachedSkinnedNormals.Reset();
        return false;
    }

    CachedSkinnedNormals.Reset();
    CachedSkinnedNormals.SetNumZeroed(VertexCount);

    const uint32 MaxInfluences = SkinWeightBuffer->GetMaxBoneInfluences();
    const float  BoneWeightScale =
        SkinWeightBuffer->GetBoneWeightByteSize() == 1 ? 255.0f : 65535.0f;

    for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
    {
        const FVector3f LocalNormal =
            LODData->StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(VertexIndex).GetSafeNormal();

        if (LocalNormal.IsNearlyZero())
        {
            continue;
        }

        FVector3f SkinnedNormal = FVector3f::ZeroVector;

        for (uint32 InfluenceIndex = 0; InfluenceIndex < MaxInfluences; ++InfluenceIndex)
        {
            const uint16 BoneWeight =
                SkinWeightBuffer->GetBoneWeight(VertexIndex, InfluenceIndex);

            if (BoneWeight == 0)
            {
                continue;
            }

            const uint32 BoneIndex =
                SkinWeightBuffer->GetBoneIndex(VertexIndex, InfluenceIndex);

            if (!CachedRefToLocalMatrices.IsValidIndex(BoneIndex))
            {
                continue;
            }

            const float     Weight = static_cast<float>(BoneWeight) / BoneWeightScale;
            const FVector4f SkinnedNormal4f =
                CachedRefToLocalMatrices[BoneIndex].TransformVector(LocalNormal);

            SkinnedNormal += FVector3f(
                                 SkinnedNormal4f.X,
                                 SkinnedNormal4f.Y,
                                 SkinnedNormal4f.Z) *
                             Weight;
        }

        CachedSkinnedNormals[VertexIndex] = SkinnedNormal.GetSafeNormal();
    }

    return CachedSkinnedNormals.Num() == VertexCount;
}

bool UDynamicWetReceiverComponent::GetLODRenderData(int32 LODIndex, FSkeletalMeshLODRenderData*& OutLODData) const
{
    OutLODData = nullptr;

    if (!TargetSkeletalMesh)
    {
        return false;
    }

    USkeletalMesh* SkeletalMesh = TargetSkeletalMesh->GetSkeletalMeshAsset();
    if (!SkeletalMesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("DynamicWetReceiverComponent: SkeletalMeshAsset reference is null."));
        return false;
    }

    FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
    if (!RenderData || !RenderData->LODRenderData.IsValidIndex(LODIndex))
    {
        UE_LOG(LogTemp, Warning, TEXT("DynamicWetReceiverComponent: RenderData reference is null."));
        return false;
    }

    OutLODData = &RenderData->LODRenderData[LODIndex];
    return true;
}

void UDynamicWetReceiverComponent::ApplyWetnessGlobal(float Amount)
{
    if (WetnessPerVertex.Num() == 0 || FMath::IsNearlyZero(Amount))
    {
        return;
    }

    const float EffectiveAmount = Amount;
    if (FMath::IsNearlyZero(EffectiveAmount))
    {
        return;
    }

    bool bDirty = false;

    for (int32 VertexIndex = 0; VertexIndex < WetnessPerVertex.Num(); ++VertexIndex)
    {
        if (EffectiveAmount > 0.0f)
        {
            QueuePendingWetness(VertexIndex, EffectiveAmount * GetAbsorptionMultiplierForVertex(VertexIndex));
        }
        else
        {
            AbsorbWetnessAtVertex(VertexIndex, EffectiveAmount, bDirty);
        }
    }

    if (bDirty)
    {
        ApplyWetnessToMaterial();
    }
}

void UDynamicWetReceiverComponent::ApplyWetnessBelowHeight(float WaterSurfaceZ, float Amount)
{
    if (!TargetSkeletalMesh || FMath::IsNearlyZero(Amount))
    {
        return;
    }

    const float EffectiveAmount = Amount;

    if (FMath::IsNearlyZero(EffectiveAmount) || !UpdateSkinnedPositions())
    {
        return;
    }

    if (WetnessPerVertex.Num() != CachedSkinnedPositions.Num())
    {
        EnsureWetnessBufferSize(CachedSkinnedPositions.Num());
    }

    bool bDirty = false;
    const FTransform ComponentTransform = TargetSkeletalMesh->GetComponentTransform();

    for (int32 VertexIndex = 0; VertexIndex < CachedSkinnedPositions.Num(); ++VertexIndex)
    {
        const FVector WorldPosition =
            ComponentTransform.TransformPosition(
                FVector(CachedSkinnedPositions[VertexIndex]));

        if (WorldPosition.Z > WaterSurfaceZ)
        {
            continue;
        }

        if (EffectiveAmount > 0.0f)
        {
            QueuePendingWetness(VertexIndex, EffectiveAmount * GetAbsorptionMultiplierForVertex(VertexIndex));
        }
        else
        {
            if (WetnessPerVertex[VertexIndex] <= 0.0f)
            {
                continue;
            }

            AbsorbWetnessAtVertex(VertexIndex, EffectiveAmount, bDirty);
        }
    }

    if (bDirty)
    {
        ApplyWetnessToMaterial();
    }
}

bool UDynamicWetReceiverComponent::ApplyWetSurface(
    const FDWCWetSurfaceData& SurfaceData,
    const float              Amount,
    const bool               bApplyMaterial)
{
    if (!TargetSkeletalMesh ||
        FMath::IsNearlyZero(Amount) ||
        SurfaceData.SizeX < 2 ||
        SurfaceData.SizeY < 2 ||
        !SurfaceData.Bounds.IsValid)
    {
        return false;
    }

    const int32 ExpectedSampleCount = SurfaceData.SizeX * SurfaceData.SizeY;
    if (SurfaceData.SurfaceZ.Num() != ExpectedSampleCount ||
        SurfaceData.Valid.Num() != ExpectedSampleCount)
    {
        return false;
    }

    const float EffectiveAmount = Amount;

    if (FMath::IsNearlyZero(EffectiveAmount) || !UpdateSkinnedPositions())
    {
        return false;
    }

    if (WetnessPerVertex.Num() != CachedSkinnedPositions.Num())
    {
        EnsureWetnessBufferSize(CachedSkinnedPositions.Num());
    }

    bool             bDirty = false;
    bool             bQueuedWetness = false;
    const FTransform ComponentTransform = TargetSkeletalMesh->GetComponentTransform();

    for (int32 VertexIndex = 0; VertexIndex < CachedSkinnedPositions.Num(); ++VertexIndex)
    {
        if (!WetnessPerVertex.IsValidIndex(VertexIndex))
        {
            continue;
        }

        const FVector WorldPosition =
            ComponentTransform.TransformPosition(
                FVector(CachedSkinnedPositions[VertexIndex]));

        float SurfaceZ = 0.0f;
        if (!QueryWetSurfaceData(SurfaceData, WorldPosition, SurfaceZ) ||
            WorldPosition.Z > SurfaceZ)
        {
            continue;
        }

        if (EffectiveAmount > 0.0f)
        {
            const float VertexAmount = EffectiveAmount * GetAbsorptionMultiplierForVertex(VertexIndex);
            QueuePendingWetness(VertexIndex, VertexAmount);
            bQueuedWetness = true;
        }
        else
        {
            if (WetnessPerVertex[VertexIndex] <= 0.0f)
            {
                continue;
            }

            AbsorbWetnessAtVertex(VertexIndex, EffectiveAmount, bDirty);
        }
    }

    if (bDirty && bApplyMaterial)
    {
        ApplyWetnessToMaterial();
    }

    return bDirty || bQueuedWetness;
}

bool UDynamicWetReceiverComponent::ApplyRainWetness(
    const FVector& RainDirection,
    float          Amount,
    bool           bApplyMaterial)
{
    FDWCWetContact Contact;
    Contact.Amount = Amount;
    Contact.Location = TargetSkeletalMesh ? TargetSkeletalMesh->Bounds.Origin : FVector::ZeroVector;
    Contact.Radius = TargetSkeletalMesh ? TargetSkeletalMesh->Bounds.SphereRadius : 0.0f;
    Contact.Direction = RainDirection;
    Contact.Normal = -RainDirection.GetSafeNormal();

    return ApplyWetContact(Contact, bApplyMaterial);
}

bool UDynamicWetReceiverComponent::ApplyWetContact(const FDWCWetContact& Contact, bool bApplyMaterial)
{
    if (!TargetSkeletalMesh || FMath::IsNearlyZero(Contact.Amount))
    {
        return false;
    }

    const float EffectiveAmount = Contact.Amount;

    if (FMath::IsNearlyZero(EffectiveAmount) || !UpdateSkinnedPositions())
    {
        return false;
    }

    const bool bHasNormals = UpdateSkinnedNormals();

    if (WetnessPerVertex.Num() != CachedSkinnedPositions.Num())
    {
        EnsureWetnessBufferSize(CachedSkinnedPositions.Num());
    }

    const FTransform ComponentTransform = TargetSkeletalMesh->GetComponentTransform();
    const FVector SafeDirection =
        Contact.Direction.IsNearlyZero()
            ? FVector::ZeroVector
            : Contact.Direction.GetSafeNormal();
    const FVector SafeNormal =
        Contact.Normal.IsNearlyZero()
            ? FVector::ZeroVector
            : Contact.Normal.GetSafeNormal();
    const float SafeRadius = FMath::Max(Contact.Radius, KINDA_SMALL_NUMBER);
    const float ExposureMin = FMath::Min(RainExposureMin, RainExposureMax - KINDA_SMALL_NUMBER);
    const float ExposureMax = FMath::Max(RainExposureMax, ExposureMin + KINDA_SMALL_NUMBER);

    bool bDirty = false;
    bool bQueuedWetness = false;

    for (int32 VertexIndex = 0; VertexIndex < CachedSkinnedPositions.Num(); ++VertexIndex)
    {
        if (!WetnessPerVertex.IsValidIndex(VertexIndex) ||
            !DoesVertexMatchBoneName(VertexIndex, Contact.BoneName))
        {
            continue;
        }

        if ((EffectiveAmount > 0.0f && WetnessPerVertex[VertexIndex] >= MaxStoredWetness) ||
            (EffectiveAmount < 0.0f && WetnessPerVertex[VertexIndex] <= 0.0f))
        {
            continue;
        }

        const FVector WorldPosition =
            ComponentTransform.TransformPosition(
                FVector(CachedSkinnedPositions[VertexIndex]));
        const float Distance = FVector::Dist(WorldPosition, Contact.Location);
        if (Distance > SafeRadius)
        {
            continue;
        }

        float Influence = 1.0f - (Distance / SafeRadius);

        if (bHasNormals && CachedSkinnedNormals.IsValidIndex(VertexIndex))
        {
            const FVector WorldNormal =
                ComponentTransform.TransformVectorNoScale(
                                      FVector(CachedSkinnedNormals[VertexIndex]))
                    .GetSafeNormal();

            if (!WorldNormal.IsNearlyZero())
            {
                if (!SafeDirection.IsNearlyZero())
                {
                    const float Facing = FVector::DotProduct(WorldNormal, -SafeDirection);
                    Influence *= FMath::SmoothStep(ExposureMin, ExposureMax, Facing);
                }

                if (!SafeNormal.IsNearlyZero())
                {
                    Influence *= FMath::Clamp(FVector::DotProduct(WorldNormal, SafeNormal), 0.0f, 1.0f);
                }
            }
        }

        if (Influence <= KINDA_SMALL_NUMBER)
        {
            continue;
        }

        const float VertexAmount =
            (EffectiveAmount > 0.0f
                 ? EffectiveAmount * GetAbsorptionMultiplierForVertex(VertexIndex)
                 : EffectiveAmount) *
            Influence;
        if (VertexAmount > 0.0f)
        {
            QueuePendingWetness(VertexIndex, VertexAmount);
            bQueuedWetness = true;
        }
        else
        {
            AbsorbWetnessAtVertex(VertexIndex, VertexAmount, bDirty);
        }
    }

    if (bDirty && bApplyMaterial)
    {
        ApplyWetnessToMaterial();
    }

    return bDirty || bQueuedWetness;
}

bool UDynamicWetReceiverComponent::ApplyWetContacts(const TArray<FDWCWetContact>& Contacts, bool bApplyMaterial)
{
    bool bAppliedAny = false;

    for (const FDWCWetContact& Contact : Contacts)
    {
        bAppliedAny |= ApplyWetContact(Contact, false);
    }

    if (bAppliedAny && bApplyMaterial)
    {
        ApplyWetnessToMaterial();
    }

    return bAppliedAny;
}

bool UDynamicWetReceiverComponent::GetWetnessWorldBounds(FBox& OutBounds) const
{
    OutBounds = FBox(ForceInit);

    if (!TargetSkeletalMesh)
    {
        return false;
    }

    OutBounds = TargetSkeletalMesh->Bounds.GetBox();
    return OutBounds.IsValid && !OutBounds.GetExtent().IsNearlyZero();
}

bool UDynamicWetReceiverComponent::QueryWetSurfaceData(
    const FDWCWetSurfaceData& SurfaceData,
    const FVector&            WorldPosition,
    float&                    OutSurfaceZ) const
{
    OutSurfaceZ = 0.0f;

    if (SurfaceData.SizeX < 2 ||
        SurfaceData.SizeY < 2 ||
        !SurfaceData.Bounds.IsValid)
    {
        return false;
    }

    const FVector BoundsMin = SurfaceData.Bounds.Min;
    const FVector BoundsMax = SurfaceData.Bounds.Max;
    const float   BoundsSizeX = BoundsMax.X - BoundsMin.X;
    const float   BoundsSizeY = BoundsMax.Y - BoundsMin.Y;

    if (BoundsSizeX <= KINDA_SMALL_NUMBER ||
        BoundsSizeY <= KINDA_SMALL_NUMBER)
    {
        return false;
    }

    const float NormalizedX = FMath::Clamp((WorldPosition.X - BoundsMin.X) / BoundsSizeX, 0.0f, 1.0f);
    const float NormalizedY = FMath::Clamp((WorldPosition.Y - BoundsMin.Y) / BoundsSizeY, 0.0f, 1.0f);

    const float GridX = NormalizedX * static_cast<float>(SurfaceData.SizeX - 1);
    const float GridY = NormalizedY * static_cast<float>(SurfaceData.SizeY - 1);

    const int32 X0 = FMath::Clamp(FMath::FloorToInt(GridX), 0, SurfaceData.SizeX - 1);
    const int32 Y0 = FMath::Clamp(FMath::FloorToInt(GridY), 0, SurfaceData.SizeY - 1);
    const int32 X1 = FMath::Clamp(X0 + 1, 0, SurfaceData.SizeX - 1);
    const int32 Y1 = FMath::Clamp(Y0 + 1, 0, SurfaceData.SizeY - 1);

    if (!SurfaceData.IsValidSampleIndex(X0, Y0) ||
        !SurfaceData.IsValidSampleIndex(X1, Y0) ||
        !SurfaceData.IsValidSampleIndex(X0, Y1) ||
        !SurfaceData.IsValidSampleIndex(X1, Y1))
    {
        return false;
    }

    const float AlphaX = GridX - static_cast<float>(X0);
    const float AlphaY = GridY - static_cast<float>(Y0);

    const float Z00 = SurfaceData.SurfaceZ[SurfaceData.GetSampleIndex(X0, Y0)];
    const float Z10 = SurfaceData.SurfaceZ[SurfaceData.GetSampleIndex(X1, Y0)];
    const float Z01 = SurfaceData.SurfaceZ[SurfaceData.GetSampleIndex(X0, Y1)];
    const float Z11 = SurfaceData.SurfaceZ[SurfaceData.GetSampleIndex(X1, Y1)];

    const float Z0 = FMath::Lerp(Z00, Z10, AlphaX);
    const float Z1 = FMath::Lerp(Z01, Z11, AlphaX);

    OutSurfaceZ = FMath::Lerp(Z0, Z1, AlphaY);
    return true;
}

bool UDynamicWetReceiverComponent::DoesVertexMatchBoneName(const int32 VertexIndex, const FName BoneName) const
{
    if (BoneName.IsNone())
    {
        return true;
    }

    if (!TargetSkeletalMesh)
    {
        return false;
    }

    const USkeletalMesh* SkeletalMesh = TargetSkeletalMesh->GetSkeletalMeshAsset();
    if (!SkeletalMesh)
    {
        return false;
    }

    const int32 BoneIndex = SkeletalMesh->GetRefSkeleton().FindBoneIndex(BoneName);
    if (BoneIndex == INDEX_NONE)
    {
        return false;
    }

    const FSkinWeightVertexBuffer* SkinWeightBuffer =
        TargetSkeletalMesh->GetSkinWeightBuffer(0);
    if (!SkinWeightBuffer)
    {
        return false;
    }

    const uint32 MaxInfluences = SkinWeightBuffer->GetMaxBoneInfluences();
    for (uint32 InfluenceIndex = 0; InfluenceIndex < MaxInfluences; ++InfluenceIndex)
    {
        if (SkinWeightBuffer->GetBoneWeight(VertexIndex, InfluenceIndex) == 0)
        {
            continue;
        }

        if (static_cast<int32>(SkinWeightBuffer->GetBoneIndex(VertexIndex, InfluenceIndex)) == BoneIndex)
        {
            return true;
        }
    }

    return false;
}

void UDynamicWetReceiverComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}
