# OmniGhost Manual Test Plan

## Test Environment Requirements
- FPGA device (PCILeech) connected
- MAKCU device (optional, for testing absence)
- Multiple games installed (CS2, Rust, Warzone, Valorant, Fortnite, FiveM)
- Monitor(s) with different DPI settings (100%, 125%, 150%, 175%, 200%, 250%)
- German/French system locale for Unicode testing
- Integrated GPU system (for digital rain performance test)

---

## 57. Close App During Each Init Stage

### Test Steps:
1. **Stage: Boot** - Close immediately after double-click (before mutex check)
2. **Stage: CheckingInstance** - Close during single-instance mutex acquisition
3. **Stage: InitializingRuntime** - Close during `RuntimeBootstrap::Prepare()`
4. **Stage: Authenticating** - Close on license/auth screen
5. **Stage: CheckingHardware** - Close during MAKCU/DMA probe
6. **Stage: Ready** - Close on launcher main screen
7. **Stage: StartingGame** - Close during game attach/offset load
8. **Stage: Running** - Close during active game session

### Expected:
- No crashes or hangs
- Clean shutdown logged: `[SHUTDOWN] scope=process state=COMPLETE`
- No resource leaks (check handles/threads in Task Manager)
- Mutex released properly (can restart immediately)

---

## 58. FPGA Disconnection During Session

### Test Steps:
1. Start game session (any game with DMA)
2. Verify session running normally
3. Physically disconnect FPGA USB cable
4. Wait 10-30 seconds
5. Reconnect FPGA
6. Observe behavior

### Expected:
- Session detects disconnection gracefully
- No application crash
- Error logged: `[DMA] device=DISCONNECTED` or similar
- On reconnect: either auto-recovery or clean return to launcher
- No memory corruption or handle leaks

---

## 59. MAKCU Absence (Optional)

### Test Steps:
1. Ensure MAKCU is NOT connected
2. Start application
3. Go through auth → launcher
4. Select game requiring aim (CS2, Rust, etc.)
5. Verify aim features show "MAKCU not connected" status
6. Test that ESP/visuals still work

### Expected:
- No crash on startup
- MAKCU status shows "NOT_FOUND" or "DISCONNECTED"
- Launcher proceeds to DMA probe normally
- Aim features disabled gracefully with clear UI indication
- `aim_type::IsConnected()` returns false

---

## 60. Reconnection After Failure Without Launcher Restart

### Test Steps:
1. Start game session
2. Force failure (disconnect FPGA, kill game process, or invalid offsets)
3. Return to launcher (automatically or manually)
4. Fix the issue (reconnect FPGA, restart game, refresh offsets)
5. Select same game again
6. Verify new session starts

### Expected:
- No need to restart OmniGhost
- Clean session teardown logged
- New session starts fresh
- No stale handles/threads from previous session
- Resource counter shows no leaks

---

## 61. Game → Launcher → Another Game (Same Execution)

### Test Steps:
1. Start Game A (e.g., CS2)
2. Play for a moment
3. Press menu key → "Return to Launcher"
4. Select Game B (e.g., Rust)
5. Play for a moment
6. Return to launcher
7. Select Game C (e.g., Valorant)
6. Repeat several times

### Expected:
- Clean transitions between games
- No memory/handle accumulation
- Each game's resources properly freed
- Launcher state preserved (last game, favorites, history)
- No crashes during transitions

---

## 62. Consecutive Sessions for Stuck Handles/Threads

### Test Steps:
1. Enable resource counter logging (Tester/PrivateStatic build)
2. Run 10+ consecutive sessions:
   - Game → Launcher → Game → Launcher...
   - Mix different games
   - Include failed attaches
3. Monitor Task Manager handles/threads
4. Check session log for resource deltas

### Expected:
- Handles return to baseline after each session
- Threads return to baseline
- Working set memory stable
- Log shows: `"leaked_handles": "NO"`, `"leaked_threads": "NO"`
- No steady increase over iterations

---

## 71. Settings Round-Trip

### Test Steps:
1. Open Settings → change every field:
   - Language (all 6)
   - All enums (StartBehavior, MinimizeBehavior, etc.)
   - All floats (ui_scale, black_level, sliders)
   - All ints (binds, monitor_index)
   - All colors (primary, secondary, fps, particles)
   - All bools (toggles)
2. Close app
3. Reopen app
4. Verify every setting persisted correctly
5. Test Export → Import cycle
6. Test Reset Page / Reset Global

### Expected:
- All values match exactly
- No schema migration errors
- Clamps logged if values corrected
- Export/Import preserves all settings
- Reset restores defaults properly

---

## 79. DPI Scaling 100%-250%

### Test Steps:
For each DPI setting (Windows Display Settings):
1. 100% (baseline)
2. 125%
3. 150%
4. 175%
5. 200%
6. 250%

At each DPI:
1. Set DPI, log out/in
2. Launch OmniGhost
3. Check:
   - Launcher not cut off
   - All text readable
   - Buttons/inputs properly sized
   - Sidebar/header/footer proportional
   - Game menu fits screen
   - No overlapping elements
   - Scrollable regions work

### Expected:
- UI scales correctly at all DPIs
- No horizontal scrollbars needed
- Text sharp (no blurry scaling)
- Breakpoints trigger correctly
- `ui_scale` combines with DPI properly

---

## 85. Long German/French Texts and Unicode

### Test Steps:
1. Set Windows language to German
2. Launch app, check all UI text
3. Set to French, repeat
4. Test with custom config names containing:
   - German: `Überwachungseinstellungen für Überwachungskameras`
   - French: `Configuration des paramètres de surveillance avancée`
   - Unicode: `测试配置 🎮 🔧 設定`
   - Emoji: `My Config 🎯💰⚡`
5. Save/load configs with these names
5. Check clipboard copy/paste

### Expected:
- All UI text translated (no English fallbacks visible)
- Long texts wrap properly (no cutoff)
- Unicode filenames work in config system
- Clipboard handles Unicode correctly
- No encoding errors in logs

---

## 92. Digital Rain Cost on Integrated GPUs

### Test Steps:
1. Run on system with integrated GPU (Intel UHD, AMD Vega, etc.)
2. Enable digital rain: `Settings → Appearance → Digital Rain = Full`
3. Set animation intensity = Full
4. Monitor for 5 minutes:
   - GPU usage (Task Manager / GPU-Z)
   - Frame time (FPS counter)
   - CPU usage
   - Temperature
5. Compare with `Digital Rain = Off`

### Expected:
- GPU usage < 15% on integrated graphics
- FPS stable at monitor refresh rate
- No thermal throttling
- Frame time < 16ms (60fps) or < 8ms (120fps)
- Option to disable works and reduces load significantly

---

## Reporting Template

For each test, record:
```
Test: [ID - Name]
Date: [YYYY-MM-DD]
Tester: [Name]
Hardware: [FPGA model, MAKCU y/n, GPU, CPU, RAM]
OS: [Windows version, DPI, Language]
Result: [PASS/FAIL]
Notes: [Any issues, logs, screenshots]
```

---

## Automation Notes

The following are automated and verified in CI/build:
- Build compiles (Tester, Publish, Release)
- Static analysis (Level 4 warnings, C++ Core Guidelines)
- Unit tests (if any)
- Schema migration tests
- Atomic write tests
- Localization audit runs on startup

Run `msbuild /p:Configuration=Tester` to verify before manual testing.