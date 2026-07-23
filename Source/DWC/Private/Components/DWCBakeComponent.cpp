#include "Components/DWCBakeComponent.h"

#include "Runtime/Engine/Classes/Components/SkeletalMeshComponent.h"
#include "Runtime/Engine/Classes/GameFramework/Actor.h"
#include "Runtime/Engine/Public/Materials/MaterialInterface.h"

UDWCBakeComponent::UDWCBakeComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

bool UDWCBakeComponent::BuildBakeSnapshot(FDWCBakeSnapshot& OutSnapshot) const
{
    OutSnapshot = FDWCBakeSnapshot();
    OutSnapshot.SourceContext = MakeSourceContext();
    OutSnapshot.SnapshotGuid = FGuid::NewGuid();

    TArray<FDWCBakeResolvedLayer> ResolvedLayers;
    ResolvedLayers.Reserve(Layers.Num());

    for (const FDWCBakeLayer& Layer : Layers)
    {
        FDWCBakeResolvedLayer ResolvedLayer;
        if (!ResolveLayer(Layer, ResolvedLayer))
        {
            return false;
        }

        ResolvedLayers.Add(ResolvedLayer);
    }

    ResolvedLayers.Sort(
        [](const FDWCBakeResolvedLayer& Left, const FDWCBakeResolvedLayer& Right)
        {
            if (Left.LayerOrder != Right.LayerOrder)
            {
                return Left.LayerOrder < Right.LayerOrder;
            }

            return Left.LayerId.LexicalLess(Right.LayerId);
        });

    OutSnapshot.BuildSignature = MakeBuildSignature(ResolvedLayers);
    OutSnapshot.Layers = MoveTemp(ResolvedLayers);
    return true;
}

bool UDWCBakeComponent::ResolveLayer(const FDWCBakeLayer& Layer, FDWCBakeResolvedLayer& OutResolvedLayer) const
{
    USkeletalMeshComponent* SkeletalMeshComponent = ResolveLayerComponent(Layer);
    if (SkeletalMeshComponent == nullptr)
    {
        return false;
    }

    USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->GetSkeletalMeshAsset();
    if (SkeletalMesh == nullptr)
    {
        return false;
    }

    //Save Actual Component Information.
    OutResolvedLayer.LayerId = Layer.LayerId;
    OutResolvedLayer.LayerOrder = Layer.LayerOrder;
    OutResolvedLayer.ComponentDisplayName = SkeletalMeshComponent->GetFName();
    OutResolvedLayer.ComponentPath = SkeletalMeshComponent->GetPathName(GetOwner());
    OutResolvedLayer.SkeletalMesh = SkeletalMesh;
    OutResolvedLayer.BakeTransform = MakeBakeTransform(*SkeletalMeshComponent);
    
    OutResolvedLayer.bCanBeRevealSource = Layer.bCanBeRevealSource;
    OutResolvedLayer.bCanBeWetOuterLayer = Layer.bCanBeWetOuterLayer;
    OutResolvedLayer.bBlocksReveal = Layer.bBlocksReveal;
    OutResolvedLayer.MaxRevealDistance = Layer.MaxRevealDistance;
    OutResolvedLayer.OuterUVChannel = Layer.OuterUVChannel;
    OutResolvedLayer.SourceUVChannel = Layer.SourceUVChannel;

    const int32 MaterialCount = SkeletalMeshComponent->GetNumMaterials();
    OutResolvedLayer.Materials.Reserve(MaterialCount);
    for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
    {
        OutResolvedLayer.Materials.Add(SkeletalMeshComponent->GetMaterial(MaterialIndex));
    }

    return true;
}

USkeletalMeshComponent* UDWCBakeComponent::ResolveLayerComponent(const FDWCBakeLayer& Layer) const
{
    AActor* Owner = GetOwner();
    if (Owner == nullptr)
    {
        return nullptr;
    }

    return Cast<USkeletalMeshComponent>(Layer.ComponentReference.GetComponent(Owner));
}

FTransform UDWCBakeComponent::MakeBakeTransform(const USceneComponent& SceneComponent) const
{
    const AActor* Owner = GetOwner();
    if (Owner == nullptr)
    {
        return SceneComponent.GetComponentTransform();
    }

    return SceneComponent.GetComponentTransform().GetRelativeTransform(Owner->GetActorTransform());
}

FDWCBakeSourceContext UDWCBakeComponent::MakeSourceContext() const
{
    FDWCBakeSourceContext SourceContext;
    SourceContext.SourceType = EDWCBakeSourceType::BlueprintPreview;

    const AActor* Owner = GetOwner();
    if (Owner != nullptr)
    {
        SourceContext.SourceDisplayName = Owner->GetName();
        SourceContext.SourceObjectPath = FSoftObjectPath(Owner->GetClass());
        if (Owner->GetWorld() != nullptr && Owner->GetWorld()->IsGameWorld())
        {
            SourceContext.SourceType = EDWCBakeSourceType::PlacedActor;
        }
    }
    else
    {
        SourceContext.SourceDisplayName = GetName();
        SourceContext.SourceObjectPath = FSoftObjectPath(GetClass());
        SourceContext.SourceType = EDWCBakeSourceType::ComponentTemplate;
    }

    return SourceContext;
}

FString UDWCBakeComponent::MakeBuildSignature(const TArray<FDWCBakeResolvedLayer>& ResolvedLayers) const
{
    FString Signature = TEXT("DWCBakeSnapshot_v1");

    for (const FDWCBakeResolvedLayer& Layer : ResolvedLayers)
    {
        Signature += FString::Printf(
            TEXT("|%s:%d:%s:%s:%d:%d:%d:%d:%d:%g"),
            *Layer.LayerId.ToString(),
            Layer.LayerOrder,
            *Layer.ComponentPath,
            *GetPathNameSafe(Layer.SkeletalMesh),
            Layer.bCanBeRevealSource ? 1 : 0,
            Layer.bCanBeWetOuterLayer ? 1 : 0,
            Layer.bBlocksReveal ? 1 : 0,
            Layer.OuterUVChannel,
            Layer.SourceUVChannel,
            Layer.MaxRevealDistance);

        for (const UMaterialInterface* Material : Layer.Materials)
        {
            Signature += FString::Printf(TEXT(":%s"), *GetPathNameSafe(Material));
        }

        Signature += FString::Printf(TEXT(":%s"), *Layer.BakeTransform.ToHumanReadableString());
    }

    return Signature;
}
