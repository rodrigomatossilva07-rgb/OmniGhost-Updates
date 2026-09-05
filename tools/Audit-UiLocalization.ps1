[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ProjectDir,
    [int]$MaximumDirectLiterals = 281,
    [string]$ReportPath = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$ProjectDir = [IO.Path]::GetFullPath((Join-Path $ProjectDir '.'))
$roots = @('src\window', 'src\launcher')
$pattern = '(?:ImGui::(?:TextUnformatted|TextWrapped|Button|Checkbox|RadioButton)|CyberWidgets::(?:ToggleSwitch|GoldButton|Button|BeginCard|SliderFloat|SliderInt|InputField))\s*\(\s*"(?<text>[^"]+)"'
$entries = New-Object System.Collections.Generic.HashSet[string]([StringComparer]::Ordinal)

foreach ($relativeRoot in $roots) {
    $root = Join-Path $ProjectDir $relativeRoot
    foreach ($file in Get-ChildItem -LiteralPath $root -Recurse -File -Filter '*.cpp') {
        if ($file.Name -ceq 'localization.cpp') { continue }
        $relative = $file.FullName.Substring($ProjectDir.Length).TrimStart('\').Replace('\','/')
        $content = [IO.File]::ReadAllText($file.FullName)
        foreach ($match in [regex]::Matches($content, $pattern)) {
            $literal = $match.Groups['text'].Value
            if ($literal.StartsWith('##', [StringComparison]::Ordinal)) { continue }
            [void]$entries.Add("$relative|$literal")
        }
    }
}

$knownEnglish = @('Reduce Motion','Team Check','Smooth','Self ESP','Joints','Reinit DMA','Hackable crate')
$violations = @($entries | Where-Object {
    $literal = ($_ -split '\|', 2)[1]
    $literal -cin $knownEnglish
})
if ($violations.Count -gt 0) {
    throw "Strings inglesas conhecidas fora da localização: $($violations -join '; ')"
}
if ($entries.Count -gt $MaximumDirectLiterals) {
    throw "A dívida de localização aumentou: $($entries.Count) literais diretos; máximo permitido $MaximumDirectLiterals. Usa Loc::Tr/Loc::TrID."
}

if (-not [string]::IsNullOrWhiteSpace($ReportPath)) {
    $ReportPath = [IO.Path]::GetFullPath((Join-Path $ProjectDir $ReportPath))
    New-Item -ItemType Directory -Path (Split-Path -Parent $ReportPath) -Force | Out-Null
    [ordered]@{
        schemaVersion = 1
        directLiteralCount = $entries.Count
        maximumAllowed = $MaximumDirectLiterals
        entries = @($entries | Sort-Object)
    } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $ReportPath -Encoding UTF8
}

Write-Host "[OmniGhost Localization] $($entries.Count) literais UI legados (máximo $MaximumDirectLiterals); nenhuma regressão conhecida."
