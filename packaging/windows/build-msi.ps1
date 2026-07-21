[CmdletBinding()]
param(
    [string]$BuildDirectory = "build-win",
    [string]$QtDirectory = "C:\Qt\6.9.3\msvc2022_64",
    [string]$Version = "0.1.3",
    [string]$OutputDirectory = "dist\msi"
)

$ErrorActionPreference = "Stop"
$repository = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$build = [System.IO.Path]::GetFullPath((Join-Path $repository $BuildDirectory))
$stage = [System.IO.Path]::GetFullPath((Join-Path $build "msi\stage"))
$output = [System.IO.Path]::GetFullPath((Join-Path $repository $OutputDirectory))
$releaseExecutable = Join-Path $build "Release\eddy.exe"
$explorerCommand = Join-Path $build "Release\eddy_explorer_command.dll"
$windeployqt = Join-Path $QtDirectory "bin\windeployqt.exe"
$wix = Join-Path $build "wix\wix.exe"
$source = Join-Path $PSScriptRoot "Eddy.wxs"
$license = Join-Path $PSScriptRoot "License.rtf"
$identityBuilder = Join-Path $PSScriptRoot "build-identity.ps1"
$registerContextMenu = Join-Path $PSScriptRoot "RegisterContextMenu.ps1"
$unregisterContextMenu = Join-Path $PSScriptRoot "UnregisterContextMenu.ps1"
$installCertificate = Join-Path $PSScriptRoot "InstallCertificate.ps1"
$removeCertificate = Join-Path $PSScriptRoot "RemoveCertificate.ps1"

foreach ($required in @($releaseExecutable, $explorerCommand, $windeployqt, $source, $license,
        $identityBuilder, $registerContextMenu, $unregisterContextMenu,
        $installCertificate, $removeCertificate)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required file not found: $required"
    }
}

if (-not (Test-Path -LiteralPath $wix -PathType Leaf)) {
    New-Item -ItemType Directory -Path (Split-Path $wix) -Force | Out-Null
    & dotnet tool install --tool-path (Split-Path $wix) wix --version 5.0.2
    if ($LASTEXITCODE -ne 0) {
        throw "Could not install the local WiX build tool"
    }
}

& $wix extension add --global WixToolset.UI.wixext/5.0.2
if ($LASTEXITCODE -ne 0) {
    throw "Could not install the WiX UI extension"
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
Copy-Item -LiteralPath $explorerCommand -Destination (Join-Path $stage "eddy_explorer_command.dll")
Copy-Item -LiteralPath $registerContextMenu -Destination $stage
Copy-Item -LiteralPath $unregisterContextMenu -Destination $stage
Copy-Item -LiteralPath $installCertificate -Destination $stage
Copy-Item -LiteralPath $removeCertificate -Destination $stage
& $windeployqt --release --dir $stage $releaseExecutable
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE"
}

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    throw "Visual Studio locator not found: $vswhere"
}
$visualStudio = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Redist.14.Latest -property installationPath
if (-not $visualStudio) {
    throw "A Visual Studio installation with the VC++ redistributable files is required"
}
$redistRoot = Join-Path $visualStudio "VC\Redist\MSVC"
$visualStudioRedist = Get-ChildItem -LiteralPath $redistRoot -Directory |
    ForEach-Object { Join-Path $_.FullName "x64\Microsoft.VC143.CRT" } |
    Where-Object { Test-Path -LiteralPath $_ -PathType Container } |
    Sort-Object -Descending |
    Select-Object -First 1
if (-not $visualStudioRedist) {
    throw "Visual C++ runtime directory not found below: $redistRoot"
}
Get-ChildItem -LiteralPath $visualStudioRedist -Filter "*.dll" -File |
    Copy-Item -Destination $stage

& $identityBuilder -BuildDirectory $build -ApplicationDirectory $stage -Version $Version
if ($LASTEXITCODE -ne 0) {
    throw "Could not build the Windows 11 context menu identity package"
}

$msi = Join-Path $output "Eddy-$Version-windows-x64.msi"
& $wix build $source -arch x64 -culture en-US -ext WixToolset.UI.wixext `
    -d "SourceDir=$stage" -d "ProductVersion=$Version" -d "LicenseRtf=$license" -o $msi
if ($LASTEXITCODE -ne 0) {
    throw "WiX failed with exit code $LASTEXITCODE"
}

Write-Output $msi
