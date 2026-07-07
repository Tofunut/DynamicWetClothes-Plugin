#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WetWrinkleAsset.generated.h"

class USkeletalMesh;
class UTexture;
class UTexture2D;
class UWetClothingAsset;

USTRUCT(BlueprintType)
struct DWC_API FWetWrinkleStamp
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Wrinkle Stamp")
    int32 MaterialSlotIndex = INDEX_NONE;

    UPROPERTY(EditAnywhere, Category = "Wrinkle Stamp")
    int32 UVChannelIndex = 0;

    UPROPERTY(EditAnywhere, Category = "Wrinkle Stamp")
    TObjectPtr<UTexture> SourceTexture = nullptr;

    UPROPERTY(EditAnywhere, Category = "Wrinkle Stamp")
    FVector2D PositionUV = FVector2D::ZeroVector;

    UPROPERTY(EditAnywhere, Category = "Wrinkle Stamp", meta = (ClampMin = "0.0"))
    float BrushRadiusUV = 0.025f;

    UPROPERTY(EditAnywhere, Category = "Wrinkle Stamp")
    float RotationRadians = 0.0f;

    UPROPERTY(EditAnywhere, Category = "Wrinkle Stamp")
    FVector2D Scale = FVector2D(1.0f, 1.0f);

    UPROPERTY(EditAnywhere, Category = "Wrinkle Stamp", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float Strength = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Wrinkle Stamp", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float Falloff = 0.5f;

    UPROPERTY(EditAnywhere, Category = "Wrinkle Stamp")
    TObjectPtr<UTexture2D> BrushNormalTexture = nullptr;

    UPROPERTY(EditAnywhere, Category = "Wrinkle Stamp")
    int32 AffectedWetPartID = INDEX_NONE;

#if WITH_EDITORONLY_DATA
    UPROPERTY(VisibleAnywhere, Category = "Wrinkle Stamp|Editor Preview")
    bool bHasEditorSurface = false;

    UPROPERTY(VisibleAnywhere, Category = "Wrinkle Stamp|Editor Preview")
    FVector EditorSurfaceLocalPosition = FVector::ZeroVector;

    UPROPERTY(VisibleAnywhere, Category = "Wrinkle Stamp|Editor Preview")
    FVector EditorSurfaceLocalNormal = FVector::UpVector;

    UPROPERTY(VisibleAnywhere, Category = "Wrinkle Stamp|Editor Preview")
    FVector EditorSurfaceLocalTangent = FVector::ForwardVector;

    UPROPERTY(VisibleAnywhere, Category = "Wrinkle Stamp|Editor Preview")
    FVector EditorSurfaceLocalBitangent = FVector::RightVector;
#endif
};

USTRUCT(BlueprintType)
struct DWC_API FWetWrinkleStroke
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Wrinkle Stroke")
    FGuid StrokeGuid;

    UPROPERTY(EditAnywhere, Category = "Wrinkle Stroke")
    FString Name;

    UPROPERTY(EditAnywhere, Category = "Wrinkle Stroke")
    bool bEnabled = true;

    UPROPERTY(EditAnywhere, Category = "Wrinkle Stroke")
    TArray<FWetWrinkleStamp> Stamps;
};

USTRUCT(BlueprintType)
struct DWC_API FWetWrinkleBakedNormalMap
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Wrinkle Normal Map")
    TObjectPtr<UTexture> SourceTexture = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Wrinkle Normal Map")
    int32 UVChannelIndex = 0;

    UPROPERTY(VisibleAnywhere, Category = "Wrinkle Normal Map")
    TArray<int32> MaterialSlotIndices;

    UPROPERTY(VisibleAnywhere, Category = "Wrinkle Normal Map")
    TObjectPtr<UTexture2D> WrinkleNormalMap = nullptr;

    UPROPERTY(EditAnywhere, Category = "Wrinkle Normal Map", meta = (ClampMin = "16", ClampMax = "8192"))
    int32 Resolution = 1024;

    UPROPERTY(EditAnywhere, Category = "Wrinkle Normal Map", meta = (ClampMin = "0", ClampMax = "64"))
    int32 PaddingPixels = 4;

    UPROPERTY(VisibleAnywhere, Category = "Wrinkle Normal Map")
    FString BuildSignature;

    UPROPERTY(VisibleAnywhere, Category = "Wrinkle Normal Map")
    FGuid BakeGuid;
};

UCLASS(BlueprintType)
class DWC_API UWetWrinkleAsset : public UDataAsset
{
    GENERATED_BODY()

  public:
    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle")
    TObjectPtr<USkeletalMesh> TargetMesh = nullptr;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle")
    TObjectPtr<UWetClothingAsset> SourceWetClothingAsset = nullptr;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle|Strokes")
    TArray<FWetWrinkleStroke> Strokes;

    UPROPERTY(VisibleAnywhere, Category = "Wet Wrinkle|Baked Normal Maps")
    TArray<FWetWrinkleBakedNormalMap> BakedNormalMaps;

#if WITH_EDITORONLY_DATA
    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle|Bake", meta = (ClampMin = "16", ClampMax = "8192"))
    int32 DefaultBakeResolution = 1024;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle|Bake", meta = (ClampMin = "0", ClampMax = "64"))
    int32 DefaultPaddingPixels = 4;
#endif
};
