param(
    [string]$EngineRoot = "C:\Program Files\Epic Games\UE_5.8\Engine",
    [string]$ProjectPath = "",
    [switch]$SkipReadOnlyClear
)

$ErrorActionPreference = "Stop"

function Resolve-FullPath([string]$Path) {
    return [System.IO.Path]::GetFullPath($Path)
}

$ScriptDir = Resolve-FullPath $PSScriptRoot
$PluginRoot = Resolve-FullPath (Join-Path $ScriptDir "..\..\..")

if ([string]::IsNullOrWhiteSpace($ProjectPath)) {
    $ProjectRoot = Resolve-FullPath (Join-Path $PluginRoot "..\..")
    $ProjectCandidates = Get-ChildItem -LiteralPath $ProjectRoot -Filter "*.uproject" -File
    if ($ProjectCandidates.Count -ne 1) {
        throw "Could not uniquely resolve the .uproject under '$ProjectRoot'. Pass -ProjectPath explicitly."
    }
    $ProjectPath = $ProjectCandidates[0].FullName
}

$UnrealEditorCmd = Join-Path $EngineRoot "Binaries\Win64\UnrealEditor-Cmd.exe"
if (!(Test-Path -LiteralPath $UnrealEditorCmd)) {
    throw "UnrealEditor-Cmd.exe was not found: $UnrealEditorCmd"
}

if (!(Test-Path -LiteralPath $ProjectPath)) {
    throw "Project file was not found: $ProjectPath"
}

$MaterialFunctionScripts = @(
    @{
        Name = "MF_DWC_GetRenderProfile"
        Script = "create_mf_dwc_get_render_profile.py"
        Asset = "Content\Materials\Functions\MF_DWC_GetRenderProfile.uasset"
    },
    @{
        Name = "MF_DWC_SampleSurfaceWaterNormals"
        Script = "create_mf_dwc_sample_surface_water_normals.py"
        Asset = "Content\Materials\Functions\MF_DWC_SampleSurfaceWaterNormals.uasset"
    },
    @{
        Name = "MF_DWC_EvaluateSurfaceAppearance"
        Script = "create_mf_dwc_evaluate_surface_appearance.py"
        Asset = "Content\Materials\Functions\MF_DWC_EvaluateSurfaceAppearance.uasset"
    },
    @{
        Name = "MF_DWC_DebugWetPartColor"
        Script = "create_mf_dwc_debug_wet_part_color.py"
        Asset = "Content\Materials\Functions\MF_DWC_DebugWetPartColor.uasset"
    }
)

Write-Host "[DWC MF] Project: $ProjectPath"
Write-Host "[DWC MF] Engine : $EngineRoot"

foreach ($Entry in $MaterialFunctionScripts) {
    $EntryName = $Entry["Name"]
    $ScriptPath = Join-Path $ScriptDir $Entry["Script"]
    $AssetPath = Join-Path $PluginRoot $Entry["Asset"]

    if (!(Test-Path -LiteralPath $ScriptPath)) {
        throw "Missing Python script for ${EntryName}: $ScriptPath"
    }

    if (!$SkipReadOnlyClear -and (Test-Path -LiteralPath $AssetPath)) {
        Set-ItemProperty -LiteralPath $AssetPath -Name IsReadOnly -Value $false
    }

    Write-Host ""
    Write-Host "[DWC MF] Running ${EntryName}"
    & $UnrealEditorCmd $ProjectPath -run=pythonscript "-script=$ScriptPath" -unattended -nop4 -nosplash

    if ($LASTEXITCODE -ne 0) {
        throw "${EntryName} failed with exit code $LASTEXITCODE"
    }
}

Write-Host ""
Write-Host "[DWC MF] All material function scripts completed successfully."
