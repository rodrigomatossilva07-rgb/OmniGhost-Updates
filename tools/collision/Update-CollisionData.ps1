[CmdletBinding()]
param(
    [ValidateSet('Menu', 'CS2', 'FiveM', 'All')]
    [string]$Game = 'Menu',
    [ValidateSet('Official', 'Workshop', 'All')]
    [string]$Cs2Scope = 'Official',
    [string]$Cs2ExtractorRoot = '',
    [string]$RageKitRoot = 'C:\Users\Rodrigo Silva\Downloads\ragekit',
    [switch]$SkipStorageCompression
)

$ErrorActionPreference = 'Stop'
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = [IO.Path]::GetFullPath((Join-Path $scriptRoot '..\..'))
$workRoot = Join-Path $repoRoot '.cache\collision-build'

function Write-Section([string]$Text) {
    Write-Host "`n  $Text" -ForegroundColor Yellow
}

function Resolve-CS2Extractor {
    $candidates = @()
    if ($Cs2ExtractorRoot) { $candidates += $Cs2ExtractorRoot }
    $candidates += (Join-Path $scriptRoot 'extractors\cs2')
    $candidates += 'C:\Users\Rodrigo Silva\Downloads\CS2 EXTRATOR MAPAS'

    foreach ($candidate in $candidates) {
        $project = Join-Path $candidate 'PhysExtractor.csproj'
        if (Test-Path -LiteralPath $project) { return $project }
    }
    throw @"
Extrator CS2 não encontrado. Coloca o respetivo projeto em:
  $scriptRoot\extractors\cs2\PhysExtractor.csproj
ou indica -Cs2ExtractorRoot <pasta>.
"@
}

function Test-TriFile([IO.FileInfo]$File) {
    if ($File.Length -lt 36 -or ($File.Length % 36) -ne 0) {
        throw "TRI inválido (esperados triângulos de 9 float32): $($File.FullName)"
    }
}

function Update-CS2Collision {
    Write-Section 'CS2 — extração e validação de mapas'
    $project = Resolve-CS2Extractor
    $appRoot = Join-Path $workRoot 'cs2\app'
    $triRoot = Join-Path $appRoot 'tri'
    $destination = Join-Path $repoRoot 'src\games\Cs2\collision'
    if (Test-Path -LiteralPath $triRoot) {
        $resolvedTri = [IO.Path]::GetFullPath($triRoot)
        $resolvedWork = [IO.Path]::GetFullPath($workRoot).TrimEnd('\') + '\'
        if (-not $resolvedTri.StartsWith($resolvedWork, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Recusa de limpeza fora da cache: $resolvedTri"
        }
        Remove-Item -LiteralPath $resolvedTri -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $appRoot, $destination | Out-Null

    & dotnet build $project -c Release -o $appRoot --nologo
    if ($LASTEXITCODE -ne 0) { throw 'A compilação do extrator CS2 falhou.' }

    $assembly = Join-Path $appRoot 'PhysExtractor.dll'
    if (-not (Test-Path -LiteralPath $assembly)) { throw "Executável do extrator em falta: $assembly" }

    $scopeChoice = switch ($Cs2Scope) { 'All' { '1' } 'Official' { '2' } 'Workshop' { '3' } }
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = 'dotnet'

    $start.Arguments = '"' + $assembly.Replace('"', '\"') + '"'
    $start.WorkingDirectory = $appRoot
    $start.UseShellExecute = $false
    $start.RedirectStandardInput = $true
    $process = [Diagnostics.Process]::Start($start)
    $process.StandardInput.WriteLine($scopeChoice)
    $process.StandardInput.WriteLine('2')
    $process.StandardInput.WriteLine('')
    $process.StandardInput.Close()
    $process.WaitForExit()

    $files = @(Get-ChildItem -LiteralPath $triRoot -Filter '*.tri' -File)
    if ($files.Count -eq 0) {
        throw "O extrator CS2 terminou com o código $($process.ExitCode) e não produziu ficheiros em $triRoot"
    }
    if ($process.ExitCode -ne 0) {


        Write-Warning "O extrator devolveu $($process.ExitCode) após produzir os mapas; os ficheiros serão validados antes de serem aceites."
    }
    foreach ($file in $files) {
        Test-TriFile $file
        Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $destination $file.Name) -Force
    }
    Write-Host ("  OK  {0} mapas TRI enviados para {1}" -f $files.Count, $destination) -ForegroundColor Green
    Write-Host '      Estes recursos são comprimidos pelo pipeline de recursos durante a build.' -ForegroundColor DarkGray
}

function Update-FiveMCollision {
    Write-Section 'FiveM — construção, validação e compressão regional'
    $toolRoot = Join-Path $repoRoot 'tools\FiveMCollisionBuilder'
    & (Join-Path $toolRoot 'Build.ps1') -RageKitRoot $RageKitRoot -Configuration Release
    if ($LASTEXITCODE -ne 0) { throw 'A compilação do FiveM Collision Builder falhou.' }

    $builder = Join-Path $toolRoot 'build\Release\FiveMCollisionBuilder.exe'
    $destination = Join-Path $repoRoot 'data\fivem\collision'
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    & $builder --output $destination --region-size 512
    if ($LASTEXITCODE -ne 0) { throw 'A geração do pacote FiveM falhou.' }

    $manifest = Join-Path $destination 'manifest.json'
    if (-not (Test-Path -LiteralPath $manifest)) { throw "Manifesto FiveM em falta: $manifest" }
    $metadata = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
    $regions = @(Get-ChildItem -LiteralPath $destination -Filter '*.ogcol' -File)
    if ($regions.Count -eq 0) { throw 'O pacote FiveM não contém regiões OGCOL.' }
    if (-not $SkipStorageCompression) {
        & (Join-Path $toolRoot 'Optimize-CollisionData.ps1') -CollisionRoot $destination
    }
    Write-Host ("  OK  {0} regiões OGCOL validadas em {1}" -f $regions.Count, $destination) -ForegroundColor Green
    Write-Host '      O pacote permanece externo ao EXE e não entra na release normal.' -ForegroundColor DarkGray
}

if ($Game -eq 'Menu') {
    Write-Host "`n  OMNIGHOST — ATUALIZAR COLISÃO" -ForegroundColor Yellow
    Write-Host '  1  CS2 — mapas oficiais'
    Write-Host '  2  CS2 — oficiais e Workshop'
    Write-Host '  3  FiveM — GTA V completo'
    Write-Host '  4  Ambos — CS2 oficiais + FiveM'
    $choice = Read-Host "`n  Escolha"
    switch ($choice) {
        '1' { $Game = 'CS2'; $Cs2Scope = 'Official' }
        '2' { $Game = 'CS2'; $Cs2Scope = 'All' }
        '3' { $Game = 'FiveM' }
        '4' { $Game = 'All'; $Cs2Scope = 'Official' }
        default { throw 'Opção inválida.' }
    }
}

New-Item -ItemType Directory -Force -Path $workRoot | Out-Null
if ($Game -in @('CS2', 'All')) { Update-CS2Collision }
if ($Game -in @('FiveM', 'All')) { Update-FiveMCollision }
Write-Host "`n  Atualização concluída." -ForegroundColor Green
