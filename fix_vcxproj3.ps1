$lines = Get-Content 'C:\Users\Rodrigo\Downloads\OmniGhost\OmiGhost.vcxproj'

for ($i = 0; $i -lt $lines.Count; $i++) {
    $line = $lines[$i]
    
    # Fix generate-embedded-offsets
    if ($line -match 'generate-embedded-offsets\s+"\$\(MSBuildProjectDirectory\)"\s+"\$\(Configuration\)"') {
        $lines[$i] = $line -replace 'generate-embedded-offsets\s+"\$\(MSBuildProjectDirectory\)"\s+"\$\(Configuration\)"', 'generate-embedded-offsets "$(MSBuildProjectDirectory)" "$(Configuration)" "$(Platform)"'
        Write-Host "Fixed offsets at line $($i+1)"
    }
    
    # Fix generate-embedded-runtime
    if ($line -match 'generate-embedded-runtime\s+"\$\(MSBuildProjectDirectory\)"\s+"\$\(VCInstallDir\)Redist\\MSVC\\\$\(VCToolsVersion\)\\x64"\s+"\$\(Configuration\)"') {
        $lines[$i] = $line -replace 'generate-embedded-runtime\s+"\$\(MSBuildProjectDirectory\)"\s+"\$\(VCInstallDir\)Redist\\MSVC\\\$\(VCToolsVersion\)\\x64"\s+"\$\(Configuration\)"', 'generate-embedded-runtime "$(MSBuildProjectDirectory)" "$(VCInstallDir)Redist\MSVC\$(VCToolsVersion)\x64" "$(Configuration)" "$(Platform)"'
        Write-Host "Fixed runtime at line $($i+1)"
    }
    
    # Fix generate-embedded-resources
    if ($line -match 'generate-embedded-resources\s+"\$\(MSBuildProjectDirectory\)"\s+"\$\(Configuration\)"') {
        $lines[$i] = $line -replace 'generate-embedded-resources\s+"\$\(MSBuildProjectDirectory\)"\s+"\$\(Configuration\)"', 'generate-embedded-resources "$(MSBuildProjectDirectory)" "$(Configuration)" "$(Platform)"'
        Write-Host "Fixed resources at line $($i+1)"
    }
}

Set-Content 'C:\Users\Rodrigo\Downloads\OmniGhost\OmiGhost.vcxproj' $lines -Encoding UTF8