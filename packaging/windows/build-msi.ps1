[CmdletBinding()]
param(
    [string]$BuildDirectory = "build-win",
    [string]$QtDirectory = "",
    [string]$Version = "0.1.0",
    [string]$OutputDirectory = "dist\msi"
)

$ErrorActionPreference = "Stop"
$repository = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$build = [System.IO.Path]::GetFullPath((Join-Path $repository $BuildDirectory))
$stage = [System.IO.Path]::GetFullPath((Join-Path $build "msi\stage"))
$output = [System.IO.Path]::GetFullPath((Join-Path $repository $OutputDirectory))
$releaseExecutable = Join-Path $build "Release\eddy.exe"
if (-not (Test-Path -LiteralPath $releaseExecutable -PathType Leaf)) {
    $releaseExecutable = Join-Path $build "eddy.exe"
}
$windeployqt = if ($QtDirectory) {
    Join-Path $QtDirectory "bin\windeployqt.exe"
} else {
    (Get-Command "windeployqt.exe" -ErrorAction Stop).Source
}
$wixDirectory = Join-Path $build "wix"
$wix = Join-Path $wixDirectory "wix.exe"
$source = Join-Path $PSScriptRoot "Eddy.wxs"
$license = Join-Path $PSScriptRoot "License.rtf"

foreach ($required in @($releaseExecutable, $windeployqt, $source, $license)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required file not found: $required"
    }
}

if (-not $stage.StartsWith($build, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to clean staging directory outside the build tree: $stage"
}
if (Test-Path -LiteralPath $stage) {
    Remove-Item -LiteralPath $stage -Recurse -Force
}
New-Item -ItemType Directory -Path $stage -Force | Out-Null
New-Item -ItemType Directory -Path $output -Force | Out-Null

Copy-Item -LiteralPath $releaseExecutable -Destination (Join-Path $stage "eddy.exe")
& $windeployqt --release --compiler-runtime --dir $stage $releaseExecutable
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE"
}

if (-not (Test-Path -LiteralPath $wix -PathType Leaf)) {
    New-Item -ItemType Directory -Path $wixDirectory -Force | Out-Null
    & dotnet tool install --tool-path $wixDirectory wix --version 5.0.2
    if ($LASTEXITCODE -ne 0) {
        throw "Could not install the local WiX build tool"
    }
}
& $wix extension add --global WixToolset.UI.wixext/5.0.2
if ($LASTEXITCODE -ne 0) {
    throw "Could not install the WiX UI extension"
}

$msi = Join-Path $output "Eddy-$Version-windows-x64.msi"
& $wix build $source -arch x64 -culture en-US -ext WixToolset.UI.wixext `
    -d "SourceDir=$stage" -d "ProductVersion=$Version" -d "LicenseRtf=$license" -o $msi
if ($LASTEXITCODE -ne 0) {
    throw "WiX failed with exit code $LASTEXITCODE"
}

Write-Output $msi
