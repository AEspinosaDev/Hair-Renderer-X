/*
    This file is part of Vulkan-Engine, a simple to use Vulkan based 3D library

    MIT License

    Copyright (c) 2023 Antonio Espinosa Garcia

*/
#ifndef SSS_PASS_H
#define SSS_PASS_H

#include <engine/core/passes/pass.h>
#include <engine/core/textures/textureLDR.h>

VULKAN_ENGINE_NAMESPACE_BEGIN

namespace Core {

/*
Screen-Space Subsurface Scattering Pass.

Implements two scattering components:

  Multiple scattering (diffusion):
    Disk-based kernel with Burley normalized diffusion profile.
    Samples the diffuse irradiance MRT from the forward pass and weights by
    the per-channel Burley R(r) profile. CPU-side samples are importance-sampled
    via the radial CDF inverse so equal weights can be used in the shader.

  Single scattering (translucency):
    Beer-Lambert transmittance T = exp(-thickness * extinctionCoeff) applied to
    back-facing irradiance from the forward pass MRT, modulated by (1 - Fdr).

Pixels with scatterMask == 0 (hair, unlit, sky) pass through the original HDR
color unchanged.

Inputs (via link_previous_images, in order):
  [0] HDR color         (forward att 7 / 0)
  [1] Albedo + mask     (forward att 10 / 3)   A = scatterMask
  [2] Diffuse irr       (forward att 11 / 4)
  [3] Back irr          (forward att 12 / 5)
  [4] Linear depth      (forward att 13 / 6)
  [5] AO + thickness    (SSAO pass att 0)       R = AO, G = thickness

Output: single SRGBA_32F attachment (scattered HDR color, fed to Bloom next).
*/
class SSSPass : public BasePass
{
    static constexpr uint32_t MAX_SAMPLES = 64;

    // Must match std140 layout declared in ssss.glsl (binding 6)
    struct SSSUniforms {
        Vec4  samples[MAX_SAMPLES]; // xy = (theta, r), zw = unused (vec4 for std140 array padding)
        int   sampleCount;
        float maxScatter;           // world-space scatter radius scale
        float extinctionCoeff;      // Beer-Lambert extinction coefficient
        float Fdr;                  // internal Fresnel diffuse reflectance
        Vec2  screenSize;
        Vec2  _pad0;                // align projection to 16-byte boundary at offset 1056
        Mat4  projection;
        Mat4  invProjection;
    };

    Mesh*                   m_vignette;
    Graphics::DescriptorSet m_descriptorSet;
    Graphics::Buffer        m_ubo;

    Graphics::Image m_hdrImage;
    Graphics::Image m_albedoMaskImage;
    Graphics::Image m_diffuseIrrImage;
    Graphics::Image m_backIrrImage;
    Graphics::Image m_depthImage;
    Graphics::Image m_brightImage;      // pass-through to Bloom (att 1 output)
    Graphics::Image m_aoThicknessImage;

    Core::Texture* m_scatterDistLUT = nullptr; // 5-pixel thin→thick LUT (binding 8)

    std::vector<Vec4> m_samples; // CPU-side samples, uploaded to UBO

    // Parameters
    int   m_sampleCount     = static_cast<int>(MAX_SAMPLES);
    float m_maxScatter      = 0.05f;
    float m_extinctionCoeff = 3.0f;
    float m_Fdr             = 0.028f;
    int   m_sssEnabled      = 1; // separate from m_enabled; controls UBO flag

    void        generate_samples();
    static float burley_cdf_inverse(float u);

  public:
    SSSPass(Graphics::Device* ctx, Extent2D extent, Mesh* vignette)
        : BasePass(ctx, extent, 1, 1, false, "SSS")
        , m_vignette(vignette) {
        generate_samples();
    }

    // Always render (pass-through when disabled) so the downstream chain keeps running
    inline void set_active(const bool s) override { m_sssEnabled = s ? 1 : 0; }
    inline bool is_sss_enabled() const { return m_sssEnabled != 0; }

    inline int   get_sample_count() const { return m_sampleCount; }
    inline void  set_sample_count(int n) { m_sampleCount = glm::clamp(n, 1, static_cast<int>(MAX_SAMPLES)); }
    inline float get_max_scatter() const { return m_maxScatter; }
    inline void  set_max_scatter(float s) { m_maxScatter = s; }
    inline float get_extinction_coeff() const { return m_extinctionCoeff; }
    inline void  set_extinction_coeff(float e) { m_extinctionCoeff = e; }
    inline float get_Fdr() const { return m_Fdr; }
    inline void  set_Fdr(float f) { m_Fdr = f; }

    void load_scatter_lut(const std::string& path);

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
