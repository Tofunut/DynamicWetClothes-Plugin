#include "WetClothing/TransparencyBake/SourceSnapshot/DWCBakeBlueprintSnapshotResolver.h"

#include "Components/DWCBakeComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "PreviewScene.h"

bool FDWCBakeBlueprintSnapshotResolver::BuildSnapshot(
    const TSubclassOf<AActor> BlueprintClass,
    FDWCBakeSnapshot&         OutSnapshot,
    FString&                  OutErrorMessage)
{
    OutSnapshot = FDWCBakeSnapshot();
    OutErrorMessage.Reset();

    if (BlueprintClass == nullptr)
    {
        OutErrorMessage = TEXT("No Blueprint class was provided for DWC bake snapshot.");
        return false;
    }

    if (BlueprintClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
    {
        OutErrorMessage = FString::Printf(TEXT("Blueprint class '%s' cannot be used for DWC bake snapshot."), *GetNameSafe(BlueprintClass.Get()));
        return false;
    }

    FPreviewScene PreviewScene(
        FPreviewScene::ConstructionValues()
            .SetCreateDefaultLighting(false)
            .SetCreatePhysicsScene(false)
            .SetTransactional(false));

    UWorld* PreviewWorld = PreviewScene.GetWorld();
    if (PreviewWorld == nullptr)
    {
        OutErrorMessage = TEXT("Failed to create DWC bake preview world.");
        return false;
    }

    FActorSpawnParameters SpawnParameters;
    SpawnParameters.Name = MakeUniqueObjectName(PreviewWorld, BlueprintClass, TEXT("DWC_BakeSnapshotPreviewActor"));
    SpawnParameters.ObjectFlags = RF_Transient;
    SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    SpawnParameters.bTemporaryEditorActor = true;

    AActor* PreviewActor = PreviewWorld->SpawnActor<AActor>(BlueprintClass, FTransform::Identity, SpawnParameters);
    if (PreviewActor == nullptr)
    {
        OutErrorMessage = FString::Printf(TEXT("Failed to spawn Blueprint class '%s' for DWC bake snapshot."), *GetNameSafe(BlueprintClass.Get()));
        return false;
    }

    UDWCBakeComponent* BakeComponent = FindBakeComponent(*PreviewActor, OutErrorMessage);
    if (BakeComponent == nullptr)
    {
        return false;
    }

    if (!BakeComponent->BuildBakeSnapshot(OutSnapshot))
    {
        OutErrorMessage = FString::Printf(
            TEXT("Failed to build DWC bake snapshot from Blueprint class '%s'. Check that every bake layer references a valid SkeletalMeshComponent."),
            *GetNameSafe(BlueprintClass.Get()));
        return false;
    }

    return true;
}

UDWCBakeComponent* FDWCBakeBlueprintSnapshotResolver::FindBakeComponent(AActor& Actor, FString& OutErrorMessage)
{
    TArray<UDWCBakeComponent*> BakeComponents;
    Actor.GetComponents<UDWCBakeComponent>(BakeComponents);

    if (BakeComponents.Num() == 0)
    {
        OutErrorMessage = FString::Printf(TEXT("Blueprint class '%s' has no DWC Bake Component."), *GetNameSafe(Actor.GetClass()));
        return nullptr;
    }

    if (BakeComponents.Num() > 1)
    {
        OutErrorMessage = FString::Printf(
            TEXT("Blueprint class '%s' has multiple DWC Bake Components. Keep exactly one bake component per wet clothing source."),
            *GetNameSafe(Actor.GetClass()));
        return nullptr;
    }

    return BakeComponents[0];
}
