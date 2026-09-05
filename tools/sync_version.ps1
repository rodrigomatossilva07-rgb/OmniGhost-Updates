[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectDir
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProjectDir = [System.IO.Path]::GetFullPath($ProjectDir)
$VersionFile = Join-Path $ProjectDir 'version.txt'
$HeaderFile = Join-Path $ProjectDir 'src\app_version.h'
$ResourceFile = Join-Path $ProjectDir 'resources\app.rc'
$Utf8NoBom = New-Object System.Text.UTF8Encoding -ArgumentList $false

if (-not (Test-Path -LiteralPath $VersionFile -PathType Leaf)) {
    throw "Ficheiro de versão não encontrado: $VersionFile"
}

$Version = (Get-Content -LiteralPath $VersionFile -Raw).Trim()
$Match = [regex]::Match($Version, '^(?<major>0|[1-9]\d*)\.(?<minor>[0-9])\.(?<patch>[0-9])$')
if (-not $Match.Success) {
    throw "A versão deve usar MAJOR.MINOR.PATCH com MINOR/PATCH entre 0 e 9 (ex.: 2.9.9 -> 3.0.0). Valor atual: '$Version'"
}

function Write-IfChanged {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Content
    )

    $Current = if (Test-Path -LiteralPath $Path) { [System.IO.File]::ReadAllText($Path) } else { $null }
    if ($Current -cne $Content) {
        [System.IO.File]::WriteAllText($Path, $Content, $Utf8NoBom)
        Write-Host "[OmniGhost] Versão sincronizada em $Path"
    }
}

$Header = [System.IO.File]::ReadAllText($HeaderFile)
$UpdatedHeader = [regex]::Replace(
    $Header,
    'inline\s+constexpr\s+char\s+Version\[\]\s*=\s*"[^"]+"\s*;',
    "inline constexpr char Version[] = `"$Version`";"
)
$ExpectedVersionLine = "inline constexpr char Version[] = `"$Version`";"
if ($UpdatedHeader -ceq $Header -and -not $Header.Contains($ExpectedVersionLine)) {
    throw "Não foi possível localizar OmniGhost::Version em $HeaderFile"
}
Write-IfChanged -Path $HeaderFile -Content $UpdatedHeader

$Major = $Match.Groups['major'].Value
$Minor = $Match.Groups['minor'].Value
$Patch = $Match.Groups['patch'].Value
$Resource = [System.IO.File]::ReadAllText($ResourceFile)
$UpdatedResource = $Resource
$UpdatedResource = [regex]::Replace($UpdatedResource, '(?m)^#define\s+OMNIGHOST_VERSION_MAJOR\s+\d+\s*$', "#define OMNIGHOST_VERSION_MAJOR $Major")
$UpdatedResource = [regex]::Replace($UpdatedResource, '(?m)^#define\s+OMNIGHOST_VERSION_MINOR\s+\d+\s*$', "#define OMNIGHOST_VERSION_MINOR $Minor")
$UpdatedResource = [regex]::Replace($UpdatedResource, '(?m)^#define\s+OMNIGHOST_VERSION_PATCH\s+\d+\s*$', "#define OMNIGHOST_VERSION_PATCH $Patch")
$UpdatedResource = [regex]::Replace($UpdatedResource, '(?m)^#define\s+OMNIGHOST_VERSION_REVISION\s+\d+\s*$', '#define OMNIGHOST_VERSION_REVISION 0')

if ($UpdatedResource -ceq $Resource -and $Resource -notmatch "OMNIGHOST_VERSION_MAJOR\s+$Major") {
    throw "Não foi possível localizar as definições de versão em $ResourceFile"
}
Write-IfChanged -Path $ResourceFile -Content $UpdatedResource

Write-Host "[OmniGhost] Versão preparada: $Version"
