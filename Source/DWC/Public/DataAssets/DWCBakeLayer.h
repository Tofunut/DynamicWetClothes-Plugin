#pragma once

#include "CoreMinimal.h"
#include "Runtime/Engine/Classes/Engine/EngineTypes.h"

#include "DWCBakeLayer.generated.h"

class UMaterialInterface;
class USkeletalMesh;

UENUM(BlueprintType)
enum class EDWCBakeSourceType : uint8
{
    Unknown UMETA(DisplayName = "Unknown"),
    BlueprintPreview UMETA(DisplayName = "Blueprint Preview"),
    PlacedActor UMETA(DisplayName = "Placed Actor"),
    ComponentTemplate UMETA(DisplayName = "Component Template")
};

USTRUCT(BlueprintType)
struct FDWCBakeSourceContext
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DWC|Bake")
    EDWCBakeSourceType SourceType = EDWCBakeSourceType::Unknown;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DWC|Bake")
    FSoftObjectPath SourceObjectPath;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DWC|Bake")
    FString SourceDisplayName;
};

USTRUCT(BlueprintType)
struct FDWCBakeLayer
{
    GENERATED_BODY()

    //Id is name of Layer which is for human readable
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
    FName LayerId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
    FComponentReference ComponentReference;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
    int32 LayerOrder = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reveal")
    bool bCanBeRevealSource = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reveal")
    bool bCanBeWetOuterLayer = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reveal")
    bool bBlocksReveal = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reveal", meta = (ClampMin = "0.0"))
    float MaxRevealDistance = 5.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UV", meta = (ClampMin = "0"))
    int32 OuterUVChannel = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UV", meta = (ClampMin = "0"))
    int32 SourceUVChannel = 0;
};

USTRUCT(BlueprintType)
struct FDWCBakeResolvedLayer
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DWC|Bake")
    FName LayerId;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DWC|Bake")
    int32 LayerOrder = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DWC|Bake")
    FName ComponentDisplayName;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DWC|Bake")
    FString ComponentPath;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DWC|Bake")
    TObjectPtr<USkeletalMesh> SkeletalMesh = nullptr;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DWC|Bake")
    TArray<TObjectPtr<UMaterialInterface>> Materials;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DWC|Bake")
    FTransform BakeTransform = FTransform::Identity;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DWC|Bake")
    bool bCanBeRevealSource = true;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DWC|Bake")
    bool bCanBeWetOuterLayer = true;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DWC|Bake")
    bool bBlocksReveal = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DWC|Bake")
    float MaxRevealDistance = 5.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DWC|Bake")
    int32 OuterUVChannel = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DWC|Bake")
    int32 SourceUVChannel = 0;
};

USTRUCT(BlueprintType)
struct FDWCBakeSnapshot
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DWC|Bake")
    FDWCBakeSourceContext SourceContext;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DWC|Bake")
    TArray<FDWCBakeResolvedLayer> Layers;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DWC|Bake")
    FGuid SnapshotGuid;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DWC|Bake")
    FString BuildSignature;
};
