#include "Core/DWCSkeletalMeshMaterialSlotExtractor.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "ContentBrowserMenuContexts.h"
#include "Engine/SkeletalMesh.h"
#include "IAssetTools.h"
#include "MeshDescription.h"
#include "MeshElementRemappings.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "SkeletalMeshAttributes.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "DWCSkeletalMeshMaterialSlotExtractor"

namespace DWCSkeletalMeshMaterialSlotExtractorInternal
{
    DEFINE_LOG_CATEGORY_STATIC(LogDWCSkeletalMeshMaterialSlotExtractor, Log, All);

    template <typename ElementIDType>
    bool IsValidElementID(const ElementIDType ElementID)
    {
        return ElementID.GetValue() != INDEX_NONE;
    }

    FName GetStableSlotName(const FSkeletalMaterial& Material, const int32 MaterialSlotIndex)
    {
        if (!Material.MaterialSlotName.IsNone())
        {
            return Material.MaterialSlotName;
        }

        if (!Material.ImportedMaterialSlotName.IsNone())
        {
            return Material.ImportedMaterialSlotName;
        }

        return FName(*FString::Printf(TEXT("MaterialSlot_%d"), MaterialSlotIndex));
    }

    int32 ResolveMaterialSlotIndex(
        const USkeletalMesh* SkeletalMesh,
        const FMeshDescription& MeshDescription,
        FSkeletalMeshAttributes& Attributes,
        const FTriangleID TriangleID)
    {
        const FPolygonID PolygonID = MeshDescription.GetTrianglePolygon(TriangleID);
        if (!IsValidElementID(PolygonID))
        {
            return INDEX_NONE;
        }

        const FPolygonGroupID PolygonGroupID = MeshDescription.GetPolygonPolygonGroup(PolygonID);
        const int32 FallbackIndex = PolygonGroupID.GetValue();
        if (SkeletalMesh == nullptr || !IsValidElementID(PolygonGroupID))
        {
            return FallbackIndex;
        }

        const TArray<FSkeletalMaterial>& Materials = SkeletalMesh->GetMaterials();
        const auto MaterialSlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
        const FName PolygonGroupMaterialName = MaterialSlotNames[PolygonGroupID];

        if (!PolygonGroupMaterialName.IsNone())
        {
            for (int32 MaterialIndex = 0; MaterialIndex < Materials.Num(); ++MaterialIndex)
            {
                const FSkeletalMaterial& Material = Materials[MaterialIndex];
                if (Material.MaterialSlotName == PolygonGroupMaterialName || Material.ImportedMaterialSlotName == PolygonGroupMaterialName)
                {
                    return MaterialIndex;
                }
            }
        }

        return Materials.IsValidIndex(FallbackIndex) ? FallbackIndex : INDEX_NONE;
    }

    int32 ResolvePolygonMaterialSlotIndex(
        const USkeletalMesh* SkeletalMesh,
        const FMeshDescription& MeshDescription,
        FSkeletalMeshAttributes& Attributes,
        const FPolygonID PolygonID)
    {
        if (!IsValidElementID(PolygonID))
        {
            return INDEX_NONE;
        }

        const FPolygonGroupID PolygonGroupID = MeshDescription.GetPolygonPolygonGroup(PolygonID);
        const int32 FallbackIndex = PolygonGroupID.GetValue();
        if (SkeletalMesh == nullptr || !IsValidElementID(PolygonGroupID))
        {
            return FallbackIndex;
        }

        const TArray<FSkeletalMaterial>& Materials = SkeletalMesh->GetMaterials();
        const auto MaterialSlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
        const FName PolygonGroupMaterialName = MaterialSlotNames[PolygonGroupID];

        if (!PolygonGroupMaterialName.IsNone())
        {
            for (int32 MaterialIndex = 0; MaterialIndex < Materials.Num(); ++MaterialIndex)
            {
                const FSkeletalMaterial& Material = Materials[MaterialIndex];
                if (Material.MaterialSlotName == PolygonGroupMaterialName || Material.ImportedMaterialSlotName == PolygonGroupMaterialName)
                {
                    return MaterialIndex;
                }
            }
        }

        return Materials.IsValidIndex(FallbackIndex) ? FallbackIndex : INDEX_NONE;
    }

    FString BuildOutputPackageName(const USkeletalMesh* SourceMesh, const int32 MaterialSlotIndex, const FName SlotName)
    {
        FString CleanSlotName = ObjectTools::SanitizeObjectName(SlotName.ToString());
        if (CleanSlotName.IsEmpty())
        {
            CleanSlotName = FString::Printf(TEXT("Slot_%d"), MaterialSlotIndex);
        }

        FString UniquePackageName;
        FString UniqueAssetName;
        const FString Suffix = FString::Printf(TEXT("_Slot_%02d_%s"), MaterialSlotIndex, *CleanSlotName);
        FAssetToolsModule::GetModule().Get().CreateUniqueAssetName(SourceMesh->GetOutermost()->GetName(), Suffix, UniquePackageName, UniqueAssetName);
        return UniquePackageName;
    }

    FString MakeResultPrefix(const USkeletalMesh* SourceMesh, const int32 MaterialSlotIndex)
    {
        return FString::Printf(TEXT("%s material slot %d"), *GetNameSafe(SourceMesh), MaterialSlotIndex);
    }

    void SetFailure(FDWCSkeletalMeshMaterialSlotExtractionResult& Result, const FString& Message)
    {
        Result.bSucceeded = false;
        Result.Message = Message;
    }

    bool DoesLODContainSlot(USkeletalMesh* Mesh, const int32 LODIndex, const int32 MaterialSlotIndex)
    {
        FMeshDescription* MeshDescription = Mesh != nullptr ? Mesh->GetMeshDescription(LODIndex) : nullptr;
        if (MeshDescription == nullptr)
        {
            return false;
        }

        FSkeletalMeshAttributes Attributes(*MeshDescription);
        Attributes.Register();

        for (const FTriangleID TriangleID : MeshDescription->Triangles().GetElementIDs())
        {
            if (ResolveMaterialSlotIndex(Mesh, *MeshDescription, Attributes, TriangleID) == MaterialSlotIndex)
            {
                return true;
            }
        }

        return false;
    }

    bool KeepOnlySlotInLOD(
        USkeletalMesh* Mesh,
        const int32 LODIndex,
        const int32 OriginalMaterialSlotIndex,
        const FName OutputSlotName,
        int32& OutRemovedTriangleCount,
        FString& OutError)
    {
        FMeshDescription* MeshDescription = Mesh != nullptr ? Mesh->GetMeshDescription(LODIndex) : nullptr;
        if (MeshDescription == nullptr)
        {
            return true;
        }

        FSkeletalMeshAttributes Attributes(*MeshDescription);
        Attributes.Register();

        TArray<FPolygonID> PolygonsToDelete;
        int32 KeptTriangleCount = 0;
        int32 RemovedTriangleCount = 0;
        for (const FPolygonID PolygonID : MeshDescription->Polygons().GetElementIDs())
        {
            const TArrayView<const FTriangleID> PolygonTriangles = MeshDescription->GetPolygonTriangles(PolygonID);
            const int32 MaterialSlotIndex = ResolvePolygonMaterialSlotIndex(Mesh, *MeshDescription, Attributes, PolygonID);
            if (MaterialSlotIndex == OriginalMaterialSlotIndex)
            {
                KeptTriangleCount += PolygonTriangles.Num();
            }
            else
            {
                RemovedTriangleCount += PolygonTriangles.Num();
                PolygonsToDelete.Add(PolygonID);
            }
        }

        if (KeptTriangleCount == 0)
        {
            OutError = FString::Printf(TEXT("LOD %d does not contain material slot %d."), LODIndex, OriginalMaterialSlotIndex);
            return false;
        }

        if (!PolygonsToDelete.IsEmpty())
        {
            MeshDescription->DeletePolygons(PolygonsToDelete);

                FElementIDRemappings Remappings;
            MeshDescription->Compact(Remappings);
        }

        FSkeletalMeshAttributes CompactedAttributes(*MeshDescription);
        CompactedAttributes.Register();
        auto MaterialSlotNames = CompactedAttributes.GetPolygonGroupMaterialSlotNames();
        for (const FPolygonGroupID PolygonGroupID : MeshDescription->PolygonGroups().GetElementIDs())
        {
            MaterialSlotNames[PolygonGroupID] = OutputSlotName;
        }

        USkeletalMesh::FCommitMeshDescriptionParams CommitParams;
        CommitParams.bForceUpdate = true;
        if (!Mesh->CommitMeshDescription(LODIndex, CommitParams))
        {
            OutError = FString::Printf(TEXT("Failed to commit LOD %d mesh description."), LODIndex);
            return false;
        }

        OutRemovedTriangleCount += RemovedTriangleCount;
        return true;
    }

    void NotifyResult(const FDWCSkeletalMeshMaterialSlotExtractionResult& Result)
    {
        const EAppMsgType::Type DialogType = EAppMsgType::Ok;
        const FText Text = FText::FromString(Result.Message);
        if (Result.bSucceeded)
        {
            UE_LOG(LogDWCSkeletalMeshMaterialSlotExtractor, Display, TEXT("%s"), *Result.Message);
        }
        else
        {
            UE_LOG(LogDWCSkeletalMeshMaterialSlotExtractor, Error, TEXT("%s"), *Result.Message);
            FMessageDialog::Open(DialogType, Text);
        }
    }

    void ExecuteExtraction(USkeletalMesh* SourceMesh, const int32 MaterialSlotIndex)
    {
        const FDWCSkeletalMeshMaterialSlotExtractionResult Result =
            FDWCSkeletalMeshMaterialSlotExtractor::ExtractMaterialSlot(SourceMesh, MaterialSlotIndex);
        NotifyResult(Result);
    }

    void BuildSlotSubMenu(UToolMenu* Menu, TWeakObjectPtr<USkeletalMesh> SourceMesh)
    {
        if (!SourceMesh.IsValid())
        {
            return;
        }

        FToolMenuSection& SlotSection = Menu->AddSection(TEXT("DWCExtractMaterialSlotSlots"));
        const TArray<FSkeletalMaterial>& Materials = SourceMesh->GetMaterials();
        for (int32 MaterialIndex = 0; MaterialIndex < Materials.Num(); ++MaterialIndex)
        {
            const FName SlotName = GetStableSlotName(Materials[MaterialIndex], MaterialIndex);
            const FString Label = FString::Printf(TEXT("%02d - %s"), MaterialIndex, *SlotName.ToString());
            SlotSection.AddMenuEntry(
                FName(*FString::Printf(TEXT("DWCExtractMaterialSlot_%d"), MaterialIndex)),
                FText::FromString(Label),
                LOCTEXT("ExtractMaterialSlotTooltip", "Create a new Skeletal Mesh asset containing only this material slot's triangles."),
                FSlateIcon(),
                FToolMenuExecuteAction::CreateLambda([SourceMesh, MaterialIndex](const FToolMenuContext&)
                {
                    ExecuteExtraction(SourceMesh.Get(), MaterialIndex);
                }));
        }
    }
}

FDWCSkeletalMeshMaterialSlotExtractionResult FDWCSkeletalMeshMaterialSlotExtractor::ExtractMaterialSlot(
    USkeletalMesh* SourceMesh,
    const int32 MaterialSlotIndex,
    const FString& OptionalOutputPackageName)
{
    using namespace DWCSkeletalMeshMaterialSlotExtractorInternal;

    FDWCSkeletalMeshMaterialSlotExtractionResult Result;

    if (SourceMesh == nullptr)
    {
        SetFailure(Result, TEXT("A source skeletal mesh is required."));
        return Result;
    }

    const TArray<FSkeletalMaterial>& SourceMaterials = SourceMesh->GetMaterials();
    if (!SourceMaterials.IsValidIndex(MaterialSlotIndex))
    {
        SetFailure(Result, FString::Printf(
            TEXT("%s failed: material slot %d is out of range. The mesh has %d material slot(s)."),
            *MakeResultPrefix(SourceMesh, MaterialSlotIndex),
            MaterialSlotIndex,
            SourceMaterials.Num()));
        return Result;
    }

    if (!DoesLODContainSlot(SourceMesh, 0, MaterialSlotIndex))
    {
        SetFailure(Result, FString::Printf(
            TEXT("%s failed: LOD 0 has no editable triangles assigned to that material slot."),
            *MakeResultPrefix(SourceMesh, MaterialSlotIndex)));
        return Result;
    }

    FSkeletalMaterial OutputMaterial = SourceMaterials[MaterialSlotIndex];
    const FName OutputSlotName = GetStableSlotName(OutputMaterial, MaterialSlotIndex);
    OutputMaterial.MaterialSlotName = OutputSlotName;
    OutputMaterial.ImportedMaterialSlotName = OutputSlotName;

    FString OutputPackageName = OptionalOutputPackageName;
    if (OutputPackageName.IsEmpty())
    {
        OutputPackageName = BuildOutputPackageName(SourceMesh, MaterialSlotIndex, OutputSlotName);
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
        SetFailure(Result, FString::Printf(TEXT("Output asset already exists: %s.%s"), *OutputPackageName, *OutputAssetName));
        return Result;
    }

    USkeletalMesh* OutputMesh = DuplicateObject<USkeletalMesh>(SourceMesh, Package, *OutputAssetName);
    if (OutputMesh == nullptr)
    {
        SetFailure(Result, FString::Printf(TEXT("Failed to duplicate skeletal mesh into: %s"), *OutputPackageName));
        return Result;
    }

    OutputMesh->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
    OutputMesh->Modify();

    int32 EditedLODCount = 0;
    FString ErrorMessage;
    const int32 LODCount = OutputMesh->GetLODNum();
    for (int32 LODIndex = 0; LODIndex < LODCount; ++LODIndex)
    {
        if (OutputMesh->GetMeshDescription(LODIndex) == nullptr)
        {
            continue;
        }

        if (!KeepOnlySlotInLOD(OutputMesh, LODIndex, MaterialSlotIndex, OutputSlotName, Result.RemovedTriangleCount, ErrorMessage))
        {
            SetFailure(Result, FString::Printf(
                TEXT("%s failed: %s"),
                *MakeResultPrefix(SourceMesh, MaterialSlotIndex),
                *ErrorMessage));
            return Result;
        }

        ++EditedLODCount;
    }

    TArray<FSkeletalMaterial> OutputMaterials;
    OutputMaterials.Add(OutputMaterial);
    OutputMesh->SetMaterials(OutputMaterials);

    if (EditedLODCount == 0)
    {
        SetFailure(Result, FString::Printf(
            TEXT("%s failed: the mesh has no editable LOD mesh descriptions."),
            *MakeResultPrefix(SourceMesh, MaterialSlotIndex)));
        return Result;
    }

    OutputMesh->InvalidateDeriveDataCacheGUID();
    OutputMesh->PostEditChange();
    OutputMesh->MarkPackageDirty();
    Package->MarkPackageDirty();
    FAssetRegistryModule::AssetCreated(OutputMesh);

    Result.bSucceeded = true;
    Result.OutputPackageName = OutputPackageName;
    Result.Message = FString::Printf(
        TEXT("Created %s from %s. Kept material slot %d (%s), edited %d LOD(s), removed %d triangle(s)."),
        *OutputPackageName,
        *GetNameSafe(SourceMesh),
        MaterialSlotIndex,
        *OutputSlotName.ToString(),
        EditedLODCount,
        Result.RemovedTriangleCount);
    return Result;
}

void FDWCSkeletalMeshMaterialSlotExtractor::RegisterContentBrowserMenu(void* Owner)
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
        Section.AddDynamicEntry(TEXT("DWCExtractMaterialSlot"), FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
        {
            const UContentBrowserAssetContextMenuContext* Context = InSection.FindContext<UContentBrowserAssetContextMenuContext>();
            if (Context == nullptr || !Context->bCanBeModified || Context->SelectedAssets.Num() != 1)
            {
                return;
            }

            USkeletalMesh* SourceMesh = Cast<USkeletalMesh>(Context->SelectedAssets[0].GetAsset());
            if (SourceMesh == nullptr || SourceMesh->GetMaterials().IsEmpty())
            {
                return;
            }

            FToolMenuEntry& SubMenu = InSection.AddSubMenu(
                TEXT("DWCExtractMaterialSlotSubMenu"),
                LOCTEXT("ExtractMaterialSlotSubMenu", "Extract Material Slot"),
                LOCTEXT("ExtractMaterialSlotSubMenuTooltip", "Create a new Skeletal Mesh asset from one material slot."),
                FNewToolMenuDelegate::CreateStatic(
                    &DWCSkeletalMeshMaterialSlotExtractorInternal::BuildSlotSubMenu,
                    TWeakObjectPtr<USkeletalMesh>(SourceMesh)));
            SubMenu.InsertPosition = FToolMenuInsert(TEXT("Duplicate"), EToolMenuInsertType::After);
        }));
    }));
}

void FDWCSkeletalMeshMaterialSlotExtractor::UnregisterContentBrowserMenu(void* Owner)
{
    if (Owner != nullptr && UToolMenus::IsToolMenuUIEnabled())
    {
        UToolMenus::UnRegisterStartupCallback(Owner);
        UToolMenus::UnregisterOwner(Owner);
    }
}

#undef LOCTEXT_NAMESPACE
