# CS2 ESP Performance Stabilization

## Objective

This pass targets **frametime stability first**, while preserving a responsive ESP when several visual and aim-related options are enabled together. The primary design rule is that slow or bursty work (filesystem, WIC decode, HTTP, large DMA chains, heap churn) must not execute unpredictably on the render thread.

## Main architectural changes

### 1. Independent scatter handles per acquisition lane

The full player acquisition pipeline and the fast camera/motion lane no longer share the same scatter handle.

- `g_scatter_full`: controllers, pawn resolution, core fields, positions, bones, weapons and local-player batches.
- `g_scatter_motion`: high-rate origin refresh only.

This prevents two threads from constructing/executing batches against one mutable scatter state.

### 2. Runtime snapshot exchange expanded to four slots

`SnapshotExchange<Runtime, 4>` gives the producer an extra spare slot while the renderer and motion lane can both hold read leases. `snapshot_drops` remains visible in telemetry so the slot count can be validated empirically.

### 3. Fast camera lane isolated from mutable Runtime state

The camera/motion thread now consumes the published runtime snapshot for `client_base`, `in_match` and the current player list instead of reading those mutable `runtime` fields directly while the producer is writing them.

### 4. Entity-list revalidation made genuinely infrequent

A healthy entity-list entry uses a cheap primary-page pointer check between full validations. The expensive multi-page / dual-stride probe is now scheduled approximately every **1500 ms**, or immediately after a validation failure / pointer change.

### 5. Render no longer deep-copies Runtime

The ESP renderer uses the immutable runtime snapshot by reference. It no longer creates a per-frame `CS2::Runtime` copy containing `std::vector<Player>` and `std::vector<Player>` spectator storage.

### 6. One presentation transform per player per frame

`SmoothPlayerForPresentation()` is evaluated once for each player into a fixed 64-player presentation cache. Radar, offscreen arrows and the main ESP all consume that cache instead of copying/translating the same player multiple times in one frame.

### 7. MotionSnapshot reduced to the data it actually produces

The fast motion lane now publishes only:

- pawn identifier
- world origin

The previous bone array in every `MotionSample` was unused by the producer and inflated every motion snapshot substantially.

### 8. Compact aim/effect bones vs complete visual skeleton

Two pose levels are now represented explicitly:

- `bones_ok`: compact upper-body anchors are valid (head, neck, chest, stomach).
- `full_bones_ok`: the complete 20-slot pose is valid.

When a complete skeleton/body trigger is not needed, DMA reads only the first **8 Source 2 joints (256 bytes)** instead of the complete **28-joint window (896 bytes)**. Full visual skeleton and body trigger still request the complete pose.

### 9. Bone acquisition LOD and hard budget

Default near-pose refresh starts at 16 ms. With skeleton LOD enabled, `skeleton_lod_distance` now drives the distance tiers. `performance_mode` imposes a minimum 32 ms pose interval and reduces the fresh-bone budget from 14 to 8 players per scan.

For pure visual skeleton acquisition (no aim/trigger dependency), clearly offscreen entities can skip fresh bone DMA and continue from cached/transformed poses.

### 10. Full weapon chain is cached

The cache now covers:

`weapon services -> active weapon handle -> resolved weapon entity -> definition index`

instead of caching only the final definition. Local weapon state uses a short 50 ms cadence; other player weapons use the configured 100 ms cadence.

### 11. Armor cadence is now real

Armor no longer rides the 16 ms core-field scan when requested. It has a dedicated cache/read stage using the configured 50 ms interval.

### 12. Local-player reads batched

Local team/scene/velocity/view/scoped/item services/crosshair/shots are grouped into a scatter batch. Dependent origin/defuser reads are handled in a second small batch.

### 13. Spectator and bomb acquisition throttled

- Spectator refresh: **250 ms**.
- Bomb memory sample: **50 ms**.
- Bomb countdown presentation is interpolated locally between memory samples.

This removes needless high-frequency sequential observer/C4 reads while preserving smooth UI.

### 14. Redundant full-scan camera refresh removed

The old end-of-scan `pre_refresh_positions` vector and view-matrix scatter were removed. That block copied all player positions and calculated shifts even though positions were not reread in the same batch. Camera presentation is owned by the dedicated fast camera lane.

### 15. Hot-path allocations reduced

Persistent/reserved storage is used for position history and several entity caches. Damage-marker and avatar caches now have stale-entry cleanup so long sessions do not grow them indefinitely.

## Asset loading / render spike fixes

### Steam avatars

The render thread no longer performs WIC decoding. HTTP/disk + WIC CPU decode runs in the background. The renderer uploads at most **one ready avatar texture per frame**. Background avatar work is capped at **two concurrent workers** to avoid a thread burst when a spectator list suddenly populates.

### Weapon icons

Filesystem/resource lookup and WIC PNG decoding also run in the background. D3D upload is capped at **one weapon texture per frame**, and background decoding is capped at **two concurrent workers**.

### RGB color calculation

Animated RGB sine values are calculated once per ImGui time sample rather than for every `Col()` call.

### Skeleton projection

Only bone slots required by the currently enabled skeleton chains/joints are projected. Joint color is calculated once outside the joint loop.

### System beep

The bomb warning beep no longer blocks the render path.

## Recommended effective cadence

| Data / task | Effective target |
|---|---:|
| ImGui ESP render | every overlay frame |
| View matrix | 4 ms |
| Motion/origins with skeleton | 12 ms |
| Motion/origins without skeleton | 16 ms |
| Full acquisition with aim | 16 ms baseline |
| Full acquisition with skeleton only | 20 ms baseline |
| Simple ESP full acquisition | 24 ms baseline |
| Performance-mode simple ESP | 28 ms baseline |
| Compact/full bones near | 16 ms |
| Bone LOD middle tiers | 24 / 32 ms |
| Bone LOD far | 50 ms |
| Local weapon | 50 ms |
| Other weapons | 100 ms |
| Armor | 50 ms |
| Local team | 500 ms |
| Names | 1000 ms |
| Spectators | 250 ms |
| Bomb memory state | 50 ms |
| Entity-list full validation | ~1500 ms or on failure |

The acquisition scheduler still backs off to at least 24 ms after a scan exceeds 12 ms, and to at least 32 ms after a scan exceeds 20 ms.

## Telemetry added

The CS2 diagnostics panel now shows:

- current snapshot age / acquisition Hz / entity count
- acquisition duration in ms
- snapshot publication interval
- snapshot drops

### What to look for during testing

1. **Acquisition ms** should normally remain well below the selected full-scan interval.
2. **Snapshot drops** should normally stay at zero. A growing counter means consumers are pinning slots for too long or the producer is outrunning the exchange.
3. Compare frametime when enabling skeleton, weapon icons and spectator list together. First-time assets should now appear progressively rather than creating one large render spike.
4. Test map/round transitions and death/spectate transitions because these exercise entity-list and observer recovery paths.
5. Test aim with skeleton disabled: the compact pose path should retain accurate head/neck/chest/stomach targeting without paying for arms/legs.

## Build validation note

The source tree was checked for stale references to the old shared scatter handle, old Runtime deep copy, old MotionSnapshot bone payload and the redundant pre-refresh block. `git diff --check` passes. This environment does not contain the Windows/MSVC SDK and project toolchain needed to perform a trustworthy native build of the Direct3D/Windows project, so the final compile and runtime validation must be done with the project's normal Visual Studio configuration.
