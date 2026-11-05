# Performance review — 5 November 2026

This is an initial code-review and measurement checkpoint, not a completed end-to-end performance audit.

## Reproducible interaction computation baseline

Build the Release target `nyxmap_interaction_graph_test`, then run:

```
./build/nyxmap_interaction_graph_test --benchmark
```

On the local Apple M3 Ultra / arm64 machine, after 100 warm-up steps, 10,000 steps averaged 116.599 microseconds per step. The fixture has one source, 256 independent Damper filters, and four destinations per filter: 1,281 nodes total. Each shared filter advances once per step. This is an isolated graph-computation measurement; it excludes engine parameter resolution/writes, event ingestion, UI painting, video decoding and GPU rendering. It is a baseline, not a measured improvement or a frame-rate guarantee.

## Corrections made during review

- Unchanged graph outputs no longer continuously reclaim a destination from manual edits.
- Missing internal source parameters clear their held graph input.
- A Select block can use input A without requiring B to be connected. Selecting an unconnected input produces no output.
- Graph filter fan-out and temporal evolution are compared with a single reference processor.
- Filter-only branches and floating graph editor lifecycle have integration coverage.

## Rendering paths requiring measurement

- `Engine::renderFrame` in `src/engine/Render.cpp`: immediate sharing calls `glFinish`; delayed sharing can wait up to 4 ms on a fence. The timeout alone is 24% of a 60 Hz frame budget or 48% at 120 Hz. Actual wait time has not been measured. Do not remove synchronization without testing real sharing receivers.
- The sonification path performs a synchronous 16 × 16 `glReadPixels` after a blit. Small transfer size does not establish low synchronization cost.
- Output/viewport grab paths perform synchronous full-image readbacks. Their callers and frequency must be profiled before changing behavior.
- `src/engine/Model3D.cpp` still resolves several scene/post-processing uniform locations during drawing, although mesh placement uniforms already have caches. Driver overhead must be measured before choosing a broader cache.

## Outstanding validation

Hosted Linux/macOS/Windows checks for the current PR heads remain authoritative. The GPU findings above are review candidates, not proven causes of the reported slowdown. Mixed video/3D/audio projects, interaction event batches, timeline/sequence scheduling and physical display timing still need integrated measurements. No general “software optimized” conclusion is justified yet.
