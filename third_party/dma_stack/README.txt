# DMA stack (MemProcFS + LeechCore)

**Single source of truth** for OmniGhost hardware DMA dependencies.

```
include/   vmmdll.h, leechcore.h
lib/       vmm.lib, leechcore.lib
bin/       vmm.dll, leechcore.dll, FTD3XX*, dbghelp, plugins runtime…
data/      (no runtime database; OmniGhost uses -disable-infodb)
versions.json
managed-files.json
```

## Rules

- Do **not** keep parallel copies under `DMALibrary/libs` or `Libs` for official stack files.
- Own extras (e.g. `pdbcrust.dll`) go in `runtime/own/`.
- Update only via `UpdateDependencies.bat` or:

```powershell
.\tools\Update-DmaDependencies.ps1 -Latest
.\tools\Update-DmaDependencies.ps1 -Validate
```

Compatibility gate: MemProcFS and standalone LeechCore overlapping binaries must have **identical SHA-256** or the updater aborts.
