[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$InstallDirectory)

$ErrorActionPreference = "Stop"
$packageName = "drvcvt.Eddy.ContextMenu"
$packagePath = Join-Path $InstallDirectory "Eddy.ContextMenu.msix"

Get-AppxPackage -Name $packageName -ErrorAction SilentlyContinue |
    Remove-AppxPackage -ErrorAction SilentlyContinue
Add-AppxPackage -Path $packagePath -ExternalLocation $InstallDirectory -ForceUpdateFromAnyVersion
