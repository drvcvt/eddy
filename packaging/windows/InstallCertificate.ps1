$ErrorActionPreference = "Stop"
$certificatePath = Join-Path $PSScriptRoot "Eddy.ContextMenu.cer"
$certificate = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new($certificatePath)
$store = [System.Security.Cryptography.X509Certificates.X509Store]::new(
    [System.Security.Cryptography.X509Certificates.StoreName]::TrustedPeople,
    [System.Security.Cryptography.X509Certificates.StoreLocation]::LocalMachine)
$store.Open([System.Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
try {
    if (-not $store.Certificates.Find(
            [System.Security.Cryptography.X509Certificates.X509FindType]::FindByThumbprint,
            $certificate.Thumbprint,
            $false).Count) {
        $store.Add($certificate)
    }
} finally {
    $store.Close()
}
