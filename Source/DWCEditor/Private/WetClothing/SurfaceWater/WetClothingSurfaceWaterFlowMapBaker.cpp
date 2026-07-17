#include "WetClothing/SurfaceWater/WetClothingSurfaceWaterFlowMapBaker.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetCompilingManager.h"
#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "UObject/Package.h"
#include "WetClothing/Common/Analysis/WetClothingAssetMeshAnalyzer.h"

namespace
{
	constexpr double Epsilon = 1.e-8;

	bool IsFinite(const FVector& V)
	{
		return FMath::IsFinite(V.X)
			&& FMath::IsFinite(V.Y)
			&& FMath::IsFinite(V.Z);
	}

	bool IsFinite(const FVector2D& V)
	{
		return FMath::IsFinite(V.X)
			&& FMath::IsFinite(V.Y);
	}

	bool Inside(
		const FVector2D& P,
		const FVector2D& A,
		const FVector2D& B,
		const FVector2D& C)
	{
		const auto S = [](
			const FVector2D& X,
			const FVector2D& Y,
			const FVector2D& Z)
		{
			return (X.X - Z.X) * (Y.Y - Z.Y)
				- (Y.X - Z.X) * (X.Y - Z.Y);
		};

		const double A0 = S(P, A, B);
		const double A1 = S(P, B, C);
		const double A2 = S(P, C, A);

		return !(
			(A0 < 0 || A1 < 0 || A2 < 0)
			&& (A0 > 0 || A1 > 0 || A2 > 0));
	}

	bool ComputeFlow(
		const FWetClothingAssetUVTriangle& T,
		FVector2D& OutDirection,
		float& OutSlope)
	{
		const FVector E1 =
			T.LocalPositions[1] - T.LocalPositions[0];

		const FVector E2 =
			T.LocalPositions[2] - T.LocalPositions[0];

		const FVector Cross = FVector::CrossProduct(E1, E2);

		if (!IsFinite(Cross) || Cross.SizeSquared() <= Epsilon)
		{
			return false;
		}

		const FVector N = Cross.GetSafeNormal();
		const FVector Gravity(0, 0, -1);

		const FVector Surface =
			Gravity - N * FVector::DotProduct(Gravity, N);

		OutSlope = Surface.Length();

		if (!FMath::IsFinite(OutSlope)
			|| OutSlope <= KINDA_SMALL_NUMBER)
		{
			OutDirection = FVector2D::ZeroVector;
			OutSlope = 0;
			return true;
		}

		const FVector2D DU1 = T.UVs[1] - T.UVs[0];
		const FVector2D DU2 = T.UVs[2] - T.UVs[0];

		const double Det =
			DU1.X * DU2.Y - DU1.Y * DU2.X;

		if (FMath::Abs(Det) <= Epsilon)
		{
			return false;
		}

		const FVector DPDU =
			(E1 * DU2.Y - E2 * DU1.Y) / Det;

		const FVector DPDV =
			(-E1 * DU2.X + E2 * DU1.X) / Det;

		const double A =
			FVector::DotProduct(DPDU, DPDU);

		const double B =
			FVector::DotProduct(DPDU, DPDV);

		const double D =
			FVector::DotProduct(DPDV, DPDV);

		const double GramDet = A * D - B * B;

		if (!IsFinite(DPDU)
			|| !IsFinite(DPDV)
			|| FMath::Abs(GramDet) <= Epsilon)
		{
			return false;
		}

		const double RHSU =
			FVector::DotProduct(DPDU, Surface);

		const double RHSV =
			FVector::DotProduct(DPDV, Surface);

		const FVector2D Flow(
			(RHSU * D - B * RHSV) / GramDet,
			(A * RHSV - B * RHSU) / GramDet);

		if (!IsFinite(Flow))
		{
			return false;
		}

		OutDirection = Flow.GetSafeNormal();
		return true;
	}
}

FString FWetClothingSurfaceWaterFlowMapBaker::MakeBuildSignature(
	const UWetClothingAsset* Asset,
	int32 MaterialSlotIndex)
{
	if (!Asset || Asset->GetRuntimeSkeletalMesh() == nullptr)
	{
		return FString();
	}

	const FSurfaceWaterSimulationSettings& S = Asset->SurfaceWaterSettings;
	const FSurfaceWaterMaterialSlotData* SlotData = S.FindMaterialSlot(MaterialSlotIndex);
	const FDWCDataUVPerLOD* DataUV = Asset->FindGeneratedDataUVForLOD(0);
	if (DataUV == nullptr || !DataUV->bIsValid)
	{
		return FString();
	}

	return FString::Printf(
		TEXT(
			"DWC.SurfaceWaterFlow.DataUV.v2|Mesh=%s|LOD=0|DataUVChannel=%d|DataUV=%s|"
			"Resolution=%d|Padding=%d|Gravity=0,0,-1|Slot=%d|WetSlots=%s"),
		*Asset->GetRuntimeSkeletalMesh()->GetPathName(),
		Asset->GetDWCDataUVChannelIndex(),
		*DataUV->MeshSignature,
		S.RenderTargetResolution,
		SlotData ? SlotData->BakedFlowMap.PaddingPixels : 4,
		MaterialSlotIndex,
		*Asset->GetPrecomputedSimulationData().MeshSignature);
}

bool FWetClothingSurfaceWaterFlowMapBaker::IsStale(
	const UWetClothingAsset* Asset)
{
	if (Asset == nullptr) return true;
	for (const FWetClothingWettableMaterialSlotState& Slot : Asset->PartData.EditableWetPartData.WettableMaterialSlots)
	{
		if (!Slot.bIsWettableSlot || Slot.MaterialSlotIndex == INDEX_NONE) continue;
		const FSurfaceWaterMaterialSlotData* Data = Asset->SurfaceWaterSettings.FindMaterialSlot(Slot.MaterialSlotIndex);
		if (!Data || !Data->BakedFlowMap.bIsValid || !Data->BakedFlowMap.FlowMap || Data->BakedFlowMap.BuildSignature != MakeBuildSignature(Asset, Slot.MaterialSlotIndex)) return true;
	}
	return false;
}

bool FWetClothingSurfaceWaterFlowMapBaker::CreateOrUpdateTexture(
	UWetClothingAsset& Asset,
	int32 MaterialSlotIndex,
	const TArray<FFloat16Color>& Pixels,
	int32 Resolution,
	UTexture2D*& OutTexture,
	FString& OutError)
{
	const FString Path =
		FPackageName::GetLongPackagePath(
			Asset.GetOutermost()->GetName());

	if (Path.IsEmpty())
	{
		OutError =
			TEXT("Could not resolve the Wet Clothing Asset package path.");
		return false;
	}

	const FString Name =
		ObjectTools::SanitizeObjectName(
			FString::Printf(
				TEXT("T_%s_SurfaceWaterFlowMap_Slot%d"),
				*Asset.GetName(), MaterialSlotIndex));

	const FString PackageName =
		Path / FString(TEXT("SurfaceWater")) / Name;

	const FString ObjectPath =
		PackageName + TEXT(".") + Name;

	UTexture2D* Texture =
		LoadObject<UTexture2D>(nullptr, *ObjectPath);

	if (!Texture)
	{
		UPackage* Package = CreatePackage(*PackageName);

		if (!Package)
		{
			OutError = TEXT("Could not create Flow Map package.");
			return false;
		}

		Texture = NewObject<UTexture2D>(
			Package,
			*Name,
			RF_Public | RF_Standalone | RF_Transactional);

		FAssetRegistryModule::AssetCreated(Texture);
	}
	else
	{
		Texture->Modify();
	}

	Texture->Source.Init(
		Resolution,
		Resolution,
		1,
		1,
		TSF_RGBA16F,
		reinterpret_cast<const uint8*>(Pixels.GetData()));

	Texture->SRGB = false;
	Texture->CompressionSettings = TC_HDR;
	Texture->MipGenSettings = TMGS_NoMipmaps;
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Clamp;
	Texture->Filter = TF_Bilinear;

	Texture->PostEditChange();
	Texture->UpdateResource();
	Texture->MarkPackageDirty();

	OutTexture = Texture;
	return true;
}

bool FWetClothingSurfaceWaterFlowMapBaker::Bake(
	UWetClothingAsset* Asset,
	FString& OutError)
{
	OutError.Reset();

	if (!Asset || Asset->GetRuntimeSkeletalMesh() == nullptr)
	{
		OutError =
			TEXT("Surface Water Flow Map requires a target mesh.");
		return false;
	}

	FAssetCompilingManager::Get().FinishCompilationForObjects({Asset->GetRuntimeSkeletalMesh()});

	FSurfaceWaterSimulationSettings& S =
		Asset->SurfaceWaterSettings;

	if (!S.bEnabled)
	{
		OutError =
			TEXT("Surface Water Flow Map is disabled on this asset.");
		return false;
	}

	const int32 Resolution =
		FMath::Clamp(S.RenderTargetResolution, 16, 4096);

	const FDWCDataUVPerLOD* DataUV = Asset->FindGeneratedDataUVForLOD(0);
	USkeletalMesh* RuntimeMesh = Asset->GetRuntimeSkeletalMesh();
	const FSkeletalMeshRenderData* RenderData = RuntimeMesh != nullptr ? RuntimeMesh->GetResourceForRendering() : nullptr;
	const int32 VertexCount = RenderData != nullptr && RenderData->LODRenderData.IsValidIndex(0)
		? static_cast<int32>(RenderData->LODRenderData[0].GetNumVertices())
		: 0;
	if (Asset->GetDWCDataUVChannelIndex() == INDEX_NONE ||
		DataUV == nullptr ||
		!DataUV->bIsValid ||
		DataUV->RenderVertexCount != VertexCount ||
		DataUV->DataUVs.Num() != VertexCount)
	{
		OutError = TEXT("Surface Water Flow Map requires valid DWC Data UV. Rebuild DWC Data UV before baking Flow Maps.");
		return false;
	}

	Asset->Modify();
	int32 BakedSlotCount = 0;

	for (const FWetClothingWettableMaterialSlotState& Slot
		: Asset->PartData.EditableWetPartData.WettableMaterialSlots)
	{
		if (!Slot.bIsWettableSlot
			|| Slot.MaterialSlotIndex == INDEX_NONE)
		{
			continue;
		}

		FSurfaceWaterMaterialSlotData* SlotData = S.SurfaceWaterMaterialSlots.FindByPredicate(
			[&Slot](const FSurfaceWaterMaterialSlotData& Candidate)
			{
				return Candidate.MaterialSlotIndex == Slot.MaterialSlotIndex;
			});
		if (!SlotData)
		{
			SlotData = &S.SurfaceWaterMaterialSlots.AddDefaulted_GetRef();
			SlotData->MaterialSlotIndex = Slot.MaterialSlotIndex;
		}
		if (!SlotData->bEnabled || !SlotData->BakedFlowMap.bEnabled) continue;

		TArray<FFloat16Color> Pixels;
		Pixels.Init(FFloat16Color(FLinearColor(.5f, .5f, 0, 0)), Resolution * Resolution);
		TArray<int32> Labels;
		Labels.Init(INDEX_NONE, Pixels.Num());
		int32 Painted = 0;
		int32 CandidateTriangleCount = 0;
		int32 OutOfRangeUVTriangleCount = 0;
		int32 DegenerateTriangleCount = 0;

		TArray<FWetClothingAssetUVIsland> Islands;
		FString Error;

		if (!FWetClothingAssetMeshAnalyzer::BuildMaterialSlotDataUVIslands(
				*Asset,
				0,
				Slot.MaterialSlotIndex,
				Islands,
				&Error))
		{
			OutError = Error;
			return false;
		}

		for (const FWetClothingAssetUVIsland& Island : Islands)
		{
			for (const FWetClothingAssetUVTriangle& T: Island.UVTriangles)
			{
				++CandidateTriangleCount;

				const bool bHasInvalidUV =
					!IsFinite(T.UVs[0])
					|| !IsFinite(T.UVs[1])
					|| !IsFinite(T.UVs[2])
					|| T.UVs[0].X < 0
					|| T.UVs[0].X > 1
					|| T.UVs[0].Y < 0
					|| T.UVs[0].Y > 1
					|| T.UVs[1].X < 0
					|| T.UVs[1].X > 1
					|| T.UVs[1].Y < 0
					|| T.UVs[1].Y > 1
					|| T.UVs[2].X < 0
					|| T.UVs[2].X > 1
					|| T.UVs[2].Y < 0
					|| T.UVs[2].Y > 1;

				if (bHasInvalidUV)
				{
					++OutOfRangeUVTriangleCount;
					continue;
				}

				FVector2D Direction;
				float Slope;

				if (!ComputeFlow(T, Direction, Slope))
				{
					++DegenerateTriangleCount;
					continue;
				}

				const FFloat16Color Color(
					FLinearColor(
						.5f + .5f * Direction.X,
						.5f + .5f * Direction.Y,
						FMath::Clamp(Slope, 0.f, 1.f),
						1.f));

				const int32 Label =
					Slot.MaterialSlotIndex * 100000
					+ Island.UVIslandID;

				const int32 MinX = FMath::Clamp(
					FMath::FloorToInt(
						FMath::Min3(
							T.UVs[0].X,
							T.UVs[1].X,
							T.UVs[2].X)
						* Resolution),
					0,
					Resolution - 1);

				const int32 MaxX = FMath::Clamp(
					FMath::FloorToInt(
						FMath::Max3(
							T.UVs[0].X,
							T.UVs[1].X,
							T.UVs[2].X)
						* Resolution),
					0,
					Resolution - 1);

				const int32 MinY = FMath::Clamp(
					FMath::FloorToInt(
						FMath::Min3(
							T.UVs[0].Y,
							T.UVs[1].Y,
							T.UVs[2].Y)
						* Resolution),
					0,
					Resolution - 1);

				const int32 MaxY = FMath::Clamp(
					FMath::FloorToInt(
						FMath::Max3(
							T.UVs[0].Y,
							T.UVs[1].Y,
							T.UVs[2].Y)
						* Resolution),
					0,
					Resolution - 1);

				for (int32 Y = MinY; Y <= MaxY; ++Y)
				{
					for (int32 X = MinX; X <= MaxX; ++X)
					{
						const FVector2D SamplePosition(
							(X + .5) / Resolution,
							(Y + .5) / Resolution);

						if (!Inside(
								SamplePosition,
								T.UVs[0],
								T.UVs[1],
								T.UVs[2]))
						{
							continue;
						}

						const int32 I = Y * Resolution + X;

						if (Labels[I] != INDEX_NONE
							&& Labels[I] != Label)
						{
							OutError = TEXT(
								"DWC Data UV islands overlap in "
								"texture space; Flow Map bake aborted.");

							return false;
						}

						if (Labels[I] == INDEX_NONE)
						{
							++Painted;
						}

						Labels[I] = Label;
						Pixels[I] = Color;
					}
				}
			}
		}

	if (Painted == 0)
	{
		OutError = FString::Printf(
			TEXT(
				"No valid DWC Data UV triangles were rasterized. "
				"WettableSlots=%d, CandidateTriangles=%d, "
				"OutOfRangeOrNonFiniteUV=%d, Degenerate3DOrUV=%d. "
				"Ensure DWC Data UV is a non-degenerate 0-1 unwrap "
				"on a wettable material slot."),
			1,
			CandidateTriangleCount,
			OutOfRangeUVTriangleCount,
			DegenerateTriangleCount);

		return false;
	}

	const int32 PaddingSteps =
		FMath::Clamp(SlotData->BakedFlowMap.PaddingPixels, 0, 64);

	for (int32 Step = 0; Step < PaddingSteps; ++Step)
	{
		const TArray<int32> Previous = Labels;
		const TArray<FFloat16Color> PreviousPixels = Pixels;

		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				const int32 I = Y * Resolution + X;

				if (Previous[I] != INDEX_NONE)
				{
					continue;
				}

				for (int32 DY = -1;
					DY <= 1 && Labels[I] == INDEX_NONE;
					++DY)
				{
					for (int32 DX = -1;
						DX <= 1 && Labels[I] == INDEX_NONE;
						++DX)
					{
						const int32 NX = X + DX;
						const int32 NY = Y + DY;

						if (NX < 0
							|| NY < 0
							|| NX >= Resolution
							|| NY >= Resolution)
						{
							continue;
						}

						const int32 N = NY * Resolution + NX;

						if (Previous[N] != INDEX_NONE)
						{
							Labels[I] = Previous[N];
							Pixels[I] = PreviousPixels[N];
						}
					}
				}
			}
		}
	}

	UTexture2D* Texture = nullptr;

	if (!CreateOrUpdateTexture(
			*Asset,
			Slot.MaterialSlotIndex,
			Pixels,
			Resolution,
			Texture,
			OutError))
	{
		return false;
	}

	FSurfaceWaterBakedFlowMapData& B =
		SlotData->BakedFlowMap;

	B.bIsValid = true;
	B.SourceLODIndex = 0;
	B.Resolution = Resolution;
	B.FlowMap = Texture;
	B.BuildSignature = MakeBuildSignature(Asset, Slot.MaterialSlotIndex);
	B.BakeGuid = FGuid::NewGuid();
	++BakedSlotCount;
	}

	if (BakedSlotCount == 0)
	{
		OutError = TEXT("No enabled wettable material slots were available for Surface Water Flow Map bake.");
		return false;
	}

	Asset->MarkPackageDirty();

	return true;
}
