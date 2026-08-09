//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Spatial/DWCEditorSurfaceOrientationPolicy.h"

#include "WetClothing/Foundation/Spatial/DWCEditorSurfacePatchProjectionVersion.h"

namespace
{
    bool IsFiniteVector(const FVector3f& Value)
    {
        return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
    }

    FVector3f ChooseOrthogonalSecondaryAxis(const FVector3f& PrimaryAxis)
    {
        const FVector3f Candidates[] = {
            FVector3f(1.0f, 0.0f, 0.0f),
            FVector3f(0.0f, 1.0f, 0.0f),
            FVector3f(0.0f, 0.0f, 1.0f)
        };

        const FVector3f* BestCandidate = &Candidates[0];
        float BestAlignment = FMath::Abs(FVector3f::DotProduct(PrimaryAxis, *BestCandidate));
        for (int32 CandidateIndex = 1; CandidateIndex < UE_ARRAY_COUNT(Candidates); ++CandidateIndex)
        {
            const float Alignment = FMath::Abs(
                FVector3f::DotProduct(PrimaryAxis, Candidates[CandidateIndex]));
            if (Alignment < BestAlignment)
            {
                BestAlignment = Alignment;
                BestCandidate = &Candidates[CandidateIndex];
            }
        }
        return *BestCandidate;
    }

    uint32 HashFloat(const float Value)
    {
        if (Value == 0.0f)
        {
            return 0;
        }
        uint32 Bits = 0;
        static_assert(sizeof(Bits) == sizeof(Value));
        FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
        return Bits;
    }

    uint32 HashVector(const FVector3f& Value)
    {
        uint32 Hash = HashFloat(Value.X);
        Hash = HashCombine(Hash, HashFloat(Value.Y));
        return HashCombine(Hash, HashFloat(Value.Z));
    }
}

void FDWCEditorSurfaceOrientationPolicy::Normalize()
{
    if (!IsFiniteVector(PrimaryAxis) || PrimaryAxis.IsNearlyZero())
    {
        PrimaryAxis = DWCEditorSurfaceOrientationDefaults::PrimaryAxis;
    }
    PrimaryAxis.Normalize();

    if (!IsFiniteVector(SecondaryAxis) || SecondaryAxis.IsNearlyZero())
    {
        SecondaryAxis = DWCEditorSurfaceOrientationDefaults::SecondaryAxis;
    }
    SecondaryAxis -= PrimaryAxis * FVector3f::DotProduct(SecondaryAxis, PrimaryAxis);
    if (SecondaryAxis.IsNearlyZero())
    {
        SecondaryAxis = ChooseOrthogonalSecondaryAxis(PrimaryAxis);
        SecondaryAxis -= PrimaryAxis * FVector3f::DotProduct(SecondaryAxis, PrimaryAxis);
    }
    SecondaryAxis.Normalize();

    if (!FMath::IsFinite(FallbackFullQuality) || !FMath::IsFinite(FallbackBeginQuality))
    {
        FallbackFullQuality = DWCEditorSurfaceOrientationDefaults::FallbackFullQuality;
        FallbackBeginQuality = DWCEditorSurfaceOrientationDefaults::FallbackBeginQuality;
    }
    FallbackFullQuality = FMath::Clamp(FallbackFullQuality, 0.0f, 1.0f);
    FallbackBeginQuality = FMath::Clamp(FallbackBeginQuality, 0.0f, 1.0f);
    if (FallbackBeginQuality <= FallbackFullQuality + UE_KINDA_SMALL_NUMBER)
    {
        FallbackFullQuality = DWCEditorSurfaceOrientationDefaults::FallbackFullQuality;
        FallbackBeginQuality = DWCEditorSurfaceOrientationDefaults::FallbackBeginQuality;
    }
}

bool FDWCEditorSurfaceOrientationPolicy::IsValid() const
{
    return IsFiniteVector(PrimaryAxis) && IsFiniteVector(SecondaryAxis) &&
        FMath::IsNearlyEqual(PrimaryAxis.SizeSquared(), 1.0f, 0.001f) &&
        FMath::IsNearlyEqual(SecondaryAxis.SizeSquared(), 1.0f, 0.001f) &&
        FMath::IsNearlyZero(FVector3f::DotProduct(PrimaryAxis, SecondaryAxis), 0.001f) &&
        FMath::IsFinite(FallbackFullQuality) && FMath::IsFinite(FallbackBeginQuality) &&
        FallbackFullQuality >= 0.0f && FallbackBeginQuality <= 1.0f &&
        FallbackFullQuality < FallbackBeginQuality;
}

uint32 FDWCEditorSurfaceOrientationPolicy::BuildSignature() const
{
    if (!IsValid())
    {
        return 0;
    }

    uint32 Hash = GetTypeHash(DWCEditorSurfaceOrientationVersion::Policy);
    Hash = HashCombine(Hash, HashVector(PrimaryAxis));
    Hash = HashCombine(Hash, HashVector(SecondaryAxis));
    Hash = HashCombine(Hash, HashFloat(FallbackFullQuality));
    Hash = HashCombine(Hash, HashFloat(FallbackBeginQuality));
    return Hash != 0 ? Hash : 1u;
}
