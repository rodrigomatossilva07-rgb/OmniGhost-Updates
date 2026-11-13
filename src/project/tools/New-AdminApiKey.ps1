[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9_.-]{1,64}$')]
    [string]$Actor
)

$secretBytes = [byte[]]::new(32)
$rng = [System.Security.Cryptography.RandomNumberGenerator]::Create()
try { $rng.GetBytes($secretBytes) } finally { $rng.Dispose() }
$secret = [Convert]::ToBase64String($secretBytes).TrimEnd('=').Replace('+', '-').Replace('/', '_')
$sha = [System.Security.Cryptography.SHA256]::Create()
try { $hashBytes = $sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($secret)) } finally { $sha.Dispose() }
$hash = ([BitConverter]::ToString($hashBytes)).Replace('-', '').ToLowerInvariant()

Write-Host 'Guarda o token exclusivamente no gestor de segredos do operador:'
Write-Output ("{0}.{1}" -f $Actor, $secret)
Write-Host ''
Write-Host 'Adiciona apenas este valor ao servidor em OMNIGHOST_ADMIN_API_KEY_HASHES:'
Write-Output ("{0}={1}" -f $Actor, $hash)
