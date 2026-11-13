# FiveM Offset Dumper

Standalone, read-only Windows x64 pattern scanner for maintaining FiveM offsets.

The scanner is intentionally limited to the globals already used and validated by OmniGhost.
Unsupported experimental pools, native internals, HUD, weather and timing candidates are not
scanned. `player_info` is handled separately because it is a field offset inside a ped, not a
module global: the dumper validates `World -> LocalPlayer -> +player_info -> PlayerInfo`.

Patterns are not treated as proof by themselves. Build-specific patterns are preferred; generic
patterns remain candidates. A result is exported only when it is unique, agrees with a known
profile when one exists, and passes its runtime semantic validator. Player Info is checked using
the live local ped, name, Net ID and wanted level.

Legacy and Enhanced have separate pattern inventories. Enhanced export is deliberately disabled
until Enhanced-specific profiles have been captured and validated.

## Build

Requirements:

- Visual Studio 2022 with **Desktop development with C++**.
- Windows 10/11 SDK.

From this directory:

```bat
build.bat
```

Optional commands:

```bat
build.bat Debug
build.bat Release
build.bat clean
```

Release output:

```text
build\Release\fivem_offset_dumper.exe
```

Inspect the embedded pattern inventory without opening FiveM:

```bat
build\Release\fivem_offset_dumper.exe --inventory
```

## Run

Start FiveM, join a populated server, and then run the dumper. The main menu is insufficient for
validating the local player, Replay lists and entity pools. Exactly one versioned text file is
written to the current directory:

```text
output\fivem_offsets_b3258.txt
```

Module globals are written as RVAs relative to the detected FiveM executable. `player_info` is
written as the field offset relative to a ped. Runtime-unavailable or ambiguous values are
reported as rejected and are never exported as usable offsets.

The tool reads process memory using Windows `ReadProcessMemory`; it is separate from the OmniGhost DMA runtime and is intended for controlled offset-maintenance work.

## Compatibility and evidence

- Core candidates have been observed across several builds, but must still be tested on each
  installed build.
- Explicit validation profiles currently cover b2060, b2189, b2372, b2545, b2612, b2699,
  b2802, b2944, b3095 and b3258. A profile is kept separate from scan results and is never
  used as an export fallback.
- Candidates labelled `UC-v2` originate in the community signature list referenced below and
  in the MIT-licensed `syslink47/fivem-offset-dumper-` implementation.
- The four legacy core candidates were cross-checked against
  `vgszf/Fivem-External-SilentAim`; its camera candidate is explicitly labelled b2372-tested.
- A successful AOB match confirms the instruction sequence and target shape, not the semantic
  identity of the target. Consumers must retain their own structure and value validation.

Sources:

- https://www.unknowncheats.me/forum/alternative-online-mods/759172-fivem-signature-pattern-list-v2-updated.html
- https://github.com/syslink47/fivem-offset-dumper-
- https://github.com/vgszf/Fivem-External-SilentAim

The imported signature table from `syslink47/fivem-offset-dumper-` is used under its MIT
license. Original minimal dumper attribution: `@const` (2025).
