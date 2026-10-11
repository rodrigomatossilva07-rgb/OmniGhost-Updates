# CS2 runtime architecture

`cs2_game.cpp` is the composition root for the CS2 acquisition runtime. It
owns the shared DMA scatter handles and includes the tightly coupled pipeline
fragments in execution order. Components with independent ownership are normal
translation units and expose narrow interfaces.

## Independently compiled components

- `runtime/cs2_camera.*` owns the camera exchange, history and deadline state.
- `runtime/cs2_runtime.*` owns immutable runtime/config/liveness exchanges.
- `visibility/cs2_visibility_worker.*` owns the latest-state-wins CPU worker.
- `collision/cs2_collision_loader.*` owns the cancelable map-loading worker.
- `state/cs2_cache_manager.*` owns session generation and cache metrics.
- `telemetry/cs2_telemetry.*` owns pressure and phase telemetry state.
- `core/cs2_core_world.*` contains pure world validation/frustum helpers.
- `metadata/cs2_metadata.*` contains pure classification/formatting helpers.
- `offsets/cs2_offsets.cpp` validates a complete offset snapshot.
- `bones/cs2_bones.cpp` owns the joint mapping.

## Acquisition fragments

The `.inl` files are deliberately part of the composition translation unit.
They share the two DMA scatter owners and must preserve a single scheduler and
single runtime-snapshot producer. Moving them to arbitrary `.cpp` files would
require exposing mutable globals and would make ownership less safe.

- `offsets/`: offset loading, attach, recovery and live validation.
- `session/`: generation changes, map reset, detach and reinitialization.
- `core/`: critical position/health/velocity acquisition and snapshot assembly.
- `bones/`: optional adaptive enrichment and bone publication.
- `spectators/`, `dropped_items/`, `bomb/`: optional feature lanes.
- `runtime/`: optional-worker queues and thread orchestration.

## Invariants

1. Camera deadlines and snapshots are independent from optional work.
2. Core world is the sole producer of runtime snapshots.
3. Optional workers publish results through exchanges; they do not mutate the
   live runtime directly.
4. Pointer-keyed caches compare the central session generation before reuse.
5. `ResetSessionState` is the authoritative map/detach reset boundary.
6. Workers are stopped or reset explicitly; no detached worker may outlive the
   CS2 runtime.
