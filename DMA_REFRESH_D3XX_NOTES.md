# DMA latency / MemProcFS refresh / FTDI D3XX notes

## What changed in this project

- MemProcFS/VMM updated to `5.18.11.250` from the supplied `v5.18.11` package.
- LeechCore aligned to `2.23.3.103` from the supplied `v2.23.3` package.
- The dependency manifest now reports the versions of the actual PE files instead of stale historical values.
- `FTD3XXWU.dll` is preferred over legacy `FTD3XX.dll`, matching current LeechCore behavior.
- Startup now logs the loaded FTDI application DLL version/path and attempts to log the installed FT60x/D3XX Windows driver version through SetupAPI.
- The VMM refresh policy now reads the live MemProcFS tick/refresh settings and moves only the expensive full/MEDIUM process refresh to 60 seconds by default. Memory, TLB, and FAST refreshes remain enabled.

## Why the 15-second hitch is suspicious

MemProcFS documents a default MEDIUM/full process refresh every 15 seconds and explicitly describes it as causing a noticeable timing spike. The previous OmniGhost log showed roughly 0.83–0.89 second calls at an approximately 14–15 second cadence, while `lock_wait_ms=0`. The new startup log prints the actual `tick_ms`, `medium_ms_before`, and `medium_ms_after` values so this can be verified on the running machine instead of assumed.

## Runtime controls

Normal/default OmniGhost behavior now targets a 60,000 ms MEDIUM refresh interval.

To choose another interval without recompiling, set an environment variable before starting OmniGhost, for example:

```powershell
$env:OMNIGHOST_VMM_MEDIUM_REFRESH_MS = '120000'
.\OmniGhost.exe
```

Accepted values are clamped to 15,000–600,000 ms. Set it to `0` to keep the upstream MemProcFS interval unchanged.

For a short A/B diagnostic only, all automatic MemProcFS refreshes can be disabled before initialization:

```powershell
$env:OMNIGHOST_VMM_NOREFRESH = '1'
.\OmniGhost.exe
```

Do not use `NOREFRESH` as the normal long-session setting. MemProcFS warns that process/allocation information can become stale when refresh is disabled.
