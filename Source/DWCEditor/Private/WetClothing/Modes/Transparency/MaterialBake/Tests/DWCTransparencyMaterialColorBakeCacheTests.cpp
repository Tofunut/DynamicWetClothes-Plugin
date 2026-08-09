//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "Misc/AutomationTest.h"
#include "WetClothing/Modes/Transparency/MaterialBake/DWCTransparencyMaterialColorBakeCache.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyMaterialColorBakeKeyTest,
    "DWC.Transparency.MaterialBake.KeyIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyMaterialColorBakeKeyTest::RunTest(const FString& Parameters)
{
    FDWCTransparencyMaterialColorBakeKey A;
    A.OwnerAssetPath = FSoftObjectPath(TEXT("/Game/Test/WCA_A.WCA_A"));
    A.SourceMeshPath = FSoftObjectPath(TEXT("/Game/Test/SK_Source.SK_Source"));
    A.MaterialSlotIndex = 3;
    A.SourceUVChannel = 1;
    A.Resolution = 2048;
    A.MaterialBakeSignature = TEXT("ABC");

    FDWCTransparencyMaterialColorBakeKey B = A;
    TestTrue(TEXT("An exact source dependency produces the same cache key."), A == B);
    TestEqual(TEXT("Equal keys produce equal hashes."), GetTypeHash(A), GetTypeHash(B));
    TestTrue(TEXT("A complete key is valid."), A.IsValid());

    B.SourceUVChannel = 2;
    TestFalse(TEXT("Source UV participates in cache identity."), A == B);
    B = A;
    B.Resolution = 4096;
    TestFalse(TEXT("Resolution participates in cache identity."), A == B);
    B = A;
    B.MaterialBakeSignature = TEXT("DEF");
    TestFalse(TEXT("Material state participates in cache identity."), A == B);
    B = A;
    B.OwnerAssetPath = FSoftObjectPath(TEXT("/Game/Test/WCA_B.WCA_B"));
    TestFalse(TEXT("Different WCA editor sessions do not share material color cache entries."), A == B);
    TestNotEqual(TEXT("WCA ownership participates in the cache hash."), GetTypeHash(A), GetTypeHash(B));
    return true;
}

#endif
