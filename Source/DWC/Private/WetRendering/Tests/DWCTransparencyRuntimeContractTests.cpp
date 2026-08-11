// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "DataAssets/WetClothingTransparencyData.h"
#include "Engine/Texture2D.h"
#include "WetRendering/DWCTransparencyRuntimeContract.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    UTexture2D* MakeRuntimeTexture()
    {
        return UTexture2D::CreateTransient(4, 4, PF_B8G8R8A8);
    }

    FWetClothingBakedTransparencyMap MakeRuntimeMap(UTexture2D* TransparencyTexture)
    {
        FWetClothingBakedTransparencyMap Map;
        Map.TransparencyMap = TransparencyTexture;
        Map.BakeGuid = FGuid::NewGuid();
        Map.BuildSignature = TEXT("Final");
        Map.bContainsColorRGB = true;
        Map.bContainsTransparencyAlpha = true;
        return Map;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyRuntimeBindingContractTest,
    "DWC.Runtime.Transparency.BindingContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyRuntimeBindingContractTest::RunTest(const FString&)
{
    UTexture2D* Transparency = MakeRuntimeTexture();
    UTexture2D* RevealNormal = MakeRuntimeTexture();
    FWetClothingBakedTransparencyMap Map = MakeRuntimeMap(Transparency);

    FDWCTransparencyRuntimeBinding Binding =
        FDWCTransparencyRuntimeContract::Resolve(&Map);
    TestEqual(TEXT("Manual-color runtime binds the final transparency map."),
        Binding.TransparencyMap, Transparency);
    TestNull(TEXT("Reveal Normal is optional for a manual-color runtime map."),
        Binding.RevealNormalMap);

    Map.RevealNormalMap = RevealNormal;
    Map.RevealNormalBuildSignature = TEXT("RevealNormal");
    Map.bSourceCoverageBakedIntoRevealNormal = true;
    Binding = FDWCTransparencyRuntimeContract::Resolve(&Map);
    TestEqual(TEXT("A canonical Reveal Normal is bound."),
        Binding.RevealNormalMap, RevealNormal);

    FWetClothingTransparencyLayerData Layer;
    Layer.SourceType = EDWCTransparencySourceType::SameMeshMaterialSlots;
    Layer.bEnableRevealNormal = true;
    Layer.RevealNormalStrength = 3.25f;
    Binding = FDWCTransparencyRuntimeContract::Resolve(&Map, &Layer);
    TestEqual(TEXT("The layer's Reveal Normal strength is bound."),
        Binding.RevealNormalStrength, 3.25f);

    Layer.RevealNormalStrength = 8.0f;
    Binding = FDWCTransparencyRuntimeContract::Resolve(&Map, &Layer);
    TestEqual(TEXT("Reveal Normal strength is clamped to the material contract."),
        Binding.RevealNormalStrength, 4.0f);

    Layer.bEnableRevealNormal = false;
    Binding = FDWCTransparencyRuntimeContract::Resolve(&Map, &Layer);
    TestNull(TEXT("A disabled layer does not bind the Reveal Normal."),
        Binding.RevealNormalMap);
    TestEqual(TEXT("A disabled layer resets Reveal Normal strength."),
        Binding.RevealNormalStrength, 0.0f);
    TestEqual(TEXT("Disabling Reveal Normal preserves the Transparency Map binding."),
        Binding.TransparencyMap, Transparency);

    Map.bSourceCoverageBakedIntoRevealNormal = false;
    Binding = FDWCTransparencyRuntimeContract::Resolve(&Map);
    TestNull(TEXT("An incomplete Reveal Normal payload is never bound."),
        Binding.RevealNormalMap);

    Map.BuildSignature.Reset();
    Binding = FDWCTransparencyRuntimeContract::Resolve(&Map);
    TestNull(TEXT("An unusable final map binds no transparency texture."),
        Binding.TransparencyMap);
    TestNull(TEXT("An unusable final map binds no Reveal Normal texture."),
        Binding.RevealNormalMap);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyRuntimeTextureMemoryContractTest,
    "DWC.Runtime.Memory.Transparency.TextureResidency",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyRuntimeTextureMemoryContractTest::RunTest(const FString&)
{
    UTexture2D* Transparency = MakeRuntimeTexture();
    UTexture2D* RevealNormal = MakeRuntimeTexture();
    FWetClothingBakedTransparencyMap Map = MakeRuntimeMap(Transparency);
    Map.RevealNormalMap = RevealNormal;
    Map.RevealNormalBuildSignature = TEXT("RevealNormal");
    Map.bSourceCoverageBakedIntoRevealNormal = true;

    const FDWCTransparencyRuntimeBinding Binding =
        FDWCTransparencyRuntimeContract::Resolve(&Map);
    TSet<const UTexture2D*> SeenTextures;
    uint32 TextureCount = 0;
    uint64 TextureBytes = 0;
    FDWCTransparencyRuntimeContract::AccumulateResidentTextureUsage(
        Binding, SeenTextures, TextureCount, TextureBytes);

    const uint64 ExpectedBytes =
        Transparency->CalcTextureMemorySizeEnum(TMC_ResidentMips) +
        RevealNormal->CalcTextureMemorySizeEnum(TMC_ResidentMips);
    TestEqual(TEXT("Transparency and Reveal Normal are both counted."),
        TextureCount, 2u);
    TestEqual(TEXT("Resident bytes include both runtime textures."),
        TextureBytes, ExpectedBytes);

    FDWCTransparencyRuntimeContract::AccumulateResidentTextureUsage(
        Binding, SeenTextures, TextureCount, TextureBytes);
    TestEqual(TEXT("Shared runtime textures are counted only once."),
        TextureCount, 2u);
    TestEqual(TEXT("Shared runtime texture bytes are counted only once."),
        TextureBytes, ExpectedBytes);

    Map.RevealNormalMap = Transparency;
    const FDWCTransparencyRuntimeBinding AliasedBinding =
        FDWCTransparencyRuntimeContract::Resolve(&Map);
    SeenTextures.Reset();
    TextureCount = 0;
    TextureBytes = 0;
    FDWCTransparencyRuntimeContract::AccumulateResidentTextureUsage(
        AliasedBinding, SeenTextures, TextureCount, TextureBytes);
    TestEqual(TEXT("Aliased final and normal textures remain unique."),
        TextureCount, 1u);
    return true;
}

#endif
