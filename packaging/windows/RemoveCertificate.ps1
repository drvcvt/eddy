$ErrorActionPreference = "SilentlyContinue"
$certificatePath = Join-Path $PSScriptRoot "Eddy.ContextMenu.cer"
if (-not (Test-Path -LiteralPath $certificatePath -PathType Leaf)) {
    exit 0
}

$certificate = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new($certificatePath)
$store = [System.Security.Cryptography.X509Certificates.X509Store]::new(
    [System.Security.Cryptography.X509Certificates.StoreName]::TrustedPeople,
    [System.Security.Cryptography.X509Certificates.StoreLocation]::LocalMachine)
$store.Open([System.Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
try {
    $store.Remove($certificate)
} finally {
    $store.Close()
}
