//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Runtime/Engine/Classes/Engine/EngineTypes.h"

#include "DWCBakeLayer.generated.h"

class UMaterialInterface;
class USkeletalMesh;

UENUM()
enum class EDWCBakeSourceType : uint8
{
    Unknown UMETA(DisplayName = "Unknown"),
    BlueprintPreview UMETA(DisplayName = "Blueprint Preview"),
    PlacedActor UMETA(DisplayName = "Placed Actor"),
    ComponentTemplate UMETA(DisplayName = "Component Template")
};

USTRUCT()
struct FDWCBakeSourceContext
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "DWC|Bake")
    EDWCBakeSourceType SourceType = EDWCBakeSourceType::Unknown;

    UPROPERTY(VisibleAnywhere, Category = "DWC|Bake")
    FSoftObjectPath SourceObjectPath;

    UPROPERTY(VisibleAnywhere, Category = "DWC|Bake")
    FString SourceDisplayName;
};

USTRUCT()
struct FDWCBakeLayer
{
    GENERATED_BODY()

    //Id is name of Layer which is for human readable
    UPROPERTY(EditAnywhere, Category = "Layer")
    FName LayerId;

    UPROPERTY(EditAnywhere, Category = "Layer")
    FComponentReference ComponentReference;

    UPROPERTY(EditAnywhere, Category = "Layer")
    int32 LayerOrder = 0;

    UPROPERTY(EditAnywhere, Category = "Reveal")
    bool bCanBeRevealSource = true;

    UPROPERTY(EditAnywhere, Category = "Reveal")
    bool bCanBeWetOuterLayer = true;

    UPROPERTY(EditAnywhere, Category = "Reveal")
    bool bBlocksReveal = false;

    UPROPERTY(EditAnywhere, Category = "Reveal", meta = (ClampMin = "0.0"))
    float MaxRevealDistance = 5.0f;

    UPROPERTY(EditAnywhere, Category = "UV", meta = (ClampMin = "0"))
    int32 SourceUVChannel = 0;
};

USTRUCT()
struct FDWCBakeResolvedLayer
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "DWC|Bake")
    FName LayerId;

    UPROPERTY(VisibleAnywhere, Category = "DWC|Bake")
    int32 LayerOrder = 0;

    UPROPERTY(VisibleAnywhere, Category = "DWC|Bake")
    FName ComponentDisplayName;

    UPROPERTY(VisibleAnywhere, Category = "DWC|Bake")
    FString ComponentPath;

    UPROPERTY(VisibleAnywhere, Category = "DWC|Bake")
    TObjectPtr<USkeletalMesh> SkeletalMesh = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "DWC|Bake")
    TArray<TObjectPtr<UMaterialInterface>> Materials;

    UPROPERTY(VisibleAnywhere, Category = "DWC|Bake")
    FTransform BakeTransform = FTransform::Identity;

    UPROPERTY(VisibleAnywhere, Category = "DWC|Bake")
    bool bCanBeRevealSource = true;

    UPROPERTY(VisibleAnywhere, Category = "DWC|Bake")
    bool bCanBeWetOuterLayer = true;

    UPROPERTY(VisibleAnywhere, Category = "DWC|Bake")
    bool bBlocksReveal = false;

    UPROPERTY(VisibleAnywhere, Category = "DWC|Bake")
    float MaxRevealDistance = 5.0f;

    UPROPERTY(VisibleAnywhere, Category = "DWC|Bake")
    int32 SourceUVChannel = 0;
};

USTRUCT()
struct FDWCBakeSnapshot
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "DWC|Bake")
    FDWCBakeSourceContext SourceContext;

    UPROPERTY(VisibleAnywhere, Category = "DWC|Bake")
    TArray<FDWCBakeResolvedLayer> Layers;

    UPROPERTY(VisibleAnywhere, Category = "DWC|Bake")
    FGuid SnapshotGuid;

    UPROPERTY(VisibleAnywhere, Category = "DWC|Bake")
    FString BuildSignature;
};
