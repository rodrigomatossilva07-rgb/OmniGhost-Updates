$content = Get-Content 'C:\Users\Rodrigo\Downloads\OmniGhost\OmiGhost.vcxproj' -Raw

# Fix generate-embedded-offsets
$content = $content -replace '(generate-embedded-offsets\s+"\$\(MSBuildProjectDirectory\)"\s+"\$\(Configuration\)")', '$1 "$(Platform)"'

# Fix generate-embedded-runtime
$content = $content -replace '(generate-embedded-runtime\s+"\$\(MSBuildProjectDirectory\)"\s+"\$\(VCInstallDir\)Redist\\MSVC\\\$\(VCToolsVersion\)\\x64"\s+"\$\(Configuration\)")', '$1 "$(Platform)"'

# Fix generate-embedded-resources
$content = $content -replace '(generate-embedded-resources\s+"\$\(MSBuildProjectDirectory\)"\s+"\$\(Configuration\)")', '$1 "$(Platform)"'

Set-Content 'C:\Users\Rodrigo\Downloads\OmniGhost\OmiGhost.vcxproj' $content -Encoding UTF8