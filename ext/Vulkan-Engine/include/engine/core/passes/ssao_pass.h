/*
    This file is part of Vulkan-Engine, a simple to use Vulkan based 3D library

    MIT License

    Copyright (c) 2023 Antonio Espinosa Garcia

*/
#ifndef SSAO_PASS_H
#define SSAO_PASS_H

#include <engine/core/passes/pass.h>
#include <engine/core/textures/textureLDR.h>

VULKAN_ENGINE_NAMESPACE_BEGIN

namespace Core {

/*
SSAO / Thickness Pass.

Reads resolved LinearDepth and view-space Normals from the forward pass and outputs:
  - R: Ambient Occlusion  [0,1]  (1 = fully open, 0 = fully occluded)
  - G: Thickness          [0,1]  (estimated light path length through geometry)

Algorithm: hemisphere sampling with a TBN matrix built from the surface normal and a random
tangent from a small tiled noise texture. The occlusion() function is evaluated twice —
once with the outward normal (AO) and once with the flipped normal (thickness).

No blur pass.
*/
class SSAOPass : public BasePass
{
    static constexpr uint32_t KERNEL_SIZE    = 64;
    static constexpr uint32_t NOISE_TEX_DIM = 4;

    // Must match std140 layout declared in ssao_thickness.glsl
    struct SSAOUniforms {
        Vec4  samples[KERNEL_SIZE]; // vec4 (w=0) — avoids vec3 std140 padding issues
        Mat4  projection;
        Mat4  invProjection;
        Vec2  screenSize;
        float radius;
        float bias;
        int   kernelSize;
        float _pad[3];
    };

    Mesh*                   m_vignette;
    Graphics::DescriptorSet m_descriptorSet;
    Graphics::Buffer        m_ubo;
    Graphics::Image         m_depthImage;
    Graphics::Image         m_normalsImage;
    Core::Texture*          m_noiseTexture = nullptr;

    // CPU-side kernel (generated once, uploaded each frame in render())
    std::vector<Vec4> m_kernel;
    // Raw noise texture data (kept alive until upload completes)
    unsigned char m_noiseData[NOISE_TEX_DIM * NOISE_TEX_DIM * 4];

    float m_radius     = 0.5f;
    float m_bias       = 0.025f;
    int   m_kernelSize = static_cast<int>(KERNEL_SIZE);

    void generate_kernel();
    void generate_noise_texture();

  public:
    SSAOPass(Graphics::Device* ctx, Extent2D extent, Mesh* vignette)
        : BasePass(ctx, extent, 1, 1, false, "SSAO")
        , m_vignette(vignette) {
        generate_kernel();
    }

    inline float get_radius() const {
        return m_radius;
    }
    inline void set_radius(float r) {
        m_radius = r;
    }
    inline float get_bias() const {
        return m_bias;
    }
    inline void set_bias(float b) {
        m_bias = b;
    }
    inline int get_kernel_size() const {
        return m_kernelSize;
    }
    inline void set_kernel_size(int k) {
        m_kernelSize = glm::clamp(k, 1, static_cast<int>(KERNEL_SIZE));
    }

    void setup_attachments(std::vector<Graphics::AttachmentInfo>&    attachments,
                           std::vector<Graphics::SubPassDependency>& dependencies) override;

    void setup_uniforms(std::vector<Graphics::Frame>& frames) override;

    void setup_shader_passes() override;

    void render(Graphics::Frame& currentFrame, Scene* const scene, uint32_t presentImageIndex = 0) override;

    void link_previous_images(std::vector<Graphics::Image> images) override;

    void cleanup() override;
};

} // namespace Core
VULKAN_ENGINE_NAMESPACE_END

#endif
