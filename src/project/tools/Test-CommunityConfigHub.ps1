param([string]$ProjectDir = ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))))
$ErrorActionPreference = 'Stop'
$files = @{
    manager = Join-Path $ProjectDir 'src/config/config_manager.h'
    core = Join-Path $ProjectDir 'src/config/config_core.cpp'
    configs = Join-Path $ProjectDir 'src/window/pages/common/SaveConfigsPage.cpp'
    marketplace = Join-Path $ProjectDir 'src/launcher/game_select_pages.inl'
}
foreach ($path in $files.Values) { if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing $path" } }
$manager = Get-Content -Raw -LiteralPath $files.manager
$core = Get-Content -Raw -LiteralPath $files.core
$configs = Get-Content -Raw -LiteralPath $files.configs
$marketplace = Get-Content -Raw -LiteralPath $files.marketplace
$checks = @(
    @{ Value = $manager; Pattern = 'CommunityConfigInfo'; Name = 'community model' },
    @{ Value = $manager; Pattern = 'Favorites = 3'; Name = 'favorites scope' },
    @{ Value = $manager; Pattern = 'Downloads = 4'; Name = 'download history scope' },
    @{ Value = $core; Pattern = 'Sha256Text\(payload'; Name = 'payload integrity' },
    @{ Value = $core; Pattern = 'CommunityBackupPath'; Name = 'apply backup' },
    @{ Value = $core; Pattern = 'RevertCommunityConfig'; Name = 'revert flow' },
    @{ Value = $core; Pattern = 'schemaVersion > kConfigSchemaVersion'; Name = 'schema compatibility' },
    @{ Value = $configs; Pattern = 'Publicar nova versão'; Name = 'version publishing' },
    @{ Value = $configs; Pattern = 'Pré-visualizar'; Name = 'diff preview' },
    @{ Value = $configs; Pattern = 'Denunciar'; Name = 'report UI' },
    @{ Value = $configs; Pattern = 'Histórico'; Name = 'download history UI' },
    @{ Value = $marketplace; Pattern = 'CONFIGURAÇÕES VERIFICADAS'; Name = 'marketplace verified area' },
    @{ Value = $marketplace; Pattern = 'DownloadCommunityToLibrary'; Name = 'independent library copy' }
)
foreach ($check in $checks) { if ($check.Value -notmatch $check.Pattern) { throw "Missing $($check.Name)" } }
Write-Host '[PASS] Community Config Hub integration invariants.'
