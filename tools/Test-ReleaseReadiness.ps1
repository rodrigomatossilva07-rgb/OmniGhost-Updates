[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ProjectDir)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)

$license = [IO.File]::ReadAllText((Join-Path $ProjectDir 'src\licensing\license_service.cpp'))
$licenseHeader = [IO.File]::ReadAllText((Join-Path $ProjectDir 'src\licensing\license_service.h'))
if ($license -match 'TemporaryDevelopmentLicense|CreateTemporaryLocalLicense|ActivateLocalKey|kLocalVerifierSha256|localLicenseValid' -or
    $licenseHeader -match 'CreateTemporaryLocalLicense|ActivateLocalKey|localLicenseValid') {
    throw 'Uma via de licença local de desenvolvimento continua no código.'
}
$project = [IO.File]::ReadAllText((Join-Path $ProjectDir 'OmiGhost.vcxproj'))
if ($project -match 'src\\tests\\|src\\launcher\\license_page\.cpp|ImGui\\imgui_demo\.cpp') {
    throw 'O executável ainda inclui testes ou a página de licença local obsoleta.'
}
$main = [IO.File]::ReadAllText((Join-Path $ProjectDir 'src\main.cpp'))
if ($main -notmatch 'OMNIGHOST_PUBLISH_BUILD.*OMNIGHOST_SKIP_KEYAUTH') {
    throw 'Publish não bloqueia a desativação do KeyAuth.'
}

$version = [IO.File]::ReadAllText((Join-Path $ProjectDir 'config\release\version.txt')).Trim()
$header = [IO.File]::ReadAllText((Join-Path $ProjectDir 'src\app_version.h'))
if ($version -notmatch '^(0|[1-9]\d*)\.[0-9]\.[0-9]$' -or
    $header -notmatch ('Version\[\]\s*=\s*"' + [regex]::Escape($version) + '"')) {
    throw 'A versão de release e app_version.h estão dessincronizados.'
}

$offsets = Get-Content -LiteralPath (Join-Path $ProjectDir 'data\fivem_offsets.json') -Raw | ConvertFrom-Json
if (-not $offsets.builds -or @($offsets.builds.PSObject.Properties).Count -eq 0) {
    throw 'A tabela de builds FiveM está vazia.'
}
foreach ($build in $offsets.builds.PSObject.Properties) {
    foreach ($field in @('world', 'replay', 'viewport', 'ped_visible_flag')) {
        $value = [string]$build.Value.$field
        if ($value -notmatch '^0x[0-9a-fA-F]+$' -or [Convert]::ToUInt64($value.Substring(2), 16) -eq 0) {
            throw "A build FiveM $($build.Name) não tem $field válido."
        }
    }
}
Write-Host "[PASS] Release readiness: versão $version, licença local ausente e $(@($offsets.builds.PSObject.Properties).Count) builds FiveM verificados."
