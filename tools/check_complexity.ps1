$files = @(
    'src/launcher/game_select.cpp',
    'src/launcher/updates_page.cpp',
    'DMALibrary/Memory/Memory.cpp',
    'src/games/Fivem/esp/esp.cpp',
    'src/games/Cs2/cs2_game.cpp'
)
foreach ($f in $files) {
    $path = Join-Path (Split-Path -Parent $PSScriptRoot) $f
    if (Test-Path $path) {
        $lines = (Get-Content $path | Measure-Object).Count
        Write-Host ($f + ': ' + $lines + ' lines')
    } else {
        Write-Host ($f + ': NOT FOUND')
    }
}
