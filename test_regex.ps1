$project = Get-Content -LiteralPath "C:\Users\Rodrigo\Downloads\OmniGhost\OmiGhost.vcxproj" -Raw
$pattern = '<OmniGhostCs2Data[^>]+Exclude="\$(ProjectDir)Cs2\\data\\offsets\.json"'
Write-Host "Pattern:"
Write-Host $pattern
Write-Host ""
Write-Host "Match result:"
$project -match $pattern