@echo off
del /f /q "C:\Users\Rodrigo Silva\Downloads\OmniGhost\build\*.*" 2>nul
rmdir /s /q "C:\Users\Rodrigo Silva\Downloads\OmniGhost\build" 2>nul
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" "C:\Users\Rodrigo Silva\Downloads\OmniGhost\OmiGhost.vcxproj" /p:Configuration=Publish /p:Platform=x64 /v:q