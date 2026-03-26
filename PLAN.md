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

## Completed Tasks (Phase 1)

| # | Task | Summary |
|---|------|---------|
| 1 | Extend ForwardPass MRT | 7 color attachments (HDR, Bright, Normals, AlbedoMask, DiffuseIrr, BackIrr, LinearDepth) + MSAA resolve counterparts |
| 2 | Forward shader MRT outputs | PBR outputs diffuseIrr/backIrr/normals/albedo+scatterMask/depth; hair/skybox/unlit write scatterMask=0 |
| 3 | SSAO/Thickness pass | Hemisphere sampling, 4x4 noise texture, outputs R=AO G=thickness |
| 4 | SSS pass | Burley diffusion (multiple scattering) + Beer-Lambert translucency (single scattering), 2 output attachments (scattered HDR + bright pass-through) |
| 5 | Wire passes into ForwardRenderer | Enum reordered (SSAO=4, SSS=5, BLOOM=6, TONEMAP=7, FXAA=8), dependency tables, Bloom reads from SSS |
| 6 | Update ResourceManager | No-op — passes are self-contained |
| 7 | Update CMakeLists | No-op — GLOB_RECURSE picks up new files |
| 8 | Debug test harness | `--frames N`, `--log-level`, `--log-file` CLI flags for headless validation runs |
| 9 | Validation run | Fixed 5 Vulkan validation issues (descriptor pool, blend attachments, phong MRT, bloom descriptor update, swapchain format) |
| 10 | GUI controls | SSAO/SSS parameter sliders, enable toggles, animate light checkbox |

**Post-plan hotfixes**:
- Segfault: missing `m_samples.resize(MAX_SAMPLES)` in revamped `generate_samples()`
- std140 UBO mismatch: `vec2 samples[]` → `vec4 samples[]` in GLSL and C++
- Missing `#include <chrono>`

**Known issue — fixed in Phase 2 Task 1**: SSS shader used `depth * 2.0 - 1.0` for NDC reconstruction (incorrect for Vulkan ZO) and negated z. Fixed to use depth directly, matching the SSAO pattern.

---

## Phase 2 Tasks

### Task 1 — Fix SSS view-space reconstruction and diffuse extraction

The SSS shader (`ssss.glsl`) produces distorted output (diagonal seam across the face, incorrect scattering) because view-space positions are reconstructed incorrectly from depth. A secondary issue clips HDR specular in the non-diffuse extraction.

**Subtask A — Fix depth → view-space reconstruction (projection bug)**

The shader uses an OpenGL-style unproject that is wrong for Vulkan ZO (`perspectiveRH_ZO` + Y-flip):

```glsl
// CURRENT (wrong):
float z = depth * 2.0 - 1.0;                                    // remaps [0,1] → [-1,1]
vec4 viewSpacePos = sss.invProjection * vec4(fragCoords, -z, 1.0); // negates z
```

The SSAO shader (`ssao_thickness.glsl:45-49`) already has the correct formula:

```glsl
// CORRECT:
vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);  // depth stays in [0,1]
vec4 viewPos = ssao.invProjection * clipPos;
return viewPos.xyz / viewPos.w;
```

Fixes needed in `ssss.glsl`:
1. Center pixel (line 109–112): remove `* 2.0 - 1.0` on depth, remove `-z` negation → use `vec4(fragCoords, depth, 1.0)`
2. Sample pixel (line 127–129): same fix — use raw depth and `sampleCoords` without negation → `vec4(sampleCoords, sampleDepth, 1.0)`

**Subtask B — Remove HDR clamp on non-diffuse extraction**

```glsl
// CURRENT (clips specular):
vec3 nonDiffuse = clamp(hdr.rgb - albedo * diffIrr, 0.0, 1.0);
```

The `clamp(..., 0.0, 1.0)` upper-bounds specular highlights to 1.0, killing HDR values that Bloom needs. Change to `max(..., 0.0)` to keep the lower bound (preventing negatives from precision drift) but allow HDR values through.

**Validation**: rebuild, run `--frames 10 --log-level warn`, verify no new validation errors. Visual check: the diagonal seam should disappear and skin should show smooth subsurface scattering.
