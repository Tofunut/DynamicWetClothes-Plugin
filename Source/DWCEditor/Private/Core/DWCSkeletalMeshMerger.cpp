#include "Core/DWCSkeletalMeshMerger.h"

#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "BoneWeights.h"
#include "ContentBrowserMenuContexts.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IAssetTools.h"
#include "MeshDescription.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "SkeletalMeshAttributes.h"
#include "SkeletalMeshOperations.h"
#include "StaticMeshOperations.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "DWCSkeletalMeshMerger"

namespace DWCSkeletalMeshMergerInternal
{
    DEFINE_LOG_CATEGORY_STATIC(LogDWCSkeletalMeshMerger, Log, All);

    void SetFailure(FDWCSkeletalMeshMergeResult& Result, const FString& Message)
    {
        Result.bSucceeded = false;
        Result.Message = Message;
    }

    void DiscardCreatedMesh(USkeletalMesh* Mesh)
    {
        if (Mesh == nullptr)
        {
            return;
        }

        Mesh->ClearFlags(RF_Public | RF_Standalone);
        Mesh->MarkAsGarbage();
    }

    FString BuildOutputPackageName(const USkeletalMesh* FirstSourceMesh)
    {
        FString UniquePackageName;
        FString UniqueAssetName;
        FAssetToolsModule::GetModule().Get().CreateUniqueAssetName(
            FirstSourceMesh->GetOutermost()->GetName(),
            TEXT("_Merged"),
            UniquePackageName,
            UniqueAssetName);
        return UniquePackageName;
    }

    FName MakeUniqueMaterialSlotName(const USkeletalMesh* SourceMesh, TSet<FName>& UsedSlotNames)
    {
        FString BaseName = ObjectTools::SanitizeObjectName(GetNameSafe(SourceMesh));
        if (BaseName.IsEmpty())
        {
            BaseName = TEXT("MergedPart");
        }

        FName Candidate(*BaseName);
        int32 Suffix = 2;
        while (UsedSlotNames.Contains(Candidate))
        {
            Candidate = FName(*FString::Printf(TEXT("%s_%02d"), *BaseName, Suffix));
            ++Suffix;
        }

        UsedSlotNames.Add(Candidate);
        return Candidate;
    }

    bool BuildBoneIndexRemapToOutputSkeleton(
        const FReferenceSkeleton& OutputRefSkeleton,
        const USkeletalMesh* SourceMesh,
        TArray<int32>& OutBoneIndexRemap,
        FString& OutReason)
    {
        OutBoneIndexRemap.Reset();
        if (SourceMesh == nullptr)
        {
            OutReason = TEXT("A source mesh is null.");
            return false;
        }

        const FReferenceSkeleton& SourceRefSkeleton = SourceMesh->GetRefSkeleton();
        OutBoneIndexRemap.SetNum(SourceRefSkeleton.GetRawBoneNum());
        for (int32 SourceBoneIndex = 0; SourceBoneIndex < SourceRefSkeleton.GetRawBoneNum(); ++SourceBoneIndex)
        {
            const FName SourceBoneName = SourceRefSkeleton.GetBoneName(SourceBoneIndex);
            const int32 OutputBoneIndex = OutputRefSkeleton.FindBoneIndex(SourceBoneName);
            if (OutputBoneIndex == INDEX_NONE)
            {
                OutReason = FString::Printf(
                    TEXT("reference skeleton bone %s from %s is missing from the shared Skeleton asset"),
                    *SourceBoneName.ToString(),
                    *GetNameSafe(SourceMesh));
                return false;
            }

            const int32 SourceParentIndex = SourceRefSkeleton.GetParentIndex(SourceBoneIndex);
            const int32 ExpectedOutputParentIndex = SourceParentIndex != INDEX_NONE
                ? OutputRefSkeleton.FindBoneIndex(SourceRefSkeleton.GetBoneName(SourceParentIndex))
                : INDEX_NONE;
            if (OutputRefSkeleton.GetParentIndex(OutputBoneIndex) != ExpectedOutputParentIndex)
            {
                OutReason = FString::Printf(
                    TEXT("reference skeleton hierarchy differs at bone %s in %s"),
                    *SourceBoneName.ToString(),
                    *GetNameSafe(SourceMesh));
                return false;
            }

            OutBoneIndexRemap[SourceBoneIndex] = OutputBoneIndex;
        }

        return true;
    }

    bool CloneLODMeshDescription(
        const USkeletalMesh* SourceMesh,
        const int32 LODIndex,
        FMeshDescription& OutMeshDescription,
        FString& OutError)
    {
        if (SourceMesh == nullptr || !SourceMesh->HasMeshDescription(LODIndex))
        {
            OutError = FString::Printf(
                TEXT("%s has no editable LOD %d Mesh Description."),
                *GetNameSafe(SourceMesh),
                LODIndex);
            return false;
        }

        if (!SourceMesh->CloneMeshDescription(LODIndex, OutMeshDescription))
        {
            OutError = FString::Printf(
                TEXT("Failed to read the LOD %d Mesh Description from %s."),
                LODIndex,
                *GetNameSafe(SourceMesh));
            return false;
        }

        FSkeletalMeshAttributes Attributes(OutMeshDescription);
        Attributes.Register(true);
        if (OutMeshDescription.Triangles().Num() <= 0)
        {
            OutError = FString::Printf(TEXT("%s LOD %d contains no triangles."), *GetNameSafe(SourceMesh), LODIndex);
            return false;
        }

        return true;
    }

    void SyncMeshDescriptionBonesToReferenceSkeleton(
        FMeshDescription& MeshDescription,
        const FReferenceSkeleton& ReferenceSkeleton)
    {
        FSkeletalMeshAttributes Attributes(MeshDescription);
        Attributes.Register(true);
        Attributes.Bones().Reset(ReferenceSkeleton.GetRawBoneNum());

        FSkeletalMeshAttributes::FBoneNameAttributesRef BoneNames = Attributes.GetBoneNames();
        FSkeletalMeshAttributes::FBoneParentIndexAttributesRef BoneParentIndices = Attributes.GetBoneParentIndices();
        FSkeletalMeshAttributes::FBonePoseAttributesRef BonePoses = Attributes.GetBonePoses();
        const TArray<FMeshBoneInfo>& RefBoneInfo = ReferenceSkeleton.GetRawRefBoneInfo();
        const TArray<FTransform>& RefBonePose = ReferenceSkeleton.GetRawRefBonePose();

        for (int32 BoneIndex = 0; BoneIndex < ReferenceSkeleton.GetRawBoneNum(); ++BoneIndex)
        {
            const FBoneID BoneID = Attributes.CreateBone();
            BoneNames.Set(BoneID, RefBoneInfo[BoneIndex].Name);
            BoneParentIndices.Set(BoneID, RefBoneInfo[BoneIndex].ParentIndex);
            BonePoses.Set(BoneID, RefBonePose[BoneIndex]);
        }
    }

    bool RemapMeshDescriptionSkinWeights(
        FMeshDescription& MeshDescription,
        const TArray<int32>& SourceToOutputBoneIndexMap,
        FString& OutError)
    {
        using namespace UE::AnimationCore;

        FSkeletalMeshAttributes Attributes(MeshDescription);
        Attributes.Register(true);

        for (const FName ProfileName : Attributes.GetSkinWeightProfileNames(false))
        {
            FSkinWeightsVertexAttributesRef SkinWeights = Attributes.GetVertexSkinWeights(ProfileName);
            if (!SkinWeights.IsValid())
            {
                continue;
            }

            for (const FVertexID VertexID : MeshDescription.Vertices().GetElementIDs())
            {
                const FVertexBoneWeights ExistingWeights = SkinWeights.Get(VertexID);
                TArray<FBoneWeight, TInlineAllocator<MaxInlineBoneWeightCount>> RemappedWeights;
                RemappedWeights.Reserve(ExistingWeights.Num());

                for (const FBoneWeight& BoneWeight : ExistingWeights)
                {
                    const int32 SourceBoneIndex = BoneWeight.GetBoneIndex();
                    if (!SourceToOutputBoneIndexMap.IsValidIndex(SourceBoneIndex))
                    {
                        OutError = FString::Printf(
                            TEXT("Skin weights reference invalid source bone index %d at vertex %d."),
                            SourceBoneIndex,
                            VertexID.GetValue());
                        return false;
                    }

                    const int32 OutputBoneIndex = SourceToOutputBoneIndexMap[SourceBoneIndex];
                    if (OutputBoneIndex < 0 || OutputBoneIndex > MAX_uint16)
                    {
                        OutError = FString::Printf(
                            TEXT("Cannot remap skin weight at vertex %d because output bone index %d is invalid."),
                            VertexID.GetValue(),
                            OutputBoneIndex);
                        return false;
                    }

                    RemappedWeights.Add(FBoneWeight(static_cast<FBoneIndexType>(OutputBoneIndex), BoneWeight.GetRawWeight()));
                }

                if (RemappedWeights.IsEmpty())
                {
                    RemappedWeights.Add(FBoneWeight(0, 1.0f));
                }

                SkinWeights.SetRaw(VertexID, RemappedWeights);
            }
        }

        return true;
    }

    bool CopySkinWeightsForAppendedVertices(
        const FMeshDescription& SourceMeshDescription,
        FMeshDescription& TargetMeshDescription,
        const TSet<FVertexID>& ExistingTargetVertexIDs,
        FString& OutError)
    {
        using namespace UE::AnimationCore;

        TArray<FVertexID> SourceVertexIDs;
        SourceVertexIDs.Reserve(SourceMeshDescription.Vertices().Num());
        for (const FVertexID SourceVertexID : SourceMeshDescription.Vertices().GetElementIDs())
        {
            SourceVertexIDs.Add(SourceVertexID);
        }
        SourceVertexIDs.Sort([](const FVertexID Left, const FVertexID Right)
        {
            return Left.GetValue() < Right.GetValue();
        });

        TArray<FVertexID> NewTargetVertexIDs;
        NewTargetVertexIDs.Reserve(SourceVertexIDs.Num());
        for (const FVertexID TargetVertexID : TargetMeshDescription.Vertices().GetElementIDs())
        {
            if (!ExistingTargetVertexIDs.Contains(TargetVertexID))
            {
                NewTargetVertexIDs.Add(TargetVertexID);
            }
        }
        NewTargetVertexIDs.Sort([](const FVertexID Left, const FVertexID Right)
        {
            return Left.GetValue() < Right.GetValue();
        });

        if (SourceVertexIDs.Num() != NewTargetVertexIDs.Num())
        {
            OutError = FString::Printf(
                TEXT("Failed to copy appended skin weights: source vertex count %d did not match appended vertex count %d."),
                SourceVertexIDs.Num(),
                NewTargetVertexIDs.Num());
            return false;
        }

        FSkeletalMeshConstAttributes SourceAttributes(SourceMeshDescription);
        FSkeletalMeshAttributes TargetAttributes(TargetMeshDescription);
        TargetAttributes.Register(true);

        for (const FName ProfileName : SourceAttributes.GetSkinWeightProfileNames(false))
        {
            FSkinWeightsVertexAttributesConstRef SourceSkinWeights = SourceAttributes.GetVertexSkinWeights(ProfileName);
            if (!SourceSkinWeights.IsValid())
            {
                continue;
            }

            FSkinWeightsVertexAttributesRef TargetSkinWeights = TargetAttributes.GetVertexSkinWeights(ProfileName);
            if (!TargetSkinWeights.IsValid() && !ProfileName.IsNone())
            {
                TargetAttributes.RegisterSkinWeightAttribute(ProfileName);
                TargetSkinWeights = TargetAttributes.GetVertexSkinWeights(ProfileName);
            }
            if (!TargetSkinWeights.IsValid())
            {
                OutError = FString::Printf(
                    TEXT("Failed to access target skin weight profile %s."),
                    *ProfileName.ToString());
                return false;
            }

            for (int32 VertexIndex = 0; VertexIndex < SourceVertexIDs.Num(); ++VertexIndex)
            {
                const FVertexBoneWeightsConst SourceWeights = SourceSkinWeights.Get(SourceVertexIDs[VertexIndex]);
                TArray<FBoneWeight, TInlineAllocator<MaxInlineBoneWeightCount>> CopiedWeights;
                CopiedWeights.Reserve(SourceWeights.Num());
                for (const FBoneWeight& BoneWeight : SourceWeights)
                {
                    CopiedWeights.Add(BoneWeight);
                }

                if (CopiedWeights.IsEmpty())
                {
                    CopiedWeights.Add(FBoneWeight(0, 1.0f));
                }

                TargetSkinWeights.SetRaw(NewTargetVertexIDs[VertexIndex], CopiedWeights);
            }
        }

        return true;
    }

    bool PrepareLODMeshDescriptionForOutputSkeleton(
        const USkeletalMesh* SourceMesh,
        const int32 LODIndex,
        const FReferenceSkeleton& OutputRefSkeleton,
        const TArray<int32>& SourceToOutputBoneIndexMap,
        FMeshDescription& OutMeshDescription,
        FString& OutError)
    {
        if (!CloneLODMeshDescription(SourceMesh, LODIndex, OutMeshDescription, OutError))
        {
            return false;
        }

        if (!RemapMeshDescriptionSkinWeights(OutMeshDescription, SourceToOutputBoneIndexMap, OutError))
        {
            return false;
        }

        SyncMeshDescriptionBonesToReferenceSkeleton(OutMeshDescription, OutputRefSkeleton);
        return true;
    }

    bool CollapseToSinglePolygonGroup(
        FMeshDescription& MeshDescription,
        const FName SlotName,
        FString& OutError)
    {
        TArray<FPolygonGroupID> PolygonGroupIDs;
        for (const FPolygonGroupID PolygonGroupID : MeshDescription.PolygonGroups().GetElementIDs())
        {
            PolygonGroupIDs.Add(PolygonGroupID);
        }

        if (PolygonGroupIDs.IsEmpty())
        {
            OutError = TEXT("The source mesh has no polygon groups.");
            return false;
        }

        const FPolygonGroupID TargetPolygonGroup = PolygonGroupIDs[0];
        for (const FPolygonID PolygonID : MeshDescription.Polygons().GetElementIDs())
        {
            MeshDescription.SetPolygonPolygonGroup(PolygonID, TargetPolygonGroup);
        }

        for (int32 PolygonGroupIndex = 1; PolygonGroupIndex < PolygonGroupIDs.Num(); ++PolygonGroupIndex)
        {
            MeshDescription.DeletePolygonGroup(PolygonGroupIDs[PolygonGroupIndex]);
        }

        FSkeletalMeshAttributes Attributes(MeshDescription);
        Attributes.Register(true);
        Attributes.GetPolygonGroupMaterialSlotNames()[TargetPolygonGroup] = SlotName;
        return true;
    }

    bool BuildMergedLOD(
        const TArray<USkeletalMesh*>& SourceMeshes,
        const TArray<FName>& OutputSlotNames,
        const FReferenceSkeleton& OutputRefSkeleton,
        const TArray<TArray<int32>>& SourceToOutputBoneIndexMaps,
        const int32 LODIndex,
        FMeshDescription& OutMergedDescription,
        FString& OutError)
    {
        if (!SourceMeshes.IsValidIndex(0) ||
            OutputSlotNames.Num() != SourceMeshes.Num() ||
            SourceToOutputBoneIndexMaps.Num() != SourceMeshes.Num())
        {
            OutError = TEXT("Invalid source mesh or material slot mapping.");
            return false;
        }

        if (!PrepareLODMeshDescriptionForOutputSkeleton(
                SourceMeshes[0],
                LODIndex,
                OutputRefSkeleton,
                SourceToOutputBoneIndexMaps[0],
                OutMergedDescription,
                OutError))
        {
            return false;
        }

        if (!CollapseToSinglePolygonGroup(OutMergedDescription, OutputSlotNames[0], OutError))
        {
            return false;
        }

        for (int32 SourceIndex = 1; SourceIndex < SourceMeshes.Num(); ++SourceIndex)
        {
            FMeshDescription SourceDescription;
            if (!PrepareLODMeshDescriptionForOutputSkeleton(
                    SourceMeshes[SourceIndex],
                    LODIndex,
                    OutputRefSkeleton,
                    SourceToOutputBoneIndexMaps[SourceIndex],
                    SourceDescription,
                    OutError))
            {
                return false;
            }

            FSkeletalMeshAttributes TargetAttributes(OutMergedDescription);
            TargetAttributes.Register(true);
            const FPolygonGroupID TargetPolygonGroup = OutMergedDescription.CreatePolygonGroup();
            TargetAttributes.GetPolygonGroupMaterialSlotNames()[TargetPolygonGroup] = OutputSlotNames[SourceIndex];

            FStaticMeshOperations::FAppendSettings AppendSettings;
            AppendSettings.bMergeObjectName = false;
            for (bool& bMergeUVChannel : AppendSettings.bMergeUVChannels)
            {
                bMergeUVChannel = true;
            }
            AppendSettings.bMergeVertexColor = true;
            AppendSettings.PolygonGroupsDelegate = FAppendPolygonGroupsDelegate::CreateLambda(
                [TargetPolygonGroup](
                    const FMeshDescription& SourceMeshDescription,
                    FMeshDescription&,
                    PolygonGroupMap& OutPolygonGroupMap)
                {
                    for (const FPolygonGroupID SourcePolygonGroupID :
                         SourceMeshDescription.PolygonGroups().GetElementIDs())
                    {
                        OutPolygonGroupMap.Add(SourcePolygonGroupID, TargetPolygonGroup);
                    }
                });

            TSet<FVertexID> ExistingTargetVertexIDs;
            ExistingTargetVertexIDs.Reserve(OutMergedDescription.Vertices().Num());
            for (const FVertexID TargetVertexID : OutMergedDescription.Vertices().GetElementIDs())
            {
                ExistingTargetVertexIDs.Add(TargetVertexID);
            }

            FStaticMeshOperations::AppendMeshDescription(
                SourceDescription,
                OutMergedDescription,
                AppendSettings);

            if (!CopySkinWeightsForAppendedVertices(
                    SourceDescription,
                    OutMergedDescription,
                    ExistingTargetVertexIDs,
                    OutError))
            {
                return false;
            }
        }

        if (OutMergedDescription.Triangles().Num() <= 0)
        {
            OutError = FString::Printf(TEXT("The merged LOD %d contains no triangles."), LODIndex);
            return false;
        }

        bool bInfluenceCountLimitHit = false;
        FSkeletalMeshOperations::ValidateAndFixInfluences(OutMergedDescription, bInfluenceCountLimitHit);
        if (bInfluenceCountLimitHit)
        {
            UE_LOG(
                LogDWCSkeletalMeshMerger,
                Warning,
                TEXT("Merged LOD %d contained vertices over the supported influence limit; influences were normalized/truncated by the engine."),
                LODIndex);
        }

        return true;
    }

    void RemoveMorphTargetsWithoutRebuild(USkeletalMesh* Mesh)
    {
        if (Mesh == nullptr || Mesh->GetMorphTargets().IsEmpty())
        {
            return;
        }

        Mesh->GetMorphTargets().Reset();
        Mesh->InitMorphTargets(false);
    }

    void NotifyResult(const FDWCSkeletalMeshMergeResult& Result)
    {
        if (!Result.bSucceeded)
        {
            UE_LOG(LogDWCSkeletalMeshMerger, Error, TEXT("%s"), *Result.Message);
            FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Result.Message));
            return;
        }

        UE_LOG(LogDWCSkeletalMeshMerger, Display, TEXT("%s"), *Result.Message);

        FNotificationInfo Notification(FText::FromString(Result.Message));
        Notification.ExpireDuration = 5.0f;
        Notification.bFireAndForget = true;
        Notification.Image = FAppStyle::GetBrush(TEXT("Icons.SuccessWithColor"));
        FSlateNotificationManager::Get().AddNotification(Notification);

        if (GEditor != nullptr && Result.OutputMesh != nullptr)
        {
            TArray<UObject*> AssetsToSync;
            AssetsToSync.Add(Result.OutputMesh);
            GEditor->SyncBrowserToObjects(AssetsToSync);
        }
    }

    void ExecuteMerge(const TArray<TWeakObjectPtr<USkeletalMesh>>& WeakSourceMeshes)
    {
        TArray<USkeletalMesh*> SourceMeshes;
        SourceMeshes.Reserve(WeakSourceMeshes.Num());
        for (const TWeakObjectPtr<USkeletalMesh>& WeakSourceMesh : WeakSourceMeshes)
        {
            if (USkeletalMesh* SourceMesh = WeakSourceMesh.Get())
            {
                SourceMeshes.Add(SourceMesh);
            }
        }

        NotifyResult(FDWCSkeletalMeshMerger::MergeMeshes(SourceMeshes));
    }
}

FDWCSkeletalMeshMergeResult FDWCSkeletalMeshMerger::MergeMeshes(
    const TArray<USkeletalMesh*>& SourceMeshes,
    const FString& OptionalOutputPackageName)
{
    using namespace DWCSkeletalMeshMergerInternal;

    FDWCSkeletalMeshMergeResult Result;

    TArray<USkeletalMesh*> ValidSourceMeshes = SourceMeshes;
    ValidSourceMeshes.RemoveAll([](const USkeletalMesh* SourceMesh)
    {
        return SourceMesh == nullptr;
    });

    if (ValidSourceMeshes.Num() < 2)
    {
        SetFailure(Result, TEXT("Select at least two Skeletal Mesh assets to merge."));
        return Result;
    }

    USkeletalMesh* FirstSourceMesh = ValidSourceMeshes[0];
    USkeleton* SharedSkeleton = FirstSourceMesh->GetSkeleton();
    if (SharedSkeleton == nullptr)
    {
        SetFailure(Result, FString::Printf(TEXT("%s has no Skeleton asset."), *GetNameSafe(FirstSourceMesh)));
        return Result;
    }
    const FReferenceSkeleton& OutputRefSkeleton = SharedSkeleton->GetReferenceSkeleton();

    TArray<FSkeletalMaterial> OutputMaterials;
    OutputMaterials.Reserve(ValidSourceMeshes.Num());
    TArray<FName> OutputSlotNames;
    OutputSlotNames.Reserve(ValidSourceMeshes.Num());
    TSet<FName> UsedSlotNames;
    TArray<TArray<int32>> SourceToOutputBoneIndexMaps;
    SourceToOutputBoneIndexMaps.Reserve(ValidSourceMeshes.Num());

    for (USkeletalMesh* SourceMesh : ValidSourceMeshes)
    {
        if (SourceMesh->GetSkeleton() != SharedSkeleton)
        {
            SetFailure(Result, FString::Printf(
                TEXT("Cannot merge %s because it uses a different Skeleton. All selected meshes must use %s."),
                *GetNameSafe(SourceMesh),
                *GetNameSafe(SharedSkeleton)));
            return Result;
        }

        FString SkeletonCompatibilityReason;
        TArray<int32>& SourceToOutputBoneIndexMap = SourceToOutputBoneIndexMaps.AddDefaulted_GetRef();
        if (!BuildBoneIndexRemapToOutputSkeleton(
                OutputRefSkeleton,
                SourceMesh,
                SourceToOutputBoneIndexMap,
                SkeletonCompatibilityReason))
        {
            SetFailure(Result, FString::Printf(
                TEXT("Cannot merge %s because its %s."),
                *GetNameSafe(SourceMesh),
                *SkeletonCompatibilityReason));
            return Result;
        }

        const TArray<FSkeletalMaterial>& SourceMaterials = SourceMesh->GetMaterials();
        if (SourceMaterials.Num() != 1)
        {
            SetFailure(Result, FString::Printf(
                TEXT("%s has %d material slots. This command requires exactly one material slot per source mesh. Use Extract Material Slot first."),
                *GetNameSafe(SourceMesh),
                SourceMaterials.Num()));
            return Result;
        }

        if (!SourceMesh->HasMeshDescription(0))
        {
            SetFailure(Result, FString::Printf(
                TEXT("%s has no editable LOD 0 source data and cannot be saved as a merged editor asset."),
                *GetNameSafe(SourceMesh)));
            return Result;
        }

        FSkeletalMaterial OutputMaterial = SourceMaterials[0];
        const FName SlotName = MakeUniqueMaterialSlotName(SourceMesh, UsedSlotNames);
        OutputMaterial.MaterialSlotName = SlotName;
        OutputMaterial.ImportedMaterialSlotName = SlotName;
        OutputMaterials.Add(MoveTemp(OutputMaterial));
        OutputSlotNames.Add(SlotName);
    }

    struct FMergedLODSourceData
    {
        int32 LODIndex = INDEX_NONE;
        FMeshDescription MeshDescription;
    };

    TArray<FMergedLODSourceData> MergedLODs;
    FString MergeError;
    const int32 OutputLODCount = FirstSourceMesh->GetLODNum();
    for (int32 LODIndex = 0; LODIndex < OutputLODCount; ++LODIndex)
    {
        const bool bFirstMeshHasImportedLOD = FirstSourceMesh->HasMeshDescription(LODIndex);
        for (USkeletalMesh* SourceMesh : ValidSourceMeshes)
        {
            if (SourceMesh->HasMeshDescription(LODIndex) != bFirstMeshHasImportedLOD)
            {
                SetFailure(Result, FString::Printf(
                    TEXT("Cannot merge LOD %d because %s and %s do not use the same imported/generated LOD layout."),
                    LODIndex,
                    *GetNameSafe(FirstSourceMesh),
                    *GetNameSafe(SourceMesh)));
                return Result;
            }
        }

        if (!bFirstMeshHasImportedLOD)
        {
            continue;
        }

        FMergedLODSourceData& MergedLOD = MergedLODs.AddDefaulted_GetRef();
        MergedLOD.LODIndex = LODIndex;
        if (!BuildMergedLOD(
                ValidSourceMeshes,
                OutputSlotNames,
                OutputRefSkeleton,
                SourceToOutputBoneIndexMaps,
                LODIndex,
                MergedLOD.MeshDescription,
                MergeError))
        {
            SetFailure(Result, MergeError);
            return Result;
        }
    }

    if (MergedLODs.IsEmpty() || MergedLODs[0].LODIndex != 0)
    {
        SetFailure(Result, TEXT("No merged LOD 0 source data was produced."));
        return Result;
    }

    FString OutputPackageName = OptionalOutputPackageName;
    if (OutputPackageName.IsEmpty())
    {
        OutputPackageName = BuildOutputPackageName(FirstSourceMesh);
    }
    else if (!FPackageName::IsValidLongPackageName(OutputPackageName))
    {
        SetFailure(Result, FString::Printf(TEXT("Invalid output package path: %s"), *OutputPackageName));
        return Result;
    }

    const FString OutputAssetName = FPackageName::GetLongPackageAssetName(OutputPackageName);
    UPackage* Package = CreatePackage(*OutputPackageName);
    if (Package == nullptr)
    {
        SetFailure(Result, FString::Printf(TEXT("Failed to create package: %s"), *OutputPackageName));
        return Result;
    }
    Package->FullyLoad();

    if (FindObject<USkeletalMesh>(Package, *OutputAssetName) != nullptr)
    {
        SetFailure(Result, FString::Printf(
            TEXT("Output asset already exists: %s.%s"),
            *OutputPackageName,
            *OutputAssetName));
        return Result;
    }

    USkeletalMesh* OutputMesh = DuplicateObject<USkeletalMesh>(FirstSourceMesh, Package, *OutputAssetName);
    if (OutputMesh == nullptr)
    {
        SetFailure(Result, FString::Printf(TEXT("Failed to create output Skeletal Mesh: %s"), *OutputPackageName));
        return Result;
    }

    OutputMesh->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
    OutputMesh->Modify();
    OutputMesh->SetSkeleton(SharedSkeleton);
    OutputMesh->SetRefSkeleton(OutputRefSkeleton);
    OutputMesh->CalculateInvRefMatrices();
    OutputMesh->SetMaterials(OutputMaterials);
    RemoveMorphTargetsWithoutRebuild(OutputMesh);

    USkeletalMesh::FCommitMeshDescriptionParams CommitParams;
    CommitParams.bForceUpdate = true;
    for (FMergedLODSourceData& MergedLOD : MergedLODs)
    {
        if (OutputMesh->CreateMeshDescription(MergedLOD.LODIndex, MoveTemp(MergedLOD.MeshDescription)) == nullptr)
        {
            DiscardCreatedMesh(OutputMesh);
            SetFailure(Result, FString::Printf(
                TEXT("Failed to install the merged LOD %d source data on the output mesh."),
                MergedLOD.LODIndex));
            return Result;
        }

        if (!OutputMesh->CommitMeshDescription(MergedLOD.LODIndex, CommitParams))
        {
            DiscardCreatedMesh(OutputMesh);
            SetFailure(Result, FString::Printf(
                TEXT("Failed to commit the merged LOD %d source data."),
                MergedLOD.LODIndex));
            return Result;
        }
    }

    OutputMesh->InvalidateDeriveDataCacheGUID();
    OutputMesh->PostEditChange();
    OutputMesh->MarkPackageDirty();
    Package->MarkPackageDirty();
    FAssetRegistryModule::AssetCreated(OutputMesh);

    Result.bSucceeded = true;
    Result.OutputPackageName = OutputPackageName;
    Result.OutputMesh = OutputMesh;
    Result.Message = FString::Printf(
        TEXT("Created %s from %d Skeletal Meshes with %d material slots and %d imported LOD(s)."),
        *OutputPackageName,
        ValidSourceMeshes.Num(),
        OutputMaterials.Num(),
        MergedLODs.Num());
    return Result;
}

void FDWCSkeletalMeshMerger::RegisterContentBrowserMenu(void* Owner)
{
    if (Owner == nullptr)
    {
        return;
    }

    UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([Owner]()
    {
        FToolMenuOwnerScoped OwnerScoped(Owner);
        UToolMenu* ToolMenu = UToolMenus::Get()->ExtendMenu(TEXT("ContentBrowser.AssetContextMenu.SkeletalMesh"));
        if (ToolMenu == nullptr)
        {
            return;
        }

        FToolMenuSection& Section = ToolMenu->FindOrAddSection(TEXT("GetAssetActions"));
        Section.AddDynamicEntry(TEXT("DWCMergeSkeletalMeshes"), FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
        {
            const UContentBrowserAssetContextMenuContext* Context = InSection.FindContext<UContentBrowserAssetContextMenuContext>();
            if (Context == nullptr || !Context->bCanBeModified || Context->SelectedAssets.Num() < 2)
            {
                return;
            }

            TArray<TWeakObjectPtr<USkeletalMesh>> SourceMeshes;
            SourceMeshes.Reserve(Context->SelectedAssets.Num());
            for (const FAssetData& SelectedAsset : Context->SelectedAssets)
            {
                USkeletalMesh* SourceMesh = Cast<USkeletalMesh>(SelectedAsset.GetAsset());
                if (SourceMesh == nullptr)
                {
                    return;
                }
                SourceMeshes.Add(SourceMesh);
            }

            const FText Label = FText::Format(
                LOCTEXT("MergeSkeletalMeshesLabel", "Merge Skeletal Meshes ({0})"),
                FText::AsNumber(SourceMeshes.Num()));

            FToolMenuEntry& Entry = InSection.AddMenuEntry(
                TEXT("DWCMergeSkeletalMeshesEntry"),
                Label,
                LOCTEXT(
                    "MergeSkeletalMeshesTooltip",
                    "Merge the selected Skeletal Meshes into one new editor asset. Each selected mesh becomes one material slot."),
                FSlateIcon(),
                FToolMenuExecuteAction::CreateLambda([SourceMeshes](const FToolMenuContext&)
                {
                    DWCSkeletalMeshMergerInternal::ExecuteMerge(SourceMeshes);
                }));
            Entry.InsertPosition = FToolMenuInsert(TEXT("Duplicate"), EToolMenuInsertType::After);
        }));
    }));
}

void FDWCSkeletalMeshMerger::UnregisterContentBrowserMenu(void* Owner)
{
    if (Owner != nullptr && UToolMenus::IsToolMenuUIEnabled())
    {
        UToolMenus::UnRegisterStartupCallback(Owner);
        UToolMenus::UnregisterOwner(Owner);
    }
}

#undef LOCTEXT_NAMESPACE
