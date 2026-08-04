#include "WetClothing/Foundation/Preview/Diagnostics/DWCEditorPreviewDiagnostics.h"

#include "Engine/Texture2D.h"
#include "WetClothing/Foundation/Preview/Session/DWCEditorPreviewSession.h"

DEFINE_LOG_CATEGORY(LogDWCEditorPreview);

namespace
{
    TArray<FDWCEditorPreviewSession*>& GetActivePreviewSessions()
    {
        static TArray<FDWCEditorPreviewSession*> Sessions;
        return Sessions;
    }
}

void FDWCEditorPreviewDiagnostics::RegisterSession(FDWCEditorPreviewSession* Session)
{
    if (Session != nullptr)
    {
        GetActivePreviewSessions().AddUnique(Session);
    }
}

void FDWCEditorPreviewDiagnostics::UnregisterSession(FDWCEditorPreviewSession* Session)
{
    GetActivePreviewSessions().RemoveSingleSwap(Session, EAllowShrinking::No);
}

void FDWCEditorPreviewDiagnostics::DumpAllSessions()
{
    const TArray<FDWCEditorPreviewSession*>& Sessions = GetActivePreviewSessions();
    UE_LOG(
        LogDWCEditorPreview,
        Display,
        TEXT("DWC editor preview diagnostics: %d active session(s)."),
        Sessions.Num());

    for (int32 SessionIndex = 0; SessionIndex < Sessions.Num(); ++SessionIndex)
    {
        if (const FDWCEditorPreviewSession* Session = Sessions[SessionIndex])
        {
            Session->DumpDiagnostics(SessionIndex);
        }
    }
}

void FDWCEditorPreviewDiagnostics::ResetAllCounters()
{
    for (FDWCEditorPreviewSession* Session : GetActivePreviewSessions())
    {
        if (Session != nullptr)
        {
            Session->ResetDiagnosticCounters();
        }
    }

    UE_LOG(LogDWCEditorPreview, Display, TEXT("Reset diagnostics for all active DWC editor preview sessions."));
}

uint64 FDWCEditorPreviewDiagnostics::EstimateTextureBytes(const UTexture2D* Texture)
{
    if (Texture == nullptr)
    {
        return 0;
    }

    uint64 BulkDataBytes = 0;
    if (const FTexturePlatformData* PlatformData = Texture->GetPlatformData())
    {
        for (const FTexture2DMipMap& Mip : PlatformData->Mips)
        {
            BulkDataBytes += static_cast<uint64>(FMath::Max<int64>(Mip.BulkData.GetBulkDataSize(), 0));
        }
    }

    const uint64 MinimumTextureBytes =
        static_cast<uint64>(FMath::Max(Texture->GetSizeX(), 0)) *
        static_cast<uint64>(FMath::Max(Texture->GetSizeY(), 0)) * sizeof(FColor);
    const uint64 ResidentResourceBytes =
        static_cast<uint64>(Texture->CalcTextureMemorySizeEnum(TMC_ResidentMips));
    return BulkDataBytes + FMath::Max(ResidentResourceBytes, MinimumTextureBytes);
}

FString FDWCEditorPreviewDiagnostics::FormatBytes(const uint64 Bytes)
{
    constexpr double BytesPerMiB = 1024.0 * 1024.0;
    return FString::Printf(TEXT("%.2f MiB"), static_cast<double>(Bytes) / BytesPerMiB);
}
