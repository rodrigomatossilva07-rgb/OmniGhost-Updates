# OmniGhost Hardware Compatibility Matrix

## Version: 3.4.2
## Last Updated: 2026-09-06

---

## Tested DMA Devices

| Device | Firmware | Status | Notes |
|--------|----------|--------|-------|
| PCILeech (FPGA) | 5.17+ | ✅ Tested | Primary supported device |
| LeechCore | 2.23+ | ✅ Tested | Required for DMA operations |

---

## Tested Input Devices

| Device | Firmware | Status | Notes |
|--------|----------|--------|-------|
| MAKCU | Latest | ✅ Tested | Optional, for mouse input |
| KMBox Net | Latest | ✅ Tested | Optional, for mouse input |

---

## Tested Games (Offsets Verified)

| Game | Version Tested | Offset Source | Status |
|------|---------------|---------------|--------|
| Counter-Strike 2 | Latest | Embedded + cheatoffsets.com | ✅ Tested |
| Rust | Latest | Embedded + cheatoffsets.com | ✅ Tested |
| Warzone 2.0 / MW3 | Latest | Embedded + cheatoffsets.com | ✅ Tested |
| Valorant | Latest | Embedded (no external offsets) | ✅ Tested |
| Fortnite | Latest | Embedded + cheatoffsets.com | ✅ Tested |
| FiveM (GTA V) | Latest | Embedded + cheatoffsets.com | ✅ Tested |
| Apex Legends | Latest | Embedded + cheatoffsets.com | ✅ Tested |

---

## Tested Operating Systems

| OS Version | Build | Status | Notes |
|------------|-------|--------|-------|
| Windows 11 24H2 | 26100.x | ✅ Tested | Primary target |
| Windows 11 23H2 | 22631.x | ✅ Tested | Supported |
| Windows 10 22H2 | 19045.x | ✅ Tested | Supported |

---

## Tested DPI Scaling

| DPI Setting | Status |
|-------------|--------|
| 100% | ✅ Tested |
| 125% | ✅ Tested |
| 150% | ✅ Tested |
| 175% | ✅ Tested |
| 200% | ✅ Tested |
| 250% | ✅ Tested |

---

## Tested Locales

| Language | Status |
|----------|--------|
| English (US) | ✅ Tested |
| Portuguese (BR) | ✅ Tested |
| German | ✅ Tested |
| French | ✅ Tested |
| Spanish | ✅ Tested |
| Russian | ✅ Tested |

---

## Notes

- Only hardware/software combinations that have been explicitly tested are listed above.
- Untested combinations may work but are not guaranteed.
- This matrix is updated with each release based on actual testing.
- For CI validation, see `.github/workflows/release.yml` hardware integration test job.