# Raymarcher version benchmark

Every working raymarcher version is built into the engine and can be switched at runtime
(Ray-marcher window → **Version**). The **Version Benchmark** window runs the same benchmark against each one.

| Id | From commit | Technique |
|---|---|---|
| `v0-direct` | 0bdbf15 | No cache: evaluates every primitive per pixel per step |
| `v1-dense-raster` | 20aca78 | Dense 3D texture, baked slice by slice in a fragment shader |
| `v2-dense-jfa` | ef5bc68 | Dense 3D texture, baked with jump flooding in compute shaders |
| `v3-sparse` | ea5db90 | Sparse brick pool, sphere tracing inside bricks |
| `v4-analytic` | db80f3d | Sparse brick pool, analytic cubic ray/trilinear intersection per voxel cell. v3 and v4 share one copied class (`core/RaymarchVersions/v4`) and differ only in their march shader |
| `v5-sparse-color` | 8b11928 (main) | v3-sparse plus a per-texel primitive id in the brick pool, for color |
| `v6-dynamic-sparse` | 3ad342e | Chunked sparse bake that rebakes only bricks around primitives that changed, every frame (copy in `core/RaymarchVersions/v6`). Includes the shading-artifact and Perlin noise fixes, so its terrain differs from v2–v5 |
| `v7-materials` | current | The engine's own `core::Raymarcher` (no copy): v6 plus per-primitive materials (textures, normal/roughness maps, surface shaders) lit by the scene's lights, and the terraced-mesa terrain |

Each old version's code and shaders are copies from its commit (`core/RaymarchVersions/v1`, `v2`, `v4`, `v5`,
`Assets/shaders/versions/`). v3 shares v4's copied bake code; only its march shader
(`Assets/shaders/versions/v3-sparse/march.frag`) is a copy. Only include paths, namespaces and shader paths were changed, plus the
`Primitive::halfExtents` → `data` rename. The one exception is v1's per-frame debug readback of the whole
volume, which is commented out (as it was later in ef5bc68) because it distorted v1's timings. Their bugs are
otherwise deliberately kept.

## What is identical for every version
- Scene (reloaded fresh for each version), camera poses, render size, volume resolution and bounds,
  max steps / max distance / surface distance, warmup and measured frame counts, bake repeats, and the brick pool size of every version that has one (v3 and later; 32^3 bricks by default, v7's own default is 64^3).
- Measurement: GPU timestamps around the raymarch pass only, and around `Bake`. VRAM is the
  `GL_NVX_gpu_memory_info` used-memory delta against a reading taken with no raymarcher loaded.
  Only one version exists at a time, so their memory never overlaps.

## Running
- In the editor: open **Version Benchmark**, pick the scene and versions, press **Run comparison**.
- From the command line (working directory = `RawEngine_v2`):
  `RawEngine.exe --version-benchmark Benchmark_common` — add `--quick` for a short smoke test.
  The engine closes when the run ends.

Results go to `Assets/Data/comparison/<scene>_<date>/`: `results.csv` (same format as the Benchmark
Tool), `meta.json` (git hash, GPU, all settings, poses) and a `.ppm` screenshot per version and pose.

## Scenes and poses
- `Benchmark_common`: every primitive type except Plane and Terrain, all rotations 0. Versions before
  63bc651 read rotations in degrees as radians, so non-zero rotations would differ between versions.
- `Terrain_test`: v0 and v1 have no terrain and are skipped automatically.
- `Benchmark_full`: the `Benchmark_common` layout with materials on the raymarched primitives (textured, procedural, layered and one default), three textured rasterized meshes in front of them, and a sun plus two point lights. Only v7 shows the materials and lights on raymarched objects; older versions render the same geometry in flat colors, and every version composites with the same rasterized meshes.
- Poses live in `<scene>.poses.json` here. Use **Capture camera as pose** and **Save poses** to edit them.
  Without a poses file the scene's own camera transform is used.
