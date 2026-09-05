$files = @(
    'src/launcher/game_select.cpp',
    'src/launcher/updates_page.cpp',
    'DMALibrary/Memory/Memory.cpp',
    'Fivem/esp/esp.cpp',
    'Rust/rust_game.cpp',
    'Cs2/cs2_game.cpp'
)
foreach ($f in $files) {
    $path = 'C:\Users\Rodrigo\Downloads\OmniGhost\' + $f
    if (Test-Path $path) {
        $lines = (Get-Content $path | Measure-Object).Count
        Write-Host ($f + ': ' + $lines + ' lines')
    } else {
        Write-Host ($f + ': NOT FOUND')
    }
}