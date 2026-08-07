//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/** Shared quantization policy for UV edge identity across DWC editor tools. */
struct FDWCQuantizedUVPoint
{
    static constexpr double QuantizeScale = 100000.0;

    int64 X = 0;
    int64 Y = 0;

    FDWCQuantizedUVPoint() = default;

    explicit FDWCQuantizedUVPoint(const FVector2D& UV)
        : X(FMath::RoundToInt64(UV.X * QuantizeScale))
        , Y(FMath::RoundToInt64(UV.Y * QuantizeScale))
    {
    }

    bool operator==(const FDWCQuantizedUVPoint& Other) const
    {
        return X == Other.X && Y == Other.Y;
    }

    bool operator<(const FDWCQuantizedUVPoint& Other) const
    {
        return X != Other.X ? X < Other.X : Y < Other.Y;
    }
};

FORCEINLINE uint32 GetTypeHash(const FDWCQuantizedUVPoint& Point)
{
    const auto HashInt64 = [](const int64 Value)
    {
        const uint64 UnsignedValue = static_cast<uint64>(Value);
        return HashCombine(
            ::GetTypeHash(static_cast<uint32>(UnsignedValue & 0xffffffffull)),
            ::GetTypeHash(static_cast<uint32>(UnsignedValue >> 32)));
    };

    return HashCombine(HashInt64(Point.X), HashInt64(Point.Y));
}

/** Direction-independent UV edge key. */
struct FDWCCanonicalUVEdge
{
    FDWCQuantizedUVPoint A;
    FDWCQuantizedUVPoint B;

    FDWCCanonicalUVEdge() = default;

    FDWCCanonicalUVEdge(const FVector2D& InA, const FVector2D& InB)
        : A(InA)
        , B(InB)
    {
        if (B < A)
        {
            Swap(A, B);
        }
    }

    bool operator==(const FDWCCanonicalUVEdge& Other) const
    {
        return A == Other.A && B == Other.B;
    }

    bool IsForward(const FVector2D& Start, const FVector2D& End) const
    {
        return FDWCQuantizedUVPoint(Start) == A && FDWCQuantizedUVPoint(End) == B;
    }
};

FORCEINLINE uint32 GetTypeHash(const FDWCCanonicalUVEdge& Edge)
{
    return HashCombine(GetTypeHash(Edge.A), GetTypeHash(Edge.B));
}
