#pragma once

#include "CoreMinimal.h"

class UWetnessProfile;
struct FWetnessProfileParameters;

/**
 * Editor-side safety policy for Wetness Profile values.
 *
 * The runtime asset owns the reflected data schema. This policy prevents
 * invalid/legacy asset values from reaching the profile preview or the packed
 * render-profile LUT.
 */
class FWetnessProfileEditorPolicy
{
public:
    static bool SanitizeProfile(UWetnessProfile* Profile, TArray<FString>* OutChanges = nullptr);
    static bool SanitizeParameters(FWetnessProfileParameters& Parameters, TArray<FString>* OutChanges = nullptr);
    static void FindProfileIssues(const UWetnessProfile* Profile, TArray<FString>& OutIssues);

    /** Returns true only for compatibility fields that should not be exposed by the custom editor. */
    static bool IsObsoleteEditorPropertyPath(const FString& PropertyPath);
};
