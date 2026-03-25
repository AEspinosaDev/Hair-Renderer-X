# Subsurface Scattering Implementation Plan

## Overview

Add a screen-space Subsurface Scattering (SSS) post-process to the forward renderer, using Burley's Normalized Diffusion Model for multiple scattering and Beer-Lambert translucency for single scattering. The forward renderer is extended with Multiple Render Targets (MRT) to provide the necessary screen-space data, preserving hardware MSAA for hair strands.

---

## Proposed Pipeline

```
Shadow → HairScatter → HairVoxel → Forward(+MRT) → SSAO/Thickness → SSS → Bloom → Tonemap → FXAA
```

Pass indices in `ForwardRenderer::RendererPasses`:

| Index | Enum | Type |
|-------|------|------|
| 0 | SHADOW_PASS | Variance shadow map |
| 1 | HAIR_SCATTER_PASS | Compute |
| 2 | HAIR_VOXELIZATION_PASS | Compute |
| 3 | FORWARD_PASS | Rasterization (MSAA + MRT) |
| 4 | SSAO_PASS | Post-process |
| 5 | SSS_PASS | Post-process |
| 6 | BLOOM_PASS | Post-process |
| 7 | TONEMAPPIN_PASS | Post-process |
| 8 | FXAA_PASS | Post-process |

---

## Forward Pass MRT Layout

All color attachments 0–6 are multisampled (matching `RendererSettings::samplesMSAA`). When MSAA is enabled, each has a resolved single-sample counterpart at index+7. Post-process passes always read the resolved versions. Depth is not resolved (not needed by post-process).

### Color attachments (indices 0–6)

| Index | Format | Content |
|-------|--------|---------|
| 0 | SRGBA_32F | HDR color |
| 1 | SRGBA_32F | Bright color (for bloom) |
| 2 | SRGBA_16F | View-space normals.rgb |
| 3 | SRGBA_16F | Albedo.rgb + scatterMask in A (cleared to alpha=0) |
| 4 | SRGBA_16F | Diffuse irradiance.rgb (front-facing, no albedo multiply, no specular) |
| 5 | SRGBA_16F | Back irradiance.rgb (lighting evaluated with flipped normal) |
| 6 | SR_32F | Linear depth copy (gl_FragCoord.z, cleared to 1.0) |
| depth | D32F | Depth (not sampled by post-process) |

### Resolve attachments (MSAA only, indices 7–13)

| Index | Resolves | Read by |
|-------|----------|---------|
| 7 | HDR color | Bloom / SSS |
| 8 | Bright color | Bloom |
| 9 | Normals | SSAO |
| 10 | AlbedoMask | SSS |
| 11 | DiffuseIrradiance | SSS |
| 12 | BackIrradiance | SSS |
| 13 | LinearDepth | SSAO, SSS |

Hair shaders write `scatterMask = 0.0` to attachment 3 alpha. The SSS pass skips pixels where `scatterMask == 0`.

**Hardware note**: 7 color attachments requires `maxColorAttachments >= 7`. All modern desktop GPUs (NVIDIA, AMD, Intel Xe) report 8. Vulkan spec minimum is 4.

---

## SSAO/Thickness Pass

- **Input**: Linear depth (resolved attachment 13 / attachment 6 non-MSAA), view-space normals (resolved attachment 9 / attachment 2 non-MSAA) from the forward pass
- **Output**: Single RT — R = ambient occlusion, G = thickness
- **Algorithm**: Hemisphere sampling using a TBN matrix built from the surface normal and a random vector from a noise texture. AO samples the hemisphere along the normal; thickness samples the hemisphere along the inverted normal. Same kernel, two evaluations.
- **Noise texture**: Small RGB texture (e.g. 4x4 or 8x8) generated on the CPU with random XY vectors, uploaded once. Nearest sampling, tiled across the screen via `uv * (screenSize / noiseTextureSize)`.
- **Kernel samples**: Hemisphere sample positions generated on the CPU, passed as a uniform array (`vec3 samples[MAX_SAMPLES]`).
- **No blur pass**.
- AO is only consumed by the SSS pass, not applied globally.

---

## SSS Pass

- **Inputs**: HDR color (att 0), albedo + scatterMask (att 2), diffuse irradiance (att 3), back irradiance (att 4), depth, SSAO/thickness output
- **Output**: Single RT — final scattered color (replaces HDR color going into bloom)
- **Algorithm**: Disk-based Burley Normalized Diffusion for multiple scattering. Beer-Lambert with thickness for single scattering (translucency).
- **Scattering distance**: Uniform `vec3`, not a per-pixel texture.
- **Sample generation**: CPU-side Fibonacci lattice for angular distribution, CCDF inverse for radial distribution. Passed as uniform array (`vec2 samples[MAX_SAMPLES]`).
- **ScatterMask**: Pixels with `scatterMask == 0` (hair, non-skin) pass through the original HDR color unchanged.

---

## Implementation Tasks

### Task 1: Extend ForwardPass MRT — ✅ DONE

**Files to modify**:
- `ext/Vulkan-Engine/include/engine/core/passes/forward_pass.h`
- `ext/Vulkan-Engine/src/core/passes/forward_pass.cpp`

**Steps**:
1. In `setup_attachments()`, add 3 new color attachments (normals, albedo+mask, diffuse irradiance, back irradiance) after the existing HDR color attachment. Each should be `RGBA16F`, with `IMAGE_USAGE_COLOR_ATTACHMENT | IMAGE_USAGE_SAMPLED`, final layout `LAYOUT_SHADER_READ_ONLY_OPTIMAL`, and `ASPECT_COLOR`.
2. When MSAA is enabled, each new attachment needs a corresponding resolve attachment (same as the existing color attachment pattern).
3. Update subpass dependencies to ensure all new attachments are properly synchronized.
4. Update `setup_uniforms()` if new descriptor sets are needed for the additional attachments.
5. Verify `create_framebuffer()` handles the additional attachment images correctly.

### Task 2: Modify Forward Shaders to Output MRT Data — ✅ DONE

**Files modified**:
- `ext/Vulkan-Engine/resources/shaders/forward/physically_based.glsl` (PBR / skin)
- `ext/Vulkan-Engine/resources/shaders/forward/hair_strand_epic.glsl` (hair)
- `ext/Vulkan-Engine/resources/shaders/forward/fast_hair_strand_epic.glsl` (hair variant)
- `ext/Vulkan-Engine/resources/shaders/forward/hair_strand_disney.glsl` (hair variant)
- `ext/Vulkan-Engine/resources/shaders/forward/hair_strand.glsl` (hair base)
- `ext/Vulkan-Engine/resources/shaders/forward/skybox.glsl`, `unlit.glsl`
- `phong.glsl` skipped (DEPRECATED, not used at runtime)

**Implemented**:
- PBR shader: light loop refactored to extract `float shadowFactor` and accumulate `diffuseIrr` / `backIrr` in parallel. New outputs: `loc 2` = view-space normal (`brdf.normal`, includes normal map), `loc 3` = `vec4(albedo, 1.0)` (scatterMask=1), `loc 4` = `vec4(diffuseIrr, 0)`, `loc 5` = `vec4(backIrr, 0)`, `loc 6` = `vec4(gl_FragCoord.z, 0, 0, 0)`.
- All hair / skybox / unlit shaders: `loc 2–5` = `vec4(0.0)`, `loc 3` alpha = 0 (scatterMask=0, SSS skips), `loc 6` = raw depth.

### Task 3: Create SSAO/Thickness Pass — ✅ DONE

**New files**:
- `ext/Vulkan-Engine/include/engine/core/passes/ssao_pass.h`
- `ext/Vulkan-Engine/src/core/passes/ssao_pass.cpp`
- `ext/Vulkan-Engine/resources/shaders/misc/ssao_thickness.glsl`

**Implemented**:
- `SSAOPass` inherits `BasePass` directly (not `PostProcessPass`) — needs 3 input textures and a custom UBO.
- Output: single `RGBA16F` attachment (R = AO, G = thickness). `isDefault = false`. Final layout `LAYOUT_SHADER_READ_ONLY_OPTIMAL`.
- Descriptor layout (binding 0 = LinearDepth, 1 = Normals, 2 = Noise, 3 = UBO).
- UBO struct `SSAOUniforms`: `vec4 samples[64]` (w=0), `mat4 projection`, `mat4 invProjection`, `vec2 screenSize`, `float radius`, `float bias`, `int kernelSize`, padding to 16-byte boundary. Matches std140.
- Noise texture: 4×4 RGBA8U, random RG, generated in-pass via `Core::Texture` + `ResourceManager::upload_texture_data`. Tiled over screen in shader by `uv * (screenSize / 4.0)`.
- Hemisphere kernel: 64 samples generated in the constructor with accelerating radial distribution. Stored as `Vec4` (w=0) to avoid std140 vec3 padding issues.
- `render()`: updates full UBO each frame from `scene->get_active_camera()` (handles resize / camera change correctly).
- `link_previous_images(images)`: `images[0]` = LinearDepth, `images[1]` = Normals (dependency table order from Task 5).
- Shader: reconstructs view-space position from depth via `invProjection` (perspectiveRH_ZO convention). `occlusion()` and `thickness()` use hemisphere sampling with TBN matrix randomized by noise. Background pixels (depth ≥ 1.0) output `vec4(1,0,0,0)` immediately.
- `cleanup()`: frees UBO buffer and noise texture.

### Task 4: Create SSS Pass — ✅ DONE

**New files**:
- `ext/Vulkan-Engine/include/engine/core/passes/sss_pass.h`
- `ext/Vulkan-Engine/src/core/passes/sss_pass.cpp`
- `ext/Vulkan-Engine/resources/shaders/misc/ssss.glsl`

**Implemented**:
- `SSSPass` inherits `BasePass`. Output: single `SRGBA_32F` attachment (replaces HDR in Bloom input), `isDefault = false`, final layout `LAYOUT_SHADER_READ_ONLY_OPTIMAL`.
- Descriptor layout: bindings 0–5 = combined image samplers (HDR, AlbedoMask, DiffuseIrr, BackIrr, LinearDepth, AO+Thickness), binding 6 = UBO.
- `SSSUniforms` struct: `Vec4 samples[25]` (xy = r/theta, zw = 0), `sampleCount`, `maxScatter`, `extinctionCoeff`, `Fdr`, `Vec4 scatteringDistance` (rgb), `Vec2 screenSize`, padding, `Mat4 projection/invProjection`. Matches std140.
- Sample generation: Fibonacci lattice (golden angle) + Burley radial CDF inverse (bisection, 64 iters, unit scatter distance). Importance-sampled so equal weights used in shader.
- `render()`: uploads full UBO each frame from camera matrices and current parameters.
- `link_previous_images(images[0..5])`: stores and binds all 6 input images.
- `cleanup()`: frees UBO buffer.
- Shader: `nonDiffuse = hdr - albedo * diffIrr` preserves specular/ambient. Disk loop evaluates per-channel `burleyWeight(r, scatterDist)`, normalizes per channel. AO modulates scattered irradiance. Beer-Lambert: `T = exp(-thick * extinctionCoeff)`, `singleScatter = (1-Fdr) * albedo * backIrr * T`. ScatterMask==0 early-exit passes hdr through unchanged. depth≥1.0 early-exit for background.

### Task 5: Wire Passes into ForwardRenderer — ✅ DONE

**Files modified**:
- `ext/Vulkan-Engine/include/engine/systems/renderers/forward.h`
- `ext/Vulkan-Engine/src/systems/renderers/forward.cpp`

**Implemented**:
1. `RendererPasses` enum updated: SSAO_PASS=4, SSS_PASS=5, BLOOM_PASS=6, TONEMAPPIN_PASS=7, FXAA_PASS=8.
2. `m_passes.resize(9)` in `create_passes()`.
3. `SSAOPass` at index 4 — dependency: `{iVec2(FORWARD_PASS, 0), {msaa?13:6, msaa?9:2}}` (LinearDepth, Normals).
4. `SSSPass` at index 5 — dependency: `FORWARD_PASS` provides {HDR, AlbedoMask, DiffIrr, BackIrr, Depth, Bright} and `SSAO_PASS` provides {AO+Thickness}. SSSPass also added a **second output attachment** (att 1 = Bright pass-through at binding 7), so Bloom reads from a single source `{iVec2(SSS_PASS, 0), {0, 1}}`, avoiding non-deterministic multi-source ordering from the unordered_map.
5. SSSPass changes: `setup_attachments` declares 2 attachments; `setup_uniforms` adds binding 7 (Bright sampler); `link_previous_images` updated to expect 7 inputs ([5]=Bright, [6]=AO); `ssss.glsl` adds `brightTex` binding 7 and `outBright` second output with pass-through at all exit paths.
6. `BLOOM_PASS` reads `{0, 1}` from `SSS_PASS` (scattered HDR + Bright).
7. Added SSAO getters/setters (`radius`, `bias`, `kernelSize`) and SSS getters/setters (`maxScatter`, `scatteringDistance`, `extinctionCoeff`) on `ForwardRenderer`.

### Task 6: Update ResourceManager — ✅ DONE (no-op)

Both passes are self-contained:
- `SSAOPass` generates and uploads its own noise texture in `setup_uniforms()` via the existing `ResourceManager::upload_texture_data()` / `destroy_texture_data()` static helpers. No new static members needed.
- `SSSPass` needs no external textures; samples are uploaded via UBO.
- `ResourceManager::VIGNETTE` (fullscreen quad) is already used by both passes, passed via constructor.

### Task 7: Update CMakeLists (if needed) — ✅ DONE (no-op)

The engine uses `file(GLOB_RECURSE ...)` via `add_module_files()` in `cmake/add_module_files.cmake`. All `.cpp` and `.h` files in the `core/passes/` directories are automatically picked up — no changes needed.

### Task 8: Create Debug Test Harness — ✅ DONE

**Goal**: Enable Claude (and the user) to run the renderer in debug mode, capture the Vulkan validation layer trace to a file, and inspect it for errors — without needing visual inspection. The user will do their own visual-inspection runs separately.

**Files to modify**:
- `src/main.cpp` — new CLI flags
- `ext/Vulkan-Engine/thirdparty/logger/include/logger.h` — minor additions if needed
- `ext/Vulkan-Engine/include/engine/graphics/utilities/utils.h` — verbosity-filtered debug callback
- `ext/Vulkan-Engine/src/graphics/utilities/utils.cpp` — wire severity filter
- `src/application.h` / `src/application.cpp` — frame-limited render loop

**New CLI flags**:
- `--frames N` — Run for N frames then auto-exit (default: no limit, interactive). Suggested test value: 10.
- `--log-level <error|warn|verbose>` — Controls which Vulkan validation layer messages are captured. `error` = errors only, `warn` = warnings + errors (default), `verbose` = all including verbose/info.
- `--log-file <path>` — Path to write the trace file. Fixed default: `build/debug_trace.log`.

**Steps**:
1. Parse the three new CLI flags in `main.cpp`, pass them through to `Logger::init()` and the application.
2. Modify the Vulkan debug callback (`debugCallback` in `utils.h`) to filter messages based on the configured log level. Currently it captures verbose+warning+error indiscriminately — gate each severity behind the chosen verbosity.
3. Initialize the Logger with file output (`Logger::init(level, filePath)`) when `--log-file` is provided (or always when `--frames` is used, defaulting to `build/debug_trace.log`).
4. Add a frame counter to the render loop in `application.cpp`. When `--frames N` is set, exit cleanly after N iterations of `tick()`.
5. Ensure the log file is flushed and closed on exit (`Logger::shutdown()`).
6. Validation layers must be enabled (Debug build). The test is always run with a Debug build — document this requirement.

**Verbosity levels mapping**:
| `--log-level` | Vulkan severities captured |
|---------------|---------------------------|
| `error` | `ERROR` only |
| `warn` | `WARNING` + `ERROR` |
| `verbose` | `VERBOSE` + `INFO` + `WARNING` + `ERROR` |

**Future extensions** (not in scope now):
- An extra verbosity level that also captures engine-level logs (pass setup, resource creation, etc.).
- A dedicated lightweight test scene for faster iteration.

**Output**: Running `./HairViewer --frames 10 --log-level warn` from `build/` produces `build/debug_trace.log` containing timestamped validation layer messages (or an empty/minimal file if no issues).

---

### Task 9: Validation Run — Fix Pipeline Issues — 🔲 NOT STARTED

**Goal**: Run the debug test harness from Task 8, inspect the validation trace, and fix all Vulkan validation errors and warnings introduced by the new passes (SSAO, SSS, MRT changes).

**Procedure**:
1. Build in Debug mode: `cd build && cmake -DCMAKE_BUILD_TYPE=Debug .. && cmake --build .`
2. Run: `./HairViewer --frames 10 --log-level verbose --log-file debug_trace.log`
3. Claude reads `build/debug_trace.log` and triages:
   - **Errors** — must fix. Likely causes: descriptor set mismatches, missing image transitions, incorrect attachment references, synchronization issues.
   - **Warnings** — fix if related to new code; document if pre-existing.
   - **Verbose/Info** — scan for anomalies, no action unless suspicious.
4. Fix each issue, re-run, verify the fix.
5. Repeat until the trace is clean (no errors, no new warnings).

**Subtasks**: Unknown at this point — will be added as issues are discovered during the validation run. Each fix may require its own subtask (e.g., "Fix missing image layout transition in SSAOPass", "Fix descriptor binding mismatch in SSSPass", etc.). Expect iterative cycles of run → read trace → fix → re-run.

**Completion criteria**: A clean trace (zero errors, zero warnings from new code) over a 10-frame run with `--log-level verbose`.

---

### Task 10: Update GUI (optional, but useful for tuning) — 🔲 NOT STARTED

**Files to modify**:
- `src/gui.h` / `src/gui.cpp`
- Possibly `ext/Vulkan-Engine/include/engine/tools/widgets.h`

**Steps**:
1. Add sliders/controls for SSS parameters: scattering distance (vec3), extinction coefficient, max scatter radius, sample count.
2. Add toggle to enable/disable the SSS pass.
3. Add sliders for SSAO parameters: radius, bias, kernel size.
4. Add toggle to enable/disable the SSAO pass.

---

## Notes

- View-space position is reconstructed from depth + inverse projection in both the SSAO and SSS shaders, avoiding the need for a dedicated position MRT.
- The SSAO reference shader uses `texturePosition` directly. This needs to be adapted to use depth reconstruction instead.
- All MSAA attachments are resolved before the SSAO pass. Performance optimization (selective resolve) can be revisited later.
- The SSS sample array changes per frame (Fibonacci lattice offset by frame counter) for temporal accumulation. The uniform buffer must be updated each frame.
- Scattering distance is a uniform `vec3` for now. Can be upgraded to a per-pixel texture later if needed for multi-material skin.
