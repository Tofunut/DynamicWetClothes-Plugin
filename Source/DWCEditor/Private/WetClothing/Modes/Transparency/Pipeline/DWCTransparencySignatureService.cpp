//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySignatureService.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyStageArtifactContract.h"

#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/DWCBakeLayer.h"
#include "DataAssets/WetClothingPartData.h"
#include "WetClothing/Modes/Transparency/Providers/DWCTransparencyProjectionSourceProvider.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Crc.h"
#include "Misc/SecureHash.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"

namespace
{
    FString HashCanonical(const FString& Canonical)
    {
        return FMD5::HashAnsiString(*Canonical);
    }

    FString BuildImportedTangentBasisSignature(
        const USkeletalMesh* Mesh,
        const int32 LODIndex)
    {
        const FSkeletalMeshRenderData* RenderData =
            Mesh != nullptr ? Mesh->GetResourceForRendering() : nullptr;
        if (RenderData == nullptr || !RenderData->LODRenderData.IsValidIndex(LODIndex))
        {
            return TEXT("Missing");
        }

#if WITH_EDITORONLY_DATA
        if (!RenderData->DerivedDataKey.IsEmpty())
        {
            return FString::Printf(
                TEXT("%s:LOD%d:DDC:%s"),
                *GetPathNameSafe(Mesh),
                LODIndex,
                *HashCanonical(RenderData->DerivedDataKey));
        }
#endif

        // Generated or transient meshes may not have an editor DDC key. Hash
        // their imported render basis directly as a deterministic fallback.
        const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];
        const FStaticMeshVertexBuffer& VertexBuffer =
            LODData.StaticVertexBuffers.StaticMeshVertexBuffer;
        uint32 BasisHash = 0;
        const int32 VertexCount = static_cast<int32>(LODData.GetNumVertices());
        for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
        {
            const FVector4f TangentX = VertexBuffer.VertexTangentX(VertexIndex);
            const FVector3f TangentY = VertexBuffer.VertexTangentY(VertexIndex);
            const FVector4f TangentZ = VertexBuffer.VertexTangentZ(VertexIndex);
            BasisHash = FCrc::MemCrc32(&TangentX, sizeof(TangentX), BasisHash);
            BasisHash = FCrc::MemCrc32(&TangentY, sizeof(TangentY), BasisHash);
            BasisHash = FCrc::MemCrc32(&TangentZ, sizeof(TangentZ), BasisHash);
        }
        return FString::Printf(
            TEXT("%s:LOD%d:V%d:%08X"),
            *GetPathNameSafe(Mesh),
            LODIndex,
            VertexCount,
            BasisHash);
    }

    FString ParameterKey(const FMaterialParameterInfo& Info)
    {
        return FString::Printf(
            TEXT("%d:%d:%s"),
            static_cast<int32>(Info.Association),
            Info.Index,
            *Info.Name.ToString());
    }

    void AppendMaterialParameters(const UMaterialInterface* Material, FString& InOutCanonical)
    {
        if (Material == nullptr)
        {
            return;
        }

        InOutCanonical += FString::Printf(
            TEXT("|LightingGuid=%s|BaseState=%s"),
            *Material->GetLightingGuid().ToString(EGuidFormats::Digits),
            Material->GetMaterial() != nullptr
                ? *Material->GetMaterial()->StateId.ToString(EGuidFormats::Digits)
                : TEXT("None"));

        TArray<FMaterialParameterInfo> Infos;
        TArray<FGuid> Ids;
        Material->GetAllScalarParameterInfo(Infos, Ids);
        Infos.Sort([](const FMaterialParameterInfo& A, const FMaterialParameterInfo& B)
        {
            return ParameterKey(A) < ParameterKey(B);
        });
        for (const FMaterialParameterInfo& Info : Infos)
        {
            float Value = 0.0f;
            if (Material->GetScalarParameterValue(Info, Value))
            {
                InOutCanonical += FString::Printf(TEXT("|S=%s,%.9g"), *ParameterKey(Info), Value);
            }
        }

        Infos.Reset();
        Ids.Reset();
        Material->GetAllVectorParameterInfo(Infos, Ids);
        Infos.Sort([](const FMaterialParameterInfo& A, const FMaterialParameterInfo& B)
        {
            return ParameterKey(A) < ParameterKey(B);
        });
        for (const FMaterialParameterInfo& Info : Infos)
        {
            FLinearColor Value = FLinearColor::Black;
            if (Material->GetVectorParameterValue(Info, Value))
            {
                InOutCanonical += FString::Printf(
                    TEXT("|V=%s,%.9g,%.9g,%.9g,%.9g"),
                    *ParameterKey(Info), Value.R, Value.G, Value.B, Value.A);
            }
        }

        Infos.Reset();
        Ids.Reset();
        Material->GetAllTextureParameterInfo(Infos, Ids);
        Infos.Sort([](const FMaterialParameterInfo& A, const FMaterialParameterInfo& B)
        {
            return ParameterKey(A) < ParameterKey(B);
        });
        for (const FMaterialParameterInfo& Info : Infos)
        {
            UTexture* Value = nullptr;
            if (Material->GetTextureParameterValue(Info, Value))
            {
                InOutCanonical += FString::Printf(
                    TEXT("|T=%s,%s"), *ParameterKey(Info), *GetPathNameSafe(Value));
                if (const UTexture2D* Texture2D = Cast<UTexture2D>(Value))
                {
                    InOutCanonical += FString::Printf(
                        TEXT(",%s"), *Texture2D->Source.GetId().ToString(EGuidFormats::Digits));
                }
            }
        }

        TArray<UTexture*> UsedTextures;
        Material->GetUsedTextures(UsedTextures);
        UsedTextures.Sort([](const UTexture& A, const UTexture& B)
        {
            return A.GetPathName() < B.GetPathName();
        });
        for (const UTexture* Texture : UsedTextures)
        {
            InOutCanonical += FString::Printf(TEXT("|UsedTexture=%s"), *GetPathNameSafe(Texture));
            if (const UTexture2D* Texture2D = Cast<UTexture2D>(Texture))
            {
                InOutCanonical += FString::Printf(
                    TEXT(",%s"), *Texture2D->Source.GetId().ToString(EGuidFormats::Digits));
            }
        }

        Infos.Reset();
        Ids.Reset();
        Material->GetAllStaticSwitchParameterInfo(Infos, Ids);
        Infos.Sort([](const FMaterialParameterInfo& A, const FMaterialParameterInfo& B)
        {
            return ParameterKey(A) < ParameterKey(B);
        });
        for (const FMaterialParameterInfo& Info : Infos)
        {
            bool Value = false;
            FGuid ExpressionGuid;
            if (Material->GetStaticSwitchParameterValue(
                    FHashedMaterialParameterInfo(Info), Value, ExpressionGuid))
            {
                InOutCanonical += FString::Printf(
                    TEXT("|SS=%s,%d,%s"), *ParameterKey(Info), Value ? 1 : 0,
                    *ExpressionGuid.ToString(EGuidFormats::Digits));
            }
        }

        Infos.Reset();
        Ids.Reset();
        Material->GetAllStaticComponentMaskParameterInfo(Infos, Ids);
        Infos.Sort([](const FMaterialParameterInfo& A, const FMaterialParameterInfo& B)
        {
            return ParameterKey(A) < ParameterKey(B);
        });
        for (const FMaterialParameterInfo& Info : Infos)
        {
            bool R = false;
            bool G = false;
            bool B = false;
            bool A = false;
            FGuid ExpressionGuid;
            if (Material->GetStaticComponentMaskParameterValue(
                    FHashedMaterialParameterInfo(Info), R, G, B, A, ExpressionGuid))
            {
                InOutCanonical += FString::Printf(
                    TEXT("|SM=%s,%d,%d,%d,%d,%s"), *ParameterKey(Info),
                    R ? 1 : 0, G ? 1 : 0, B ? 1 : 0, A ? 1 : 0,
                    *ExpressionGuid.ToString(EGuidFormats::Digits));
            }
        }
    }

    FString BuildPlacementSignature(const FTransform& Transform)
    {
        const FVector Translation = Transform.GetTranslation();
        FQuat Rotation = Transform.GetRotation().GetNormalized();
        // q and -q encode the same rotation. Pick one representation so an
        // equivalent placement cannot create a redundant persistent cache.
        if (Rotation.W < 0.0)
        {
            Rotation.X = -Rotation.X;
            Rotation.Y = -Rotation.Y;
            Rotation.Z = -Rotation.Z;
            Rotation.W = -Rotation.W;
        }
        const FVector Scale = Transform.GetScale3D();
        return HashCanonical(FString::Printf(
            TEXT("T=%.17g,%.17g,%.17g|R=%.17g,%.17g,%.17g,%.17g|S=%.17g,%.17g,%.17g"),
            Translation.X, Translation.Y, Translation.Z,
            Rotation.X, Rotation.Y, Rotation.Z, Rotation.W,
            Scale.X, Scale.Y, Scale.Z));
    }

    void AppendRevealStrokes(
        const FWetClothingTransparencyLayerData& Layer,
        FString& InOutCanonical)
    {
        for (const FDWCTransparencyRevealColorStroke& Stroke : Layer.RevealColorPaintStrokes)
        {
            InOutCanonical += FString::Printf(
                TEXT("|RevealStroke=%s,%d,%d,%d,%d,%.9g,%.9g,%.9g,%.9g,%.9g,%d"),
                *Stroke.StrokeGuid.ToString(EGuidFormats::Digits), Stroke.bEnabled ? 1 : 0,
                Stroke.MaterialSlotIndex, static_cast<int32>(Stroke.UVAddressMode),
                static_cast<int32>(Stroke.BrushMode), Stroke.PaintColor.R,
                Stroke.PaintColor.G, Stroke.PaintColor.B, Stroke.Falloff,
                Stroke.Spacing, Stroke.Samples.Num());
            for (const FDWCTransparencyBrushSample& Sample : Stroke.Samples)
            {
                InOutCanonical += FString::Printf(
                    TEXT(";%.9g,%.9g,%.9g,%.9g,%d"), Sample.PositionUV.X,
                    Sample.PositionUV.Y, Sample.RadiusUV, Sample.Strength,
                    Sample.UVIslandID);
            }
        }
    }

    void AppendAlphaStrokes(
        const FWetClothingTransparencyLayerData& Layer,
        FString& InOutCanonical)
    {
        for (const FDWCTransparencyBrushStroke& Stroke : Layer.EditableStrokes)
        {
            InOutCanonical += FString::Printf(
                TEXT("|AlphaStroke=%s,%d,%d,%d,%d,%.9g,%.9g,%.9g,%d"),
                *Stroke.StrokeGuid.ToString(EGuidFormats::Digits), Stroke.bEnabled ? 1 : 0,
                Stroke.MaterialSlotIndex, static_cast<int32>(Stroke.UVAddressMode),
                static_cast<int32>(Stroke.BrushMode), Stroke.Falloff,
                Stroke.TargetAlpha, Stroke.Spacing, Stroke.Samples.Num());
            for (const FDWCTransparencyBrushSample& Sample : Stroke.Samples)
            {
                InOutCanonical += FString::Printf(
                    TEXT(";%.9g,%.9g,%.9g,%.9g,%d"), Sample.PositionUV.X,
                    Sample.PositionUV.Y, Sample.RadiusUV, Sample.Strength,
                    Sample.UVIslandID);
            }
        }
    }

    void AppendTargetWetPartEligibility(
        const UWetClothingAsset& Asset,
        const int32 MaterialSlotIndex,
        FString& InOutCanonical)
    {
        const FWetClothingAuthoredMaterialSlot* Slot =
            Asset.Authored.PartData.EditableWetPartData.FindMaterialSlot(MaterialSlotIndex);
        if (Slot == nullptr)
        {
            InOutCanonical += TEXT("|TargetWetParts=MissingSlot");
            return;
        }

        InOutCanonical += FString::Printf(
            TEXT("|TargetWettable=%d"),
            Slot->bIsWettableSlot ? 1 : 0);

        TArray<const FWetClothingWetPartEntry*> Parts;
        Parts.Reserve(Slot->WetPartEntries.Num());
        for (const FWetClothingWetPartEntry& Part : Slot->WetPartEntries)
        {
            Parts.Add(&Part);
        }
        Parts.Sort([](const FWetClothingWetPartEntry& A, const FWetClothingWetPartEntry& B)
        {
            return A.WetPartID < B.WetPartID;
        });

        for (const FWetClothingWetPartEntry* Part : Parts)
        {
            TArray<int32> IslandIDs = Part->AssignedUVIslandIDs;
            IslandIDs.Sort();
            InOutCanonical += FString::Printf(TEXT("|WetPart=%d:Islands="), Part->WetPartID);
            for (const int32 IslandID : IslandIDs)
            {
                InOutCanonical += FString::Printf(TEXT("%d,"), IslandID);
            }
        }
    }
}

FDWCTransparencyMaterialSurfaceBakeIdentity
FDWCTransparencySignatureService::BuildMaterialSurfaceBakeIdentity(
    const USkeletalMesh* SourceMesh,
    const UMaterialInterface* EffectiveMaterial,
    const FTransform& BakeTransform,
    const int32 MaterialSlotIndex,
    const int32 SourceUVChannel,
    const int32 Resolution)
{
    FDWCTransparencyMaterialSurfaceBakeIdentity Identity;
    Identity.SourceMeshContentSignature =
        UWetClothingAsset::BuildMeshContentSignature(SourceMesh, 0, SourceUVChannel);

    FString MaterialCanonical = FString::Printf(
        TEXT("DWC.Transparency.MaterialSurfaceState.v3|Material=%s"),
        *GetPathNameSafe(EffectiveMaterial));
    AppendMaterialParameters(EffectiveMaterial, MaterialCanonical);
    Identity.EffectiveMaterialSignature = HashCanonical(MaterialCanonical);
    Identity.PlacementSignature = BuildPlacementSignature(BakeTransform);

    const FString Canonical = FString::Printf(
        TEXT("DWC.Transparency.MaterialSurfaceBake.v%d|Properties=BaseColor,Normal,Metallic|Mesh=%s|MeshContent=%s|Slot=%d|UV=%d|Resolution=%d|Material=%s|Placement=%s"),
        FDWCTransparencyMaterialSurfaceBakeIdentity::Version,
        *GetPathNameSafe(SourceMesh), *Identity.SourceMeshContentSignature,
        MaterialSlotIndex, SourceUVChannel, Resolution,
        *Identity.EffectiveMaterialSignature, *Identity.PlacementSignature);
    Identity.Digest = HashCanonical(Canonical);
    return Identity;
}

FString FDWCTransparencySignatureService::BuildStageArtifactSignature(
    const EDWCTransparencyTempArtifactKind Kind,
    const int32 ContractVersion,
    const FString& DependencySignature)
{
    if (DependencySignature.IsEmpty() || ContractVersion <= 0)
    {
        return FString();
    }
    return HashCanonical(FString::Printf(
        TEXT("DWC.Transparency.StageArtifact.v%d|Kind=%d|Dependency=%s"),
        ContractVersion, static_cast<int32>(Kind), *DependencySignature));
}

bool FDWCTransparencySignatureService::BuildSourceSignature(
    const UWetClothingAsset& Asset,
    const FWetClothingTransparencyLayerData& Layer,
    FString& OutSignature,
    FString& OutMaterialBakeSignature,
    FString& OutError)
{
    OutSignature.Reset();
    OutMaterialBakeSignature.Reset();
    OutError.Reset();

    const USkeletalMesh* Mesh = Asset.GetRuntimeSkeletalMesh();
    const USkeletalMesh* SourceMesh = Asset.GetSourceSkeletalMesh();
    constexpr int32 LODIndex = 0;
    const int32 DataUV = Asset.GetDWCDataUVChannelIndex();
    const FDWCDataUVLODMetadata* DataUVMetadata = Asset.FindDataUVMetadataForLOD(LODIndex);
    if (Mesh == nullptr || SourceMesh == nullptr || DataUV == INDEX_NONE || DataUVMetadata == nullptr ||
        DataUVMetadata->DataUVOutputSignature.IsEmpty())
    {
        OutError = TEXT("Transparency source signature requires a runtime mesh and valid LOD 0 DWC Data UV metadata.");
        return false;
    }
    if (!Mesh->GetMaterials().IsValidIndex(Layer.TargetSurface.OuterMaterialSlotIndex))
    {
        OutError = TEXT("Transparency source signature references an invalid target material slot.");
        return false;
    }

    const int32 Resolution = FMath::Clamp(
        Asset.Authored.TransparencyData.TransparencyBakeResolution, 16, 4096);
    TMap<const USkeletalMesh*, FString> TangentBasisSignatures;
    const auto ResolveTangentBasisSignature = [&TangentBasisSignatures, LODIndex](
        const USkeletalMesh* SignatureMesh) -> const FString&
    {
        if (const FString* Existing = TangentBasisSignatures.Find(SignatureMesh))
        {
            return *Existing;
        }
        return TangentBasisSignatures.Add(
            SignatureMesh,
            BuildImportedTangentBasisSignature(SignatureMesh, LODIndex));
    };
    FString Canonical = FString::Printf(
        TEXT("DWC.Transparency.Source.v4|Mesh=%s|Layer=%s|Type=%d|Slot=%d|UV=%d|LOD=0|Resolution=%d|Address=%d|DataUV=%s|OuterBasis=%s"),
        *GetPathNameSafe(Mesh), *Layer.LayerGuid.ToString(EGuidFormats::DigitsWithHyphens),
        static_cast<int32>(Layer.SourceType), Layer.TargetSurface.OuterMaterialSlotIndex,
        DataUV, Resolution, static_cast<int32>(Layer.TargetSurface.UVAddressMode),
        *DataUVMetadata->DataUVOutputSignature,
        *ResolveTangentBasisSignature(Mesh));
    AppendTargetWetPartEligibility(Asset, Layer.TargetSurface.OuterMaterialSlotIndex, Canonical);

    if (Layer.SourceType == EDWCTransparencySourceType::ManualColorOrTexture)
    {
        const FLinearColor& Color = Layer.ManualColorSource.BaseRevealColor;
        Canonical += FString::Printf(
            TEXT("|Manual=%d,%.9g,%.9g,%.9g,%.9g,%s,%d,%d,%d"),
            static_cast<int32>(Layer.ManualColorSource.SourceMode),
            Color.R, Color.G, Color.B,
            Layer.ManualColorSource.InitialTransparencyAlpha,
            *Layer.ManualColorSource.SampledColorTexture.ToSoftObjectPath().ToString(),
            Layer.ManualColorSource.SampledMaterialSlotIndex,
            Layer.ManualColorSource.SampledUVChannelIndex,
            Layer.ManualColorSource.SampledUVIslandID);
        if (Layer.ManualColorSource.SourceMode ==
                EDWCTransparencyManualRevealSourceMode::UVIslandAverage)
        {
            const UTexture2D* SampledTexture =
                Layer.ManualColorSource.SampledColorTexture.LoadSynchronous();
            Canonical += FString::Printf(
                TEXT("|SampledTextureSource=%s"),
                SampledTexture != nullptr
                    ? *SampledTexture->Source.GetId().ToString()
                    : TEXT("Missing"));
        }
    }
    else if (Layer.SourceType == EDWCTransparencySourceType::SameMeshMaterialSlots)
    {
        Canonical += FString::Printf(
            TEXT("|SourceBasis=%s"),
            *ResolveTangentBasisSignature(SourceMesh));
        Canonical += FString::Printf(
            TEXT("|Ray=%.9g,%.9g,%.9g,%.9g,%.9g"), Layer.RaySettings.RayStartOffset,
            Layer.RaySettings.MinHitDistance, Layer.RaySettings.FullTransparencyDistance,
            Layer.RaySettings.NoTransparencyDistance, Layer.RaySettings.MaxRayDistance);
        for (int32 Priority = 0; Priority < Layer.SameMeshSource.InnerSlotPriority.Num(); ++Priority)
        {
            const FWetClothingTransparencyInnerSlot& Inner =
                Layer.SameMeshSource.InnerSlotPriority[Priority];
            UMaterialInterface* Material = SourceMesh->GetMaterials().IsValidIndex(Inner.MaterialSlotIndex)
                ? SourceMesh->GetMaterials()[Inner.MaterialSlotIndex].MaterialInterface
                : nullptr;
            const FDWCTransparencyMaterialSurfaceBakeIdentity MaterialIdentity =
                BuildMaterialSurfaceBakeIdentity(
                    SourceMesh, Material, FTransform::Identity,
                    Inner.MaterialSlotIndex, Inner.SourceUVChannel, Resolution);
            if (!MaterialIdentity.IsValid())
            {
                OutError = TEXT("Could not identify a Same Mesh source material surface.");
                return false;
            }
            OutMaterialBakeSignature += FString::Printf(
                TEXT("|%d:%s"), Priority, *MaterialIdentity.Digest);
            Canonical += FString::Printf(
                TEXT("|Inner=%d,%d,%d,%s,%s,%s"), Priority, Inner.MaterialSlotIndex,
                Inner.SourceUVChannel, *Inner.MaterialSlotName.ToString(),
                *GetPathNameSafe(SourceMesh), *MaterialIdentity.Digest);
        }
        OutMaterialBakeSignature = HashCanonical(OutMaterialBakeSignature);
    }
    else if (Layer.SourceType == EDWCTransparencySourceType::OtherSkeletalMeshComponents)
    {
        if (Layer.BlueprintSource.BlueprintClass.IsNull())
        {
            OutError = TEXT("Transparency source signature requires a Source Blueprint.");
            return false;
        }
        FDWCTransparencyProjectionSourceSet BlueprintSources;
        FString SourceError;
        if (!FDWCTransparencyProjectionSourceProvider::BuildBlueprintSources(
                Asset,
                Layer,
                BlueprintSources,
                SourceError))
        {
            OutError = MoveTemp(SourceError);
            return false;
        }
        Canonical += FString::Printf(
            TEXT("|Blueprint=%s|Snapshot=%s"),
            *Layer.BlueprintSource.BlueprintClass.ToSoftObjectPath().ToString(),
            *BlueprintSources.ProviderSignature);
        FString MaterialCanonical;
        for (const FDWCTransparencyProjectionSource& Source : BlueprintSources.Sources)
        {
            Canonical += FString::Printf(
                TEXT("|SourceBasis=%s"),
                *ResolveTangentBasisSignature(Source.Layer.SkeletalMesh));
            const FDWCTransparencyMaterialSurfaceBakeIdentity MaterialIdentity =
                BuildMaterialSurfaceBakeIdentity(
                    Source.Layer.SkeletalMesh, Source.EffectiveMaterial,
                    Source.Layer.BakeTransform, Source.MaterialSlotIndex,
                    Source.Layer.SourceUVChannel, Resolution);
            if (!MaterialIdentity.IsValid())
            {
                OutError = TEXT("Could not identify a Blueprint source material surface.");
                return false;
            }
            MaterialCanonical += FString::Printf(
                TEXT("|P%d:%s:%d:%s"), Source.PriorityIndex,
                *Source.Layer.LayerId.ToString(), Source.MaterialSlotIndex,
                *MaterialIdentity.Digest);
        }
        OutMaterialBakeSignature = HashCanonical(MaterialCanonical);
        Canonical += FString::Printf(
            TEXT("|MaterialSurfaces=%s"), *OutMaterialBakeSignature);
    }
    else if (Layer.SourceType == EDWCTransparencySourceType::ExternalSkeletalMesh)
    {
        FDWCTransparencyProjectionSourceSet ExternalSources;
        FString SourceError;
        if (!FDWCTransparencyProjectionSourceProvider::BuildExternalMeshSources(
                Asset,
                Layer,
                ExternalSources,
                SourceError))
        {
            OutError = MoveTemp(SourceError);
            return false;
        }
        Canonical += FString::Printf(
            TEXT("|ExternalSources=%s"),
            *ExternalSources.ProviderSignature);
        FString MaterialCanonical;
        for (const FDWCTransparencyProjectionSource& Source : ExternalSources.Sources)
        {
            Canonical += FString::Printf(
                TEXT("|SourceBasis=%s"),
                *ResolveTangentBasisSignature(Source.Layer.SkeletalMesh));
            const FDWCTransparencyMaterialSurfaceBakeIdentity MaterialIdentity =
                BuildMaterialSurfaceBakeIdentity(
                    Source.Layer.SkeletalMesh, Source.EffectiveMaterial,
                    Source.Layer.BakeTransform, Source.MaterialSlotIndex,
                    Source.Layer.SourceUVChannel, Resolution);
            if (!MaterialIdentity.IsValid())
            {
                OutError = TEXT("Could not identify an External Mesh source material surface.");
                return false;
            }
            MaterialCanonical += FString::Printf(
                TEXT("|P%d:%s:%d:%s"), Source.PriorityIndex,
                *Source.Layer.LayerId.ToString(), Source.MaterialSlotIndex,
                *MaterialIdentity.Digest);
        }
        OutMaterialBakeSignature = HashCanonical(MaterialCanonical);
        Canonical += FString::Printf(
            TEXT("|MaterialSurfaces=%s"), *OutMaterialBakeSignature);
    }
    else
    {
        OutError = TEXT("Unsupported transparency source type.");
        return false;
    }

    OutSignature = HashCanonical(Canonical);
    return true;
}

FString FDWCTransparencySignatureService::BuildRevealSignature(
    const FString& SourceSignature,
    const FWetClothingTransparencyLayerData& Layer,
    const float RevealMetallicDarkeningStrength)
{
    FString Canonical = FString::Printf(
        TEXT("DWC.Transparency.Reveal.v2|Source=%s|MetallicDarkening=%.9g"),
        *SourceSignature,
        static_cast<double>(FMath::Clamp(RevealMetallicDarkeningStrength, 0.0f, 1.0f)));
    AppendRevealStrokes(Layer, Canonical);
    return HashCanonical(Canonical);
}

FString FDWCTransparencySignatureService::BuildRevealNormalSignature(
    const FString& SourceSignature)
{
    const FString Canonical = FString::Printf(
        TEXT("DWC.Transparency.RevealNormal.v%d|Basis=%d|Encoding=CoverageWeightedOuterTangentNormalRG|Source=%s"),
        RevealNormalEncodingVersion,
        RevealSurfaceBasisVersion,
        *SourceSignature);
    return HashCanonical(Canonical);
}

FString FDWCTransparencySignatureService::BuildRevealSurfaceAuthoringSignature(
    const FString& SourceSignature)
{
    const FString Canonical = FString::Printf(
        TEXT("DWC.Transparency.RevealSurfaceAuthoring.v2|Basis=%d|Encoding=OuterTangentNormalRG,InnerMetallicB,SourceCoverageA|Source=%s"),
        RevealSurfaceBasisVersion,
        *SourceSignature);
    return HashCanonical(Canonical);
}

FString FDWCTransparencySignatureService::BuildAlphaAuthoringSignature(
    const FWetClothingTransparencyLayerData& Layer)
{
    FString Canonical(TEXT("DWC.Transparency.AlphaAuthoring.v1"));
    AppendAlphaStrokes(Layer, Canonical);
    return HashCanonical(Canonical);
}

FString FDWCTransparencySignatureService::BuildSuppressionSettingsSignature(
    const float CoverageThreshold,
    const float MaskSoftness,
    const float SuppressionStrength,
    const float TransparencyStrength)
{
    const FString Canonical = FString::Printf(
        TEXT("DWCTransparencySuppression_v2.DirectMask|threshold=%.9g|softness=%.9g|suppression=%.9g|transparency=%.9g"),
        CoverageThreshold,
        MaskSoftness,
        SuppressionStrength,
        TransparencyStrength);
    return FMD5::HashAnsiString(*Canonical);
}

FString FDWCTransparencySignatureService::BuildFinalSignature(
    const FDWCTransparencyFinalSignatureInputs& Inputs)
{
    const FString AlphaSignature = BuildFinalAlphaSignature(Inputs);
    const FString Canonical = FString::Printf(
        TEXT("DWC.Transparency.Final.v3|Reveal=%s|FinalAlpha=%s"),
        *Inputs.RevealSignature,
        *AlphaSignature);
    return HashCanonical(Canonical);
}

FString FDWCTransparencySignatureService::BuildFinalAlphaSignature(
    const FDWCTransparencyFinalSignatureInputs& Inputs)
{
    const FString Canonical = FString::Printf(
        TEXT("DWC.Transparency.FinalAlpha.v2|Source=%s|Alpha=%s|WrinkleMask=%s|Suppression=%s|Padding=%d|EdgeFeather=%.9g"),
        *Inputs.SourceSignature,
        *Inputs.AlphaAuthoringSignature,
        *Inputs.WrinkleMaskBuildSignature,
        *Inputs.SuppressionSettingsSignature,
        Inputs.PaddingPixels,
        Inputs.EdgeFeatherPixels);
    return HashCanonical(Canonical);
}

FString FDWCTransparencySignatureService::BuildFinalSignature(
    const FString& RevealSignature,
    const FWetClothingTransparencyLayerData& Layer,
    const FString& WrinkleMaskBuildSignature,
    const FString& SuppressionSettingsSignature,
    const int32 PaddingPixels,
    const float EdgeFeatherPixels,
    const FString& SourceSignature)
{
    FDWCTransparencyFinalSignatureInputs Inputs;
    // Callers should pass the canonical Stage 2 source identity. Falling back
    // to RevealSignature preserves deterministic legacy/test call sites.
    Inputs.SourceSignature = SourceSignature.IsEmpty() ? RevealSignature : SourceSignature;
    Inputs.RevealSignature = RevealSignature;
    Inputs.AlphaAuthoringSignature = BuildAlphaAuthoringSignature(Layer);
    Inputs.WrinkleMaskBuildSignature = WrinkleMaskBuildSignature;
    Inputs.SuppressionSettingsSignature = SuppressionSettingsSignature;
    Inputs.PaddingPixels = PaddingPixels;
    Inputs.EdgeFeatherPixels = EdgeFeatherPixels;
    return BuildFinalSignature(Inputs);
}

FDWCTransparencyStageStatus FDWCTransparencySignatureService::EvaluateEditorStageCache(
    const FWetClothingTransparencyLayerData& Layer,
    const FString& ExpectedSourceSignature,
    const FString& ExpectedRevealSignature)
{
    FDWCTransparencyStageStatus Status;
#if WITH_EDITORONLY_DATA
    const FDWCTransparencyEditorStageCacheMetadata& Cache = Layer.EditorStageCache;
    Status.Stage = EDWCTransparencyStage::Source;
    if (!Cache.bSourceGenerated || Cache.SourceSignature.IsEmpty())
    {
        Status.Reason = EDWCTransparencyStaleReason::MissingArtifact;
        Status.Detail = TEXT("The Stage 2 source checkpoint has not been generated.");
        return Status;
    }
    if (Cache.SourceSignature != ExpectedSourceSignature)
    {
        Status.Reason = EDWCTransparencyStaleReason::SourceInputsChanged;
        Status.Detail = TEXT("The Stage 2 source inputs changed.");
        return Status;
    }
    const FDWCTransparencyTempArtifactReference* BaseRevealReference =
        FDWCTransparencyStageArtifactContract::FindReference(
            Layer, EDWCTransparencyTempArtifactKind::BaseRevealColor);
    const FIntPoint ArtifactResolution = BaseRevealReference != nullptr
        ? BaseRevealReference->Resolution
        : FIntPoint::ZeroValue;
    FString ArtifactError;
    if (!FDWCTransparencyStageArtifactContract::InspectSourceArtifactSet(
            Layer,
            ExpectedSourceSignature,
            ArtifactResolution,
            false,
            ArtifactError))
    {
        Status.Reason = EDWCTransparencyStaleReason::MissingArtifact;
        Status.Detail = ArtifactError;
        return Status;
    }
    Status.Stage = EDWCTransparencyStage::Reveal;
    if (!ExpectedRevealSignature.IsEmpty() && Cache.RevealSignature != ExpectedRevealSignature)
    {
        Status.Reason = EDWCTransparencyStaleReason::RevealEditsChanged;
        Status.Detail = TEXT("The Stage 3 reveal-color edits changed.");
        return Status;
    }
    if (Cache.bRevealReviewed &&
        !FDWCTransparencyStageArtifactContract::InspectRevealArtifact(
            Layer,
            ExpectedSourceSignature,
            ExpectedRevealSignature,
            ArtifactResolution,
            false,
            ArtifactError))
    {
        Status.Reason = EDWCTransparencyStaleReason::MissingArtifact;
        Status.Detail = ArtifactError;
        return Status;
    }
#else
    Status.Reason = EDWCTransparencyStaleReason::MissingArtifact;
    Status.Detail = TEXT("Transparency stage cache metadata is editor-only.");
#endif
    return Status;
}
