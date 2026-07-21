$ErrorActionPreference = "SilentlyContinue"
$packageName = "drvcvt.Eddy.ContextMenu"

Get-AppxPackage -Name $packageName | Remove-AppxPackage
