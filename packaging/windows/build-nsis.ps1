[CmdletBinding()]
param(
    [string]$BuildDirectory = "build-win",
    [string]$QtDirectory = "",
    [Parameter(Mandatory = $true)]
    [string]$TesseractDirectory,
    [string]$Version = "0.1.0",
    [string]$OutputDirectory = "dist\nsis"
)

$ErrorActionPreference = "Stop"
$repository = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$build = [System.IO.Path]::GetFullPath((Join-Path $repository $BuildDirectory))
$stage = [System.IO.Path]::GetFullPath((Join-Path $build "nsis\stage"))
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
$source = Join-Path $PSScriptRoot "Eddy.nsi"
$license = Join-Path $PSScriptRoot "License.rtf"
$tesseract = Join-Path $TesseractDirectory "tesseract.exe"
$deu = Join-Path $TesseractDirectory "tessdata\deu.traineddata"
$osd = Join-Path $TesseractDirectory "tessdata\osd.traineddata"
$ocrLicense = Join-Path $TesseractDirectory "LICENSE"

foreach ($required in @($releaseExecutable, $windeployqt, $source, $license,
        $tesseract, $deu, $osd, $ocrLicense)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required file not found: $required"
    }
}

$makensis = Get-Command "makensis.exe" -ErrorAction SilentlyContinue |
    Select-Object -ExpandProperty Source -First 1
if (-not $makensis) {
    $makensis = @(
        "${env:ProgramFiles(x86)}\NSIS\makensis.exe",
        "$env:ProgramFiles\NSIS\makensis.exe",
        "$env:LOCALAPPDATA\Programs\NSIS\makensis.exe"
    ) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
}
if (-not $makensis) {
    throw "NSIS 3 was not found. Install NSIS or add makensis.exe to PATH."
}

$versionParts = @($Version.Split('.'))
if ($versionParts.Count -gt 4 -or ($versionParts | Where-Object { $_ -notmatch '^\d+$' })) {
    throw "NSIS requires a numeric version with at most four components: $Version"
}
while ($versionParts.Count -lt 4) { $versionParts += "0" }
$numericVersion = $versionParts -join '.'

if (-not $stage.StartsWith($build, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to clean staging directory outside the build tree: $stage"
}
if (Test-Path -LiteralPath $stage) {
    Remove-Item -LiteralPath $stage -Recurse -Force
}
New-Item -ItemType Directory -Path $stage -Force | Out-Null
New-Item -ItemType Directory -Path $output -Force | Out-Null

Copy-Item -LiteralPath $releaseExecutable -Destination (Join-Path $stage "eddy.exe")
Copy-Item -LiteralPath $TesseractDirectory -Destination (Join-Path $stage "ocr") -Recurse
& $windeployqt --release --compiler-runtime --dir $stage $releaseExecutable
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE"
}

$ocr = Join-Path $stage "ocr\tesseract.exe"
$ocrData = Join-Path $stage "ocr\tessdata"
$languages = & $ocr --tessdata-dir $ocrData --list-langs 2>&1
if ($LASTEXITCODE -ne 0 -or $languages -notcontains "deu" -or $languages -notcontains "osd") {
    throw "Packaged Tesseract smoke test failed: $($languages -join ' ')"
}

$installer = Join-Path $output "Eddy-$Version-windows-x64-setup.exe"
& $makensis /V3 /WX `
    "/DSourceDir=$stage" `
    "/DProductVersion=$Version" `
    "/DProductVersionNumeric=$numericVersion" `
    "/DLicenseRtf=$license" `
    "/DOutputFile=$installer" `
    $source
if ($LASTEXITCODE -ne 0) {
    throw "NSIS failed with exit code $LASTEXITCODE"
}

Write-Output $installer
