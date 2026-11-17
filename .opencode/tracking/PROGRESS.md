# Progress Tracking - OmniGhost Improvements

## Phase 1: Critical - Unification & Cleanup
- [x] 1.1 Remove Platform::Result → unify to tl::expected
- [x] 1.2 Eliminate throws in Unwrap() → use and_then/or_else (removed GetValue, added TryGetValue)
- [x] 1.3 Migrate std::cerr/cout → OGS_LOG_* (main.cpp done, hardware_manager.cpp done, ~13 files remaining)
- [x] 1.4 Remove dead code OMNIGHOST_DEV_EXTERNAL_OFFSETS
- [x] 1.5 Cache paths in OffsetAuto (avoid filesystem in hot path)

## Phase 2: Architecture - DI & Module System
- [x] 2.1 ServiceContainer DI complete + dependency injection (basic setup done)
- [x] 2.2 IGameModule interface + per-game implementations (CS2 example created)
- [x] 2.3 Refactor global singletons → DI (SecurityManager, HardwareManager interfaces created)
- [ ] 2.4 Config schema validation (nlohmann::json_schema_validator)
- [ ] 2.5 Async DMA thread pool + futures

## Phase 3: Quality & Tests
- [ ] 3.1 Split large files (widgets.cpp, main.cpp, updates_page.cpp)
- [ ] 3.2 Unit tests Catch2 (OffsetAuto, ConfigManager, Security, etc)
- [ ] 3.3 Fuzzing targets (libFuzzer for JSON/offset parsers)
- [ ] 3.4 Magic numbers → constants
- [ ] 3.5 C-style casts → static_cast
- [ ] 3.5 Naming consistency
- [ ] 3.6 Doxygen documentation

## Phase 4: Polish & CI
- [ ] 4.1 Clang-format enforce in CI
- [ ] 4.2 Pre-commit hooks active
- [ ] 4.3 SBOM generation in release
- [ ] 4.4 Benchmarks + profiling

---
Last Updated: 2026-11-17
Current Phase: 2
Current Task: 2.3 Refactor remaining singletons (ConfigManager, OffsetAuto, etc.)