param(
    [string]$EngineRoot = "C:\Program Files\Epic Games\UE_5.8"
)

$ErrorActionPreference = "Stop"

if (Get-Process -Name "UnrealEditor" -ErrorAction SilentlyContinue) {
    throw "Unreal Editor is running. Save your work, close the Editor completely, then run this script again."
}

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$pluginRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptDirectory "..\.."))
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $pluginRoot "..\.."))
$projectFiles = @(Get-ChildItem -LiteralPath $projectRoot -Filter "*.uproject" -File)
if ($projectFiles.Count -ne 1) {
    throw "Expected exactly one .uproject under '$projectRoot', found $($projectFiles.Count)."
}

$projectFile = $projectFiles[0].FullName
$editorTarget = "$($projectFiles[0].BaseName)Editor"
$dotnetRoot = Join-Path $EngineRoot "Engine\Binaries\ThirdParty\DotNet\10.0\win-x64"
$buildScript = Join-Path $EngineRoot "Engine\Build\BatchFiles\Build.bat"
if (-not (Test-Path -LiteralPath (Join-Path $dotnetRoot "dotnet.exe"))) {
    throw "UE 5.8 bundled .NET 10 was not found under '$dotnetRoot'."
}
if (-not (Test-Path -LiteralPath $buildScript)) {
    throw "UE 5.8 Build.bat was not found at '$buildScript'."
}

$env:DOTNET_ROOT = $dotnetRoot
$env:PATH = "$dotnetRoot;$env:PATH"

Write-Host "Building $editorTarget with UE 5.8 bundled .NET 10..."
& $buildScript `
    $editorTarget `
    Win64 `
    Development `
    "-Project=$projectFile" `
    -WaitMutex `
    -architecture=x64

if ($LASTEXITCODE -ne 0) {
    throw "UnrealBuildTool failed with exit code $LASTEXITCODE."
}

Write-Host "SUCCESS. Restart Unreal Editor, then run regenerate_dwc_surface_water.py."
