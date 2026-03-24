#ifndef FORWARD_H
#define FORWARD_H

#include <engine/core/passes/bloom_pass.h>
#include <engine/core/passes/forward_pass.h>
#include <engine/core/passes/hair_scattering_pass.h>
#include <engine/core/passes/hair_voxelization_pass.h>
#include <engine/core/passes/postprocess_pass.h>
#include <engine/core/passes/ssao_pass.h>
#include <engine/core/passes/sss_pass.h>
#include <engine/core/passes/variance_shadow_pass.h>

#include <engine/systems/renderers/renderer.h>

VULKAN_ENGINE_NAMESPACE_BEGIN

namespace Systems {

/*
Renders a given scene data to a given window using forward rendering. Fully parametrizable.
*/
class ForwardRenderer : public BaseRenderer
{

    enum RendererPasses
    {
        SHADOW_PASS            = 0,
        HAIR_SCATTER_PASS      = 1,
        HAIR_VOXELIZATION_PASS = 2,
        FORWARD_PASS           = 3,
        SSAO_PASS              = 4,
        SSS_PASS               = 5,
        BLOOM_PASS             = 6,
        TONEMAPPIN_PASS        = 7,
        FXAA_PASS              = 8,
    };

    ShadowResolution m_shadowQuality = ShadowResolution::MEDIUM;
    bool             m_updateShadows = false;

  public:
    ForwardRenderer(Core::IWindow* window)
        : BaseRenderer(window) {
    }
    ForwardRenderer(Core::IWindow* window, ShadowResolution shadowQuality = ShadowResolution::MEDIUM, RendererSettings settings = {})
        : BaseRenderer(window, settings)
        , m_shadowQuality(shadowQuality) {
    }

    inline ShadowResolution get_shadow_quality() const {
        return m_shadowQuality;
    }
    inline void set_shadow_quality(ShadowResolution quality) {
        m_shadowQuality = quality;
        if (m_initialized)
            m_updateShadows = true;
    }
    inline float get_bloom_strength() const {
        if (m_passes[BLOOM_PASS])
        {
            return static_cast<Core::BloomPass*>(m_passes[BLOOM_PASS])->get_bloom_strength();
        }
        return 0.0f;
    }
    inline void set_bloom_strength(float st) {
        if (m_passes[BLOOM_PASS])
        {
            static_cast<Core::BloomPass*>(m_passes[BLOOM_PASS])->set_bloom_strength(st);
        }
    }

    // SSAO parameters
    inline float get_ssao_radius() const {
        if (m_passes[SSAO_PASS]) return static_cast<Core::SSAOPass*>(m_passes[SSAO_PASS])->get_radius();
        return 0.5f;
    }
    inline void set_ssao_radius(float r) {
        if (m_passes[SSAO_PASS]) static_cast<Core::SSAOPass*>(m_passes[SSAO_PASS])->set_radius(r);
    }
    inline float get_ssao_bias() const {
        if (m_passes[SSAO_PASS]) return static_cast<Core::SSAOPass*>(m_passes[SSAO_PASS])->get_bias();
        return 0.025f;
    }
    inline void set_ssao_bias(float b) {
        if (m_passes[SSAO_PASS]) static_cast<Core::SSAOPass*>(m_passes[SSAO_PASS])->set_bias(b);
    }
    inline int get_ssao_kernel_size() const {
        if (m_passes[SSAO_PASS]) return static_cast<Core::SSAOPass*>(m_passes[SSAO_PASS])->get_kernel_size();
        return 64;
    }
    inline void set_ssao_kernel_size(int k) {
        if (m_passes[SSAO_PASS]) static_cast<Core::SSAOPass*>(m_passes[SSAO_PASS])->set_kernel_size(k);
    }

    // SSS parameters
    inline float get_sss_max_scatter() const {
        if (m_passes[SSS_PASS]) return static_cast<Core::SSSPass*>(m_passes[SSS_PASS])->get_max_scatter();
        return 1.0f;
    }
    inline void set_sss_max_scatter(float s) {
        if (m_passes[SSS_PASS]) static_cast<Core::SSSPass*>(m_passes[SSS_PASS])->set_max_scatter(s);
    }
    inline Vec3 get_sss_scattering_distance() const {
        if (m_passes[SSS_PASS]) return static_cast<Core::SSSPass*>(m_passes[SSS_PASS])->get_scattering_distance();
        return Vec3(0.75f, 0.32f, 0.15f);
    }
    inline void set_sss_scattering_distance(Vec3 d) {
        if (m_passes[SSS_PASS]) static_cast<Core::SSSPass*>(m_passes[SSS_PASS])->set_scattering_distance(d);
    }
    inline float get_sss_extinction_coeff() const {
        if (m_passes[SSS_PASS]) return static_cast<Core::SSSPass*>(m_passes[SSS_PASS])->get_extinction_coeff();
        return 1.0f;
    }
    inline void set_sss_extinction_coeff(float e) {
        if (m_passes[SSS_PASS]) static_cast<Core::SSSPass*>(m_passes[SSS_PASS])->set_extinction_coeff(e);
    }

  protected:
    virtual void on_before_render(Core::Scene* const scene);

    virtual void on_after_render(RenderResult& renderResult, Core::Scene* const scene);

    virtual void create_passes();
};
} // namespace Systems
VULKAN_ENGINE_NAMESPACE_END

#endif