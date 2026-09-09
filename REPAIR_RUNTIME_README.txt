OmniGhost runtime repair — 2026-09-09

Root cause found:
- The Release/Tester/Publish executable uses delay-loaded vmm.dll and leechcore.dll.
- The runtime validator accepts only files present in the embedded private-runtime manifest.
- The source package contained only the empty fallback manifest header and rc stub; the generated
  .cache\generated\<Configuration>\x64 runtime files were not part of the source package.
- A broken build could therefore produce an EXE whose embedded manifest did not contain
  vmm.dll/leechcore.dll. At runtime this becomes:
    "O ficheiro não pertence ao manifesto do runtime privado."

Changes in this package:
1. Build-EmbeddedRuntime.ps1 now fails closed for non-PrivateStatic builds if vmm.dll/leechcore.dll
   are not selected for embedding.
2. Validate-EmbeddedRuntime.ps1 verifies that generated manifest + RC contain both DLLs.
3. OmiGhost.vcxproj invokes that validator automatically before compilation/resources.
4. Run-OmniGhostPowerShell.cmd exposes the new validation action.
5. Repair-EmbeddedRuntime.ps1 regenerates and validates the selected configuration.

Clean repair on Windows:
1. Open Developer PowerShell for Visual Studio with x64 tools.
2. cd to the OmniGhost project root.
3. Run:
   powershell -NoLogo -NoProfile -ExecutionPolicy Bypass -File .\tools\Repair-EmbeddedRuntime.ps1 -Configuration Release -Platform x64
4. Delete the previous build output/cache for the affected configuration if it is stale.
5. Rebuild:
   msbuild .\OmiGhost.vcxproj /m /p:Configuration=Release /p:Platform=x64
6. The build must print:
   [EmbeddedRuntimeValidate] PASS configuration=Release platform=x64 required DMA DLLs embedded.
7. Re-test the resulting EXE.

Expected result:
- The runtime manifest contains libs/vmm.dll and libs/leechcore.dll.
- The old "não pertence ao manifesto do runtime privado" error is eliminated.
- FTDI remains a separate hardware/driver check; it is intentionally soft-fail at bootstrap.

Not claimed/tested here:
- Actual FPGA/DMA hardware opening on Windows.
- FTDI driver state, firmware, PCIe link, Secure Boot/VBS, or a live FiveM session.
- A successful MSVC build, because this environment does not provide Windows/MSVC.

Safety note:
The repair targets generic Windows runtime packaging/integrity. It does not alter game-process memory access logic.
