# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Configure (from repo root)
mkdir build && cd build
cmake ..

# Build (from build/)
cmake --build .

# With Ninja (recommended)
cmake -G Ninja .. && ninja
```

Prerequisites: Vulkan SDK 1.3.* (with VMA and Shaderc), CMake, Git LFS (resources stored via LFS). C++17.

## Architecture Overview

This is a **Vulkan strand-based hair renderer** built on top of a custom Vulkan engine (`ext/Vulkan-Engine/`). The application (`src/`) links against the engine as a library.

### Two-Layer Structure

- **Application layer** (`src/`): `HairViewer` (in `application.h/cpp`) orchestrates the lifecycle — scene setup, camera, lights, materials, neural hair loading. GUI lives in `gui.h/cpp`. Hair-specific asset loading in `hair_loader.h/cpp`.
- **Engine layer** (`ext/Vulkan-Engine/`): Contains the renderer, render passes, materials, RHI (Graphics/), resource management, and shader system. Headers in `include/engine/`, implementations in `src/`.

### Renderer — Forward Pipeline

The renderer is **forward** (not deferred). The `ForwardRenderer` (`systems/renderers/forward.h/cpp`) defines this pass sequence:

| Index | Pass | Type | Purpose |
|-------|------|------|---------|
| 0 | `SHADOW_PASS` | Variance shadow map | Per-light shadow maps |
| 1 | `HAIR_SCATTER_PASS` | Compute | Hair scattering LUT |
| 2 | `HAIR_VOXELIZATION_PASS` | Compute | Hair volume density |
| 3 | `FORWARD_PASS` | Rasterization (MSAA) | Main scene rendering |
| 4 | `BLOOM_PASS` | Post-process | Physically-based bloom (Jimenez 2014) |
| 5 | `TONEMAPPIN_PASS` | Post-process | HDR tonemapping |
| 6 | `FXAA_PASS` | Post-process | Optional software AA |

Hair rendering uses the forward path because hair fibers benefit from hardware MSAA — TAA is insufficient for fine strands.

### Adding a New Post-Process Pass

1. **Create the pass class**: Inherit from `PostProcessPass` (or `BasePass` for full control). See `postprocess_pass.h/cpp` for the simplest template — it takes a shader path, binds one input image, and draws a fullscreen quad (`m_vignette`).

2. **Write the shader**: Place in `ext/Vulkan-Engine/resources/shaders/`. Use the unified file format with `#shader fragment` directives. Shaders compile on the fly (no offline compilation needed).

3. **Register in `ForwardRenderer::create_passes()`** (`ext/Vulkan-Engine/src/systems/renderers/forward.cpp`):
   - Add an enum value to `RendererPasses`
   - Resize `m_passes` to accommodate the new slot
   - Instantiate the pass (see tonemapping/FXAA as examples)
   - Set the dependency table via `set_image_dependace_table()` — this links output images from previous passes as inputs

4. **Dependency table format**: `{iVec2(SOURCE_PASS_INDEX, FRAMEBUFFER_INDEX), {ATTACHMENT_IDS...}}` — this tells the engine which framebuffer attachments from a source pass to feed into the new pass via `link_previous_images()`.

5. **Default pass**: The last pass in the chain that renders to the swapchain must have `isDefault = true`. If inserting a pass before the current final pass, update which pass is the default.

### Render Pass Lifecycle (IBasePass)

Each pass implements four virtual setup methods called during `setup()`:
- `setup_attachments()` — declare render targets and subpass dependencies
- `setup_uniforms()` — create descriptor pool, layouts, and allocate descriptor sets
- `setup_shader_passes()` — build graphics/compute pipelines, link shaders
- `render()` — record command buffer for this pass

Resources flow between passes through the dependency table + `link_previous_images()`. There is **no render graph** — passes are manually sequenced.

### Shader System

- **No reflection** — descriptor layouts must be manually defined in C++ pass code
- **Unified files** — vertex + fragment in one `.glsl` file, separated by `#shader fragment`
- **Simple includes** — `#include utils.glsl` (non-recursive, declared in entry-point shader)
- **Include scripts** — reusable modules in `resources/shaders/scripts/` (BRDFs, lighting, camera, etc.)
- **On-the-fly compilation** via Shaderc — no offline `.spv` generation needed

### Key Engine Concepts

- **Resource Manager** (`core/resource_manager.h/cpp`): CPU-to-GPU data upload. Holds shared resources like `VIGNETTE` (fullscreen quad mesh used by all post-process passes).
- **Materials**: `HairEpicMaterial` for hair, `PhysicallyBasedMaterial` for head/eyes. Material classes define their own descriptor layouts and uniform buffers.
- **RHI** (`Graphics/` folder): Low-level Vulkan abstraction (device, swapchain, command buffers, images, descriptors). Should generally remain untouched.
- Uses classic `VkRenderPass` objects, **not** `VK_KHR_dynamic_rendering`.


## Workflow

The workflow for this project will be mainly driven by the user, and Claude will execute the proposed tasks. But Claude will always double-check the orders and ask any inquiries to the user before performing a task. After finishing a task, Claude will summarize the results and ask the user for validation, and automatically update the PLAN.md to mark the task as done, and summarize the issues found and solutions implemented. It should also update CLAUDE.md if necessary.

**Before writing significant code**, Claude should ask the user questions to validate the plan. Don't assume — ask. **Plans should be fool-proof**, so Claude should make the necessary amount of questions to the user before committing to the plan.