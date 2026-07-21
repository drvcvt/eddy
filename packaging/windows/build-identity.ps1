[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$BuildDirectory,
    [Parameter(Mandatory = $true)][string]$ApplicationDirectory,
    [Parameter(Mandatory = $true)][string]$Version
)

$ErrorActionPreference = "Stop"
$build = [System.IO.Path]::GetFullPath($BuildDirectory)
$application = [System.IO.Path]::GetFullPath($ApplicationDirectory)
$identity = Join-Path $build "identity"
$packageStage = Join-Path $identity "package"
$certificateDirectory = Join-Path $identity "certificate"
$manifestTemplate = Join-Path $PSScriptRoot "identity\AppxManifest.xml.in"
$package = Join-Path $application "Eddy.ContextMenu.msix"
$publicCertificate = Join-Path $application "Eddy.ContextMenu.cer"
$pfx = Join-Path $certificateDirectory "Eddy.ContextMenu.pfx"
$savedCertificate = Join-Path $certificateDirectory "Eddy.ContextMenu.cer"
$passwordFile = Join-Path $certificateDirectory "password.txt"

$sdkRoot = "${env:ProgramFiles(x86)}\Windows Kits\10\bin"
$sdkTools = Get-ChildItem -LiteralPath $sdkRoot -Directory |
    Sort-Object Name -Descending |
    ForEach-Object {
        $makeAppx = Join-Path $_.FullName "x64\makeappx.exe"
        $signTool = Join-Path $_.FullName "x64\signtool.exe"
        if ((Test-Path -LiteralPath $makeAppx -PathType Leaf) -and
            (Test-Path -LiteralPath $signTool -PathType Leaf)) {
            [PSCustomObject]@{ MakeAppx = $makeAppx; SignTool = $signTool }
        }
    } | Select-Object -First 1
if (-not $sdkTools) {
    throw "Windows SDK packaging tools were not found below: $sdkRoot"
}

New-Item -ItemType Directory -Path $packageStage, $certificateDirectory -Force | Out-Null
Get-ChildItem -LiteralPath $packageStage -Force | Remove-Item -Recurse -Force

$versionParts = @($Version.Split('.'))
while ($versionParts.Count -lt 4) { $versionParts += "0" }
$packageVersion = ($versionParts[0..3] -join '.')
$manifest = (Get-Content -LiteralPath $manifestTemplate -Raw).Replace("@VERSION@", $packageVersion)
Set-Content -LiteralPath (Join-Path $packageStage "AppxManifest.xml") -Value $manifest -Encoding UTF8

if (-not ((Test-Path -LiteralPath $pfx -PathType Leaf) -and
          (Test-Path -LiteralPath $savedCertificate -PathType Leaf) -and
          (Test-Path -LiteralPath $passwordFile -PathType Leaf))) {
    $random = [byte[]]::new(32)
    [System.Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($random)
    $password = [Convert]::ToBase64String($random)
    Set-Content -LiteralPath $passwordFile -Value $password -Encoding ASCII -NoNewline
    $securePassword = ConvertTo-SecureString $password -AsPlainText -Force
    $certificate = New-SelfSignedCertificate `
        -Type Custom `
        -Subject "CN=drvcvt" `
        -FriendlyName "Eddy context menu package signing" `
        -CertStoreLocation "Cert:\CurrentUser\My" `
        -KeyAlgorithm RSA `
        -KeyLength 2048 `
        -HashAlgorithm SHA256 `
        -KeyExportPolicy Exportable `
        -KeyUsage DigitalSignature `
        -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3") `
        -NotAfter (Get-Date).AddYears(5)
    try {
        Export-PfxCertificate -Cert $certificate -FilePath $pfx -Password $securePassword | Out-Null
        Export-Certificate -Cert $certificate -FilePath $savedCertificate | Out-Null
    } finally {
        Remove-Item -LiteralPath "Cert:\CurrentUser\My\$($certificate.Thumbprint)" -Force
    }
}

$password = Get-Content -LiteralPath $passwordFile -Raw
Copy-Item -LiteralPath $savedCertificate -Destination $publicCertificate -Force

Add-Type -AssemblyName System.Drawing
$logo = [System.Drawing.Bitmap]::new(150, 150)
$graphics = [System.Drawing.Graphics]::FromImage($logo)
try {
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.Clear([System.Drawing.Color]::FromArgb(0x12, 0x12, 0x12))
    $font = [System.Drawing.Font]::new("Segoe UI", 88, [System.Drawing.FontStyle]::Bold,
        [System.Drawing.GraphicsUnit]::Pixel)
    $brush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(0xe6, 0xe6, 0xe6))
    try {
        $format = [System.Drawing.StringFormat]::new()
        $format.Alignment = [System.Drawing.StringAlignment]::Center
        $format.LineAlignment = [System.Drawing.StringAlignment]::Center
        $graphics.DrawString("e", $font, $brush, [System.Drawing.RectangleF]::new(0, -5, 150, 150), $format)
        $format.Dispose()
    } finally {
        $brush.Dispose()
        $font.Dispose()
    }
    $logo.Save((Join-Path $application "eddy-logo.png"), [System.Drawing.Imaging.ImageFormat]::Png)
} finally {
    $graphics.Dispose()
    $logo.Dispose()
}

& $sdkTools.MakeAppx pack /d $packageStage /p $package /o /nv
if ($LASTEXITCODE -ne 0) {
    throw "MakeAppx failed with exit code $LASTEXITCODE"
}
& $sdkTools.SignTool sign /fd SHA256 /f $pfx /p $password $package
if ($LASTEXITCODE -ne 0) {
    throw "SignTool failed with exit code $LASTEXITCODE"
}

Write-Output $package
