# OmniGhost

OmniGhost is a single-process, portable DMA-based overlay application for game interaction. Built with modern C++20, targeting Windows x64 only.

## Requirements

- Windows 10/11 x64 (22H2+)
- Visual Studio 2022 17.9+ with MSVC v145 toolset
- Windows SDK 10.0.26100+
- C++20 compatible compiler
- DMA device (PCILeech/FPGA) for full functionality
- MAKCU or KMBox Net (optional, for mouse input)

## Build Configurations

| Configuration | Purpose | Output Directory |
|---------------|---------|------------------|
| **Release** | Local optimized build, no remote publishing | `build\` |
| **Tester** | Development build with external offsets/resources | `build\Tester\` |
| **Publish** | Private customer build with auto-publish to GitHub | `build\Publish\` |
| **PrivateStatic** | Private development build with static VMM/LeechCore | `build\PrivateStatic\` |

### Key Differences

- **Release**: Optimized, single-EXE distribution, validates no external runtime files
- **Tester**: Includes external offset JSONs, verbose diagnostics, incremental LTCG
- **Publish**: Same as Release but enables GitHub publication pipeline, requires clean reproducible source
- **PrivateStatic**: Links VMM/LeechCore statically from source (not for distribution)

## Building

### Prerequisites

1. Install Visual Studio 2022 with "Desktop development with C++" workload
2. Ensure Windows SDK 10.0.26100+ is installed
3. Clone the repository with submodules:
   ```cmd
   git clone --recurse-submodules https://github.com/your-repo/OmniGhost.git
   ```

### Command Line (MSBuild)

```cmd
# Release build
msbuild OmiGhost.vcxproj /m /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145

# Tester build
msbuild OmiGhost.vcxproj /m /p:Configuration=Tester /p:Platform=x64 /p:PlatformToolset=v145

# Publish build (requires Git tags, clean repo)
msbuild OmiGhost.vcxproj /m /p:Configuration=Publish /p:Platform=x64 /p:PlatformToolset=v145
```

### Visual Studio

1. Open `OmiGhost.slnx` or `OmiGhost.vcxproj`
2. Select configuration: Release, Tester, Publish, or PrivateStatic
3. Select platform: x64
4. Build → Build Solution

## Project Structure

```
OmniGhost/
├── src/                    # Core application source
│   ├── platform/           # Platform abstraction, bootstrap, crash handling
│   ├── updater/            # Update service, HTTP client, manifest parsing
│   ├── launcher/           # Game selection, authentication, adapters
│   ├── window/             # ImGui-based UI, pages, widgets
│   ├── config/             # Configuration management
│   ├── licensing/          # License validation
│   ├── makcu/              # MAKCU device integration
│   └── ...                 # Other subsystems
├── Cs2/                    # Counter-Strike 2 specific code
├── Rust/                   # Rust specific code
├── Warzone/                # Warzone specific code
├── Valorant/               # Valorant specific code
├── Fortnite/               # Fortnite specific code
├── Fivem/                  # FiveM specific code
├── DMALibrary/             # DMA memory library
├── ImGui/                  # Dear ImGui (embedded)
├── resources/              # Icons, manifests, embedded resources
├── third_party/            # DMA stack (VMM, LeechCore), cloudflared
├── tools/                  # PowerShell build/validation scripts
├── data/                   # Game offset data (dev only)
└── runtime/                # Custom runtime DLLs
```

## Embedded Resources

OmniGhost embeds all runtime dependencies at build time:

- **Offsets**: Game-specific memory offsets (generated from JSON)
- **Runtime**: DMA DLLs (vmm.dll, leechcore.dll), VCRUNTIME140, cloudflared
- **Resources**: Game images, weapon data, radar webapp

Generation happens automatically during build via PowerShell scripts in `tools/`.

## Configuration

### Runtime Config (`%APPDATA%\OmniGhost\config.json`)

```json
{
  "schemaVersion": 1,
  "language": "pt-BR",
  "ui_scale": 1.0,
  "update_channel": "stable",
  "update_auto_download": true,
  "update_install_on_exit": false,
  "update_auto_check": true
}
```

### Game-Specific Configs

Stored in `%APPDATA%\OmniGhost\configs\<game>.json`

## Testing

### Manual Test Plan

See [TEST_PLAN.md](TEST_PLAN.md) for comprehensive manual testing procedures covering:
- Close app during each init stage
- FPGA disconnection during session
- MAKCU absence handling
- Reconnection after failure
- Game → Launcher → Another game transitions
- Consecutive sessions for handle/thread leaks
- Settings round-trip
- DPI scaling 100%-250%
- German/French/Unicode text handling
- Digital rain performance on integrated GPUs

### Automated Validation (CI)

The GitHub Actions workflow (`.github/workflows/release.yml`) runs on tag push:

1. **Build & Test**: Compiles Release, Tester, Publish; runs static analysis
2. **PE Validation**: Validates single-EXE output, import table, dependencies
3. **Package & SBOM**: Creates release ZIP, generates CycloneDX SBOM
4. **Hardware Integration** (self-hosted): Tests with real DMA device and game
5. **Publish**: Creates draft GitHub release, validates assets, publishes

Run validation locally:
```powershell
# Project structure validation
.\tools\Validate-Project.ps1 -ProjectDir .

# Release engineering tests
.\tools\Test-ReleaseEngineering.ps1 -ProjectDir .

# Localization audit
.\tools\Audit-UiLocalization.ps1 -ProjectDir .

# Source complexity audit
.\tools\Audit-SourceComplexity.ps1 -ProjectDir .
```

## Distribution

### Release (Local)

Output: `build\OmniGhost.exe` (single EXE, no external dependencies)

### Publish (Customer Channel)

1. Tag the commit: `git tag v3.4.2`
2. Push tag: `git push origin v3.4.2`
3. GitHub Actions builds, validates, packages, and publishes to `rodrigomatossilva07-rgb/OmniGhost-Updates`

### Requirements for Publish

- Clean Git working tree (no uncommitted changes)
- Version in `version.txt` matches tag (`v3.4.2`)
- All validation checks pass
- `release-publish.json` configured correctly

## Troubleshooting

### Build Fails: Missing DMA Stack

Ensure `third_party\dma_stack\` contains:
- `include/vmmdll.h`, `include/leechcore.h`
- `lib/vmm.lib`, `lib/leechcore.lib`
- `bin/vmm.dll`, `bin/leechcore.dll`
- `versions.json`, `managed-files.json`

### Build Fails: Missing libs\

Place required runtime DLLs in `libs\` (fallback bundle for Tester/Release).

### Runtime: "DLL not found"

Ensure embedded runtime generation ran (check build output for `[EmbeddedRuntime]` messages).

### Runtime: "Offsets missing"

For Tester: Ensure JSON offset files exist in `data\`.
For Release/Publish: Offsets are embedded; regenerate with `Build-EmbeddedOffsets.ps1`.

## Contributing

1. Follow existing code style (C++20, Level 4 warnings, `/permissive-`)
2. Run `Validate-Project.ps1` before committing
3. Update `CHANGELOG.md` with changes
4. Ensure tests pass in CI

## License

Proprietary. Not for redistribution without explicit permission.

## Third-Party Notices

See [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt) for embedded dependency licenses.