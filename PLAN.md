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

---

## GLB Loading Support (Standalone Task)

### Goal

Add GLB (glTF Binary) file loading to the engine using tinygltf. This enables loading skeletal meshes with morph targets (blendshapes) from the 4 test characters in `resources/models/`. The loader extracts all geometry data and stores skeletal/morph metadata for future animation work. Mesh selection is by index, so the caller decides which mesh(es) from the GLB to load.

### Architecture Decision: Skinning Data Storage

**Option C — Separate parallel data (chosen).** Joint indices and weights are stored alongside `GeometricData` as parallel vectors, not added to the `Vertex` struct. Reasons:

- The `Vertex` struct is the foundation of **every** VAO binding, pipeline, and shader in the engine. Adding 32 bytes (uvec4 + vec4) to it would force changes to every vertex input layout, break the hash function, waste memory on non-skinned meshes, and risk Vulkan validation errors across all passes.
- Skinning data is consumed by a compute/vertex shader at animation time via SSBO — it doesn't need to flow through the rasterization vertex attributes.
- Morph target deltas are also stored as parallel data (SSBO-ready), same pattern.
- This is the minimal-change approach: existing Vertex, VAO, and pipeline code is untouched.

### Data Model

```
GeometricData (existing)          SkinData (new, per-Geometry)
├─ vertexData: Vertex[]           ├─ jointIndices: uvec4[]      (per-vertex, 4 joints)
├─ vertexIndex: uint32_t[]        ├─ jointWeights: vec4[]       (per-vertex, 4 weights)
├─ voxelData: Voxel[]             ├─ inverseBindMatrices: mat4[] (per-joint)
├─ maxCoords, minCoords, center   └─ jointNames: string[]       (for debugging)
└─ loaded: bool
                                  MorphTargetData (new, per-Geometry)
                                  ├─ targets[]: { deltaPos: vec3[], deltaNormal: vec3[] }
                                  └─ targetNames: string[]
```

`SkinData` and `MorphTargetData` live on `GeometricData` as `std::optional` fields — present only when the source file contains them.

### GLB Test File Summary

| File | Meshes | Joints | Morphs (mesh 0) | Textures | Main mesh attrs | Secondary mesh attrs |
|------|--------|--------|------------------|----------|-----------------|----------------------|
| maria.glb | 4 | 55 | 100 | 1 | POS, NORMAL, UV, JOINTS, WEIGHTS | POS, JOINTS, WEIGHTS |
| javi.glb | 6 | 55 | 100 | 1 | POS, NORMAL, UV, JOINTS, WEIGHTS | POS, JOINTS, WEIGHTS |
| alex.glb | 4 | 55 | 100 | 1 | POS, NORMAL, UV, JOINTS, WEIGHTS | POS, JOINTS, WEIGHTS |
| nadia.glb | 4 | 55 | 100 | 1 | POS, NORMAL, UV, JOINTS, WEIGHTS | POS, JOINTS, WEIGHTS |

Main mesh (mesh 0) has normals and UVs. Secondary meshes (teeth, tongue, hearing aid) have only positions — normals must be computed from triangle topology. No tangents in any mesh — must be computed via Gram-Schmidt. Morph targets contain only POSITION deltas. 55-joint skeleton. 1 embedded texture per file.

### Tasks

| # | Task | Status | Description |
|---|------|--------|-------------|
| 1 | Add tinygltf to thirdparty | [x] | tinygltf v2.9.3 + nlohmann/json v3.11.3 under `thirdparty/tinygltf/include/`. INTERFACE CMake target, linked into VulkanEngine. |
| 2 | Extend GeometricData | [x] | Added `SkinData` (jointIndices uvec4[], jointWeights vec4[], inverseBindMatrices mat4[], jointNames) and `MorphTargetData` (targets[]{deltaPos vec3[]}, targetNames) as `std::optional` fields on `GeometricData`. Added `set_skin_data()` / `set_morph_target_data()` setters to `Geometry`. |
| 3 | Implement `load_GLB()` | [x] | Implemented in `loaders.cpp`. Extracts pos/normal/UV/color, computes normals from topology when absent, always computes tangents via Gram-Schmidt, reads JOINTS_0/WEIGHTS_0 for skinning, reads inverse bind matrices from skin[0], reads morph target POSITION deltas with names from `extras.targetNames`. |
| 4 | Wire GLB into `load_3D_file()` | [x] | Added `GLB`/`GLTF` defines to `common.h`. Added dispatch branch (sync and async) in `load_3D_file()`. |
| 5 | Add `USE_GLB_MODELS` path in application | [x] | Added `#define USE_GLB_MODELS` block in `application.cpp` loading `maria.glb` mesh 0 with PBR material. Uses `#ifdef`/`#elif`/`#else`/`#endif` chain with `USE_NEURAL_MODELS` and default. |
| 6 | Build and validate | [x] | Clean build (GCC, -O3). 10-frame headless run: zero Vulkan validation errors or warnings from new code. Pre-existing warnings unchanged. |

**Implementation notes**:
- `TINYGLTF_NO_STB_IMAGE` + `TINYGLTF_NO_STB_IMAGE_WRITE` defined before tinygltf include — geometry-only loader, no image decoding needed.
- `TINYGLTF_IMPLEMENTATION` defined at top of `loaders.cpp` (single translation unit), header only exposes the function declaration.
- `glm/gtc/type_ptr.hpp` included for `glm::make_mat4` used to parse column-major GLB matrices.
- `tinygltf::Value::IsObject()` used for extras check (this version has no `IsNull()`).

---

## Hair Card Rendering

### Context

The project currently renders hair as strand geometry (line segments expanded via geometry shaders). We need to add support for **hair cards** — textured triangle meshes that approximate hair volume using alpha-tested quads. The OBJ mesh (`resources/models/hair_fauxmohawk.obj`, ~21K vertices with UVs and normals) and texture (`resources/textures/hair_fauxmohawk.PNG`) come from Unreal MetaHumans.

The texture is a **packed data texture** (not albedo — the visible colors are false-color channel encoding):
- **R**: Alpha/opacity mask (confirmed by user)
- **G**: Root-to-tip gradient (for color variation along the strand)
- **B**: Per-strand random ID (for subtle per-strand hue/brightness variation)

Hair color will come from uniform values (rootColor, tipColor). The hair cards will be added alongside existing strand hair in the scene. There is also a `hair_fauxmohawk_rootscoverage.PNG` texture (density/flow data) that is not used in the initial implementation.

`HAIR_CARD_TYPE = 4` is already reserved in the `IMaterial::Type` enum (`material.h:47`) but has no implementation.

### Task 1: Create `HairCardMaterial` class

**Create** `ext/Vulkan-Engine/include/engine/core/materials/hair_card.h`

Follow the pattern established by `PhysicallyBasedMaterial` (`physically_based.h`):
- Class `HairCardMaterial` inherits `IMaterial` with type `HAIR_CARD_TYPE`
- Constructor sets default `MaterialSettings`: `alphaTest = true`, `faceCulling = false` (double-sided cards), `depthTest = true`, `depthWrite = true`
- Member variables with getters/setters (each setter sets `m_isDirty = true`):
  - `m_hairColor` (Vec3, default dark brown `{0.05, 0.02, 0.01}`) — fallback color when no root/tip distinction
  - `m_rootColor` (Vec3, default same as hairColor) — color at the root of strands
  - `m_tipColor` (Vec3, default same as hairColor) — color at the tip of strands
  - `m_roughness` (float, default 0.4) — surface roughness for lighting
  - `m_specularIntensity` (float, default 0.5) — Kajiya-Kay specular strength multiplier
  - `m_specularShift` (float, default 0.1 radians) — tangent shift for the primary Kajiya-Kay highlight
  - `m_alphaThreshold` (float, default 0.1) — discard threshold applied to R channel of packed texture
  - `m_colorVariation` (float, default 0.1) — how much the B channel (strand ID) varies the final color
- Texture enum and storage (same pattern as PBR's `m_textures` unordered_map):
  - `HAIR_DATA = 0` — the packed RGB texture (R=alpha, G=root-to-tip, B=strand ID)
  - `NORMAL = 1` — optional normal map (not used initially, slot reserved)
  - Initialize map: `{{HAIR_DATA, nullptr}, {NORMAL, nullptr}}`
- `m_textureBindingState` (unordered_map<int, bool>) for tracking GPU binding state
- Boolean flags: `m_hasHairDataTexture`, `m_hasNormalTexture`
- Texture setters follow PBR pattern: set flag, reset binding state to false, assign pointer, set dirty
- Override virtual methods: `get_uniforms()`, `get_textures()`, `get_texture_binding_state()`, `set_texture_binding_state()`

**Create** `ext/Vulkan-Engine/src/core/materials/hair_card.cpp`

Implement `get_uniforms()` following the PBR material pattern (`physically_based.cpp`). Pack into 8 Vec4 slots of `Graphics::MaterialUniforms`:
```
dataSlot1: {hairColor.r, hairColor.g, hairColor.b, alphaThreshold}
dataSlot2: {roughness, specularIntensity, specularShift, colorVariation}
dataSlot3: {rootColor.r, rootColor.g, rootColor.b, (float)hasHairDataTexture}
dataSlot4: {tipColor.r, tipColor.g, tipColor.b, (float)hasNormalTexture}
dataSlot5-8: Vec4(0.0) — reserved for future use
```

Status: [x] — `hair_card.h` and `hair_card.cpp` created.

### Task 2: Create `hair_card.glsl` forward shader

**Create** `ext/Vulkan-Engine/resources/shaders/forward/hair_card.glsl`

Unified vertex + fragment shader using the engine's `#shader vertex` / `#shader fragment` directive format.

**Vertex shader** (same structure as `physically_based.glsl`):
- `#version 460`
- `#include camera.glsl` and `#include object.glsl`
- Inputs: `layout(location = 0) in vec3 pos`, `layout(location = 1) in vec3 normal`, `layout(location = 2) in vec2 uv`, `layout(location = 3) in vec3 tangent`
- Outputs: `v_pos` (view-space), `v_normal` (view-space), `v_modelNormal`, `v_uv`, `v_modelPos`, `v_screenExtent`, `v_TBN` (mat3), `v_tangent` (view-space tangent for Kajiya-Kay)
- Transform logic identical to PBR vertex shader
- UV: `v_uv = vec2(uv.x, 1.0 - uv.y)` (may need to test with/without flip)

**Fragment shader**:
- `#version 460`, extensions for ray tracing/ray query (same as PBR)
- Includes: `camera.glsl`, `light.glsl`, `scene.glsl`, `object.glsl`, `utils.glsl`, `shadow_mapping.glsl`, `fresnel.glsl`, `IBL.glsl`, `reindhart.glsl`, `raytracing.glsl`
- Material uniform block (set=1, binding=1) matching the `get_uniforms()` layout above
- Texture samplers: `layout(set = 2, binding = 0) uniform sampler2D hairDataTex;`
- Global uniforms: shadow map (set=0 binding=2), irradiance map (set=0 binding=4), TLAS (set=0 binding=5), blue noise (set=0 binding=6)
- 7 MRT outputs matching forward pass: outColor, outBrightColor, outNormals, outAlbedoMask, outDiffuseIrr, outBackIrr, outLinearDepth

**Fragment logic**:
1. Sample packed texture: `vec4 texData = texture(hairDataTex, v_uv);`
2. Alpha test: `if (texData.r < material.alphaThreshold) discard;`
3. Compute base color:
   - Root-to-tip: `vec3 baseColor = mix(material.rootColor, material.tipColor, texData.g);`
   - Strand variation: `baseColor *= (1.0 + material.colorVariation * (texData.b - 0.5));`
4. Kajiya-Kay anisotropic specular (per-light):
   - `vec3 T = normalize(v_tangent);` (view-space tangent)
   - `float sinTL = sqrt(1.0 - pow2(dot(T, wi)));`
   - `float sinTV = sqrt(1.0 - pow2(dot(T, V)));`
   - Primary: `float spec1 = pow(sinTL * sinTV - dot(T, wi) * dot(T, V), specExponent);`
   - Secondary: shift tangent by `specularShift`, compute again with different exponent
   - Scale by `specularIntensity`
5. Diffuse: Lambert `max(dot(N, wi), 0.0) * radiance * baseColor`
6. Shadow mapping: same as PBR shader (VSM, classic, or raytraced depending on light settings)
7. IBL ambient: `computeAmbient()` from `IBL.glsl` (or simple `ambientColor * ambientIntensity * baseColor`)
8. MRT outputs:
   - `outColor = vec4(color, 1.0);` (alpha test already discarded transparent fragments)
   - `outBrightColor` = bloom threshold check
   - `outNormals = vec4(v_normal, 0.0);`
   - `outAlbedoMask = vec4(baseColor, 0.0);` (scatterMask = 0, skip SSS for hair cards)
   - `outDiffuseIrr = vec4(diffuseIrr, 0.0);`
   - `outBackIrr = vec4(backIrr, 0.0);`
   - `outLinearDepth = vec4(gl_FragCoord.z, 0.0, 0.0, 0.0);`

Status: [x] — `hair_card.glsl` created with Kajiya-Kay lighting, alpha test, 7 MRT outputs, IBL ambient.

### Task 3: Register shader pass in forward renderer

**Modify** `ext/Vulkan-Engine/src/core/passes/forward_pass.cpp`

In `setup_shader_passes()`, add after the `HAIR_STR_EPIC_TYPE` block (after line 331) and before the Disney hair block (line 334):

```cpp
GraphicShaderPass* hairCardPass =
    new GraphicShaderPass(m_device->get_handle(), m_renderpass, m_imageExtent, ENGINE_RESOURCES_PATH "shaders/forward/hair_card.glsl");
hairCardPass->settings.descriptorSetLayoutIDs = {{GLOBAL_LAYOUT, true}, {OBJECT_LAYOUT, true}, {OBJECT_TEXTURE_LAYOUT, true}};
hairCardPass->graphicSettings.attributes      = {
    {POSITION_ATTRIBUTE, true}, {NORMAL_ATTRIBUTE, true}, {UV_ATTRIBUTE, true}, {TANGENT_ATTRIBUTE, true}, {COLOR_ATTRIBUTE, false}};
hairCardPass->graphicSettings.blendAttachments = blendAttachments;
hairCardPass->graphicSettings.dynamicStates    = dynamicStates;
hairCardPass->graphicSettings.samples          = samples;
m_shaderPasses[IMaterial::Type::HAIR_CARD_TYPE] = hairCardPass;
```

Key points:
- Topology = `VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST` (default, unlike hair strands which use LINE_LIST)
- `OBJECT_TEXTURE_LAYOUT = true` so material textures get bound
- All vertex attributes except COLOR enabled (OBJ has pos/normal/uv, tangents computed by loader)
- No push constants needed (unlike HAIR_STR_EPIC_TYPE)
- No geometry shader needed (unlike hair strands)

Also need to add `#include <engine/core/materials/hair_card.h>` at top of this file if not already pulled in via `core.h` (check — `forward_pass.cpp` includes `forward_pass.h` which includes `pass.h`, need to verify include chain reaches `hair_card.h`; the `#include <engine/core/materials/hair.h>` at line 1 suggests materials are included directly, so add `hair_card.h` there too).

Status: [x] — Hair card shader pass registered in `forward_pass.cpp` after `HAIR_STR_EPIC_TYPE` block (TRIANGLE_LIST topology, pos/normal/uv/tangent attributes, OBJECT_TEXTURE_LAYOUT enabled).

### Task 4: Alpha-tested shadow pass for hair cards

**Create** `ext/Vulkan-Engine/resources/shaders/shadows/shadows_alpha_geom.glsl`

Based on the existing `shadows_geom.glsl` (which has a geometry shader for layered rendering into shadow map array). Modifications:
- **Vertex shader**: Add `layout(location = 2) in vec2 uv;` input. Pass `v_uv` to geometry shader.
- **Geometry shader**: Pass through `v_uv` from vertex to fragment (add `in vec2 gs_uv[]` and `out vec2 v_uv`).
- **Fragment shader**: Instead of empty fragment, sample the hair data texture R channel:
  ```glsl
  layout(set = 1, binding = 1) uniform MaterialUniforms { ... alphaThreshold ... } material;
  layout(set = 2, binding = 0) uniform sampler2D hairDataTex;
  
  void main() {
      float alpha = texture(hairDataTex, v_uv).r;
      if (alpha < material.alphaThreshold) discard;
  }
  ```

**Modify** `ext/Vulkan-Engine/src/core/passes/shadow_pass.cpp`:

1. **`setup_uniforms()`**: Add a texture descriptor layout for set 2 with 1 combined image sampler binding (for the hair data texture). This mirrors what the forward pass has for `OBJECT_TEXTURE_LAYOUT`.

2. **`setup_shader_passes()`**: Add a third shader pass after line 115:
   ```cpp
   GraphicShaderPass* depthAlphaPass =
       new GraphicShaderPass(m_device->get_handle(), m_renderpass, m_imageExtent, ENGINE_RESOURCES_PATH "shaders/shadows/shadows_alpha_geom.glsl");
   depthAlphaPass->settings = settings;
   depthAlphaPass->settings.descriptorSetLayoutIDs = {{GLOBAL_LAYOUT, true}, {OBJECT_LAYOUT, true}, {OBJECT_TEXTURE_LAYOUT, true}};
   depthAlphaPass->graphicSettings = gfxSettings;
   depthAlphaPass->graphicSettings.attributes = {
       {POSITION_ATTRIBUTE, true}, {NORMAL_ATTRIBUTE, false}, {UV_ATTRIBUTE, true}, {TANGENT_ATTRIBUTE, false}, {COLOR_ATTRIBUTE, false}};
   depthAlphaPass->build_shader_stages();
   depthAlphaPass->build(m_descriptorPool);
   m_shaderPasses[2] = depthAlphaPass;
   ```

3. **`render()`**: Update shader pass selection logic (line 146):
   ```cpp
   // Before: ShaderPass* shaderPass = mat->get_type() != IMaterial::Type::HAIR_STR_TYPE ? m_shaderPasses[0] : m_shaderPasses[1];
   // After:
   ShaderPass* shaderPass;
   if (mat->get_type() == IMaterial::Type::HAIR_STR_TYPE)
       shaderPass = m_shaderPasses[1];  // line geometry
   else if (mat->get_type() == IMaterial::Type::HAIR_CARD_TYPE)
       shaderPass = m_shaderPasses[2];  // alpha-tested triangles
   else
       shaderPass = m_shaderPasses[0];  // opaque triangles
   ```
   For HAIR_CARD_TYPE, also bind the material's texture descriptor set at set 2 (same pattern as forward pass):
   ```cpp
   if (mat->get_type() == IMaterial::Type::HAIR_CARD_TYPE)
       cmd.bind_descriptor_set(mat->get_texture_descriptor(), 2, *shaderPass);
   ```

Need to verify the existing shadow shader (`shadows_geom.glsl`) structure first — it uses a geometry shader for layered rendering into the shadow map array. The alpha variant must preserve this layered rendering while adding UV passthrough and fragment discard.

Status: [x] — Created `shadows_alpha_geom.glsl` (vertex passes UV with Y-flip, geometry layers per-light with UV passthrough, fragment discards on R < alphaThreshold). Modified `shadow_pass.cpp`: added 7-binding OBJECT_TEXTURE_LAYOUT in `setup_uniforms()` (matches forward pass for descriptor set compatibility); added `m_shaderPasses[2]` with UV attribute enabled and OBJECT_TEXTURE_LAYOUT enabled; updated `render()` to three-way pass selection and bind texture descriptor for HAIR_CARD_TYPE.

### Task 5: Add include in `core.h`

**Modify** `ext/Vulkan-Engine/include/engine/core.h`

Add after line 26 (`#include <engine/core/materials/hair_disney.h>`):
```cpp
#include <engine/core/materials/hair_card.h>
```

This ensures `HairCardMaterial` is available to any file that includes `<engine/core.h>`, including `application.cpp`.

Status: [x] — Added `#include <engine/core/materials/hair_card.h>` after `hair_disney.h` in `core.h`.

### Task 6: Wire up in application

**Modify** `src/application.cpp`

In `setup()`, inside the `#ifdef USE_GLB_MODELS` block, after the strand hair setup (after line 102), add:

```cpp
Mesh* hairCards = new Mesh();
Tools::Loaders::load_3D_file(hairCards, MESH_PATH + "hair_fauxmohawk.obj", false);
hairCards->set_position({0.0f, -12.6f, 0.2f});  // match character transform
hairCards->set_scale(10.0f);                      // match character scale
hairCards->set_rotation({0.0f, 180.0f, 0.0f});   // match character rotation

HairCardMaterial* hcMat = new HairCardMaterial();
hcMat->set_hair_color(Vec3(0.05f, 0.02f, 0.01f));  // dark brown base

Texture* hairDataTex = new Texture();
// IMPORTANT: Load as TEXTURE_FORMAT_TYPE_NORMAL (linear, not sRGB)
// because R/G/B encode data (alpha, gradient, ID), not perceptual color
Tools::Loaders::load_texture(hairDataTex, TEXTURE_PATH + "hair_fauxmohawk.PNG", TEXTURE_FORMAT_TYPE_NORMAL);
hcMat->set_hair_data_texture(hairDataTex);

hairCards->push_material(hcMat);
hairCards->set_name("HairCards");
m_scene->add(hairCards);  // added after character and strand hair for render order
```

Transform values (`position`, `scale`, `rotation`) will likely need tuning — the OBJ may already be in the character's local space (from the Unreal export) or may need adjustment. The initial values match the character mesh (`maria.glb`) transforms as a starting point.

Status: [x] — Added hair card mesh/material/texture setup in `application.cpp` inside `#ifdef USE_GLB_MODELS`, after strand hair block. `hair_fauxmohawk.obj` loaded synchronously; `HairCardMaterial` created with dark-brown base color; `hair_fauxmohawk.PNG` loaded as `TEXTURE_FORMAT_TYPE_NORMAL` (linear). Transform matches `maria.glb`.

### Task 7: Build and validate

1. **Build**: `cd build && cmake .. && cmake --build .`
2. **Debug test**: `./HairViewer --frames 10 --log-level warn` — check `debug_trace.log` for validation errors
3. **Visual test** (user performs): Run interactively, verify:
   - Hair cards render with correct alpha cutout (strand silhouettes, not solid rectangles)
   - Base hair color uniform applies correctly (dark brown)
   - Root-to-tip gradient visible (color changes from root to tip using G channel)
   - Kajiya-Kay specular highlights follow the hair tangent direction (anisotropic sheen)
   - Shadows show correct alpha-tested silhouette (not solid card shadows)
   - Existing strand hair still renders alongside cards (both visible)
   - No visual regressions in head/eyes rendering
4. **Tuning**: Transform, color, alpha threshold, specular parameters will likely need adjustment after first visual test

Status: [x] — Build clean (MSVC Debug). 10-frame headless run: zero new Vulkan validation errors or warnings from hair card code. Pre-existing warnings (STORAGE_IMAGE pool, swapchain semaphore reuse) unchanged from before this feature. Visual test pending user review.

### Files Summary

| Action | File | Purpose |
|--------|------|---------|
| Create | `ext/Vulkan-Engine/include/engine/core/materials/hair_card.h` | Material class definition |
| Create | `ext/Vulkan-Engine/src/core/materials/hair_card.cpp` | `get_uniforms()` implementation |
| Create | `ext/Vulkan-Engine/resources/shaders/forward/hair_card.glsl` | Forward rendering shader with Kajiya-Kay |
| Create | `ext/Vulkan-Engine/resources/shaders/shadows/shadows_alpha_geom.glsl` | Alpha-tested shadow shader |
| Modify | `ext/Vulkan-Engine/src/core/passes/forward_pass.cpp` | Register hair card shader pass (~10 lines) |
| Modify | `ext/Vulkan-Engine/src/core/passes/shadow_pass.cpp` | Add alpha-tested shadow pass + render selection |
| Modify | `ext/Vulkan-Engine/include/engine/core.h` | Add `#include` (1 line) |
| Modify | `src/application.cpp` | Scene setup (~15 lines) |

CMake picks up new `.cpp`/`.h` files automatically via `GLOB_RECURSE` in `add_module_files.cmake`.

### Risks & Notes

- **UV flip**: The PBR shader does `1-uv.y`. The MetaHuman OBJ may or may not need this — test both if alpha mask looks wrong.
- **Tangent quality**: OBJ loader computes tangents via Gram-Schmidt (`compute_tangents_gram_smidt()`). If Kajiya-Kay highlights look wrong, may need to derive tangent from UV gradient in shader instead.
- **Texture format**: Must load as `TEXTURE_FORMAT_TYPE_NORMAL` (linear, `RGBA_8U`) not color (`SRGBA_8`), since R/G/B encode data, not perceptual color. If loaded as sRGB, the gamma correction will distort the alpha threshold and gradient values.
- **Render order**: Hair cards are added after opaque geometry in the scene, which helps early-z rejection. No explicit sort needed since we use alpha test (discard), not alpha blending.
- **Double-sided**: Hair cards default to `faceCulling = false` since they are thin planar geometry that may be viewed from either side.
- **Shadow pass texture descriptor**: The existing shadow pass only has 2 descriptor set layouts (GLOBAL + OBJECT). Adding OBJECT_TEXTURE_LAYOUT for the alpha-tested pass requires adding the layout to the shadow pass's descriptor pool — need to verify pool allocation is sufficient.

---