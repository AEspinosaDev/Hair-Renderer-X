#include <engine/core/passes/sss_pass.h>
#include <engine/core/resource_manager.h>
#include <engine/tools/loaders.h>

#include <cmath>
#include <chrono>

#ifdef _WIN32
#include "hair_voxelization_pass.cpp"
#endif
#include <random>

class MonteCarloRandom {
private:
    std::mt19937_64 generator;
    std::uniform_real_distribution<double> distribution;

public:
    MonteCarloRandom(long long seed) : generator(seed), distribution(0.0, 1.0) {}

    double getRandomInRange(double min, double max) {
        return min + (max - min) * distribution(generator);
    }
};

VULKAN_ENGINE_NAMESPACE_BEGIN
using namespace Graphics;
namespace Core {

// ---------------------------------------------------------------------------
// CPU-side helpers
// ---------------------------------------------------------------------------

void SSSPass::generate_samples() {
	// Fibonacci lattice: well-distributed angles + importance-sampled radii
    const float PHI          = 1.6180339887f;
    float randomRange = 1.0 / static_cast<float>(MAX_SAMPLES);
    m_samples.resize(MAX_SAMPLES);

    for (uint32_t i = 0; i < MAX_SAMPLES; i++) {
        // Sample theta in polar coordinates with Fibonacci lattice
        float x = std::fmod((static_cast<float>(i + 1) / PHI), 1.0);
        float theta = 2.0 * M_PI * x;

        // Sample r in polar coordinates with CCDF
    	auto now = std::chrono::system_clock::now();
    	long long timeSeed = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
		MonteCarloRandom monteCarloRandom(timeSeed);
        float y = 1.0 - monteCarloRandom.getRandomInRange(0.0, 1.0);
        
        y = y * randomRange + static_cast<float>(i) * randomRange; // Ordered random
        float Gr = 1.0 + 4.0 * y * (2.0 * y + std::sqrt(1.0 + 4.0 * std::pow(y, 2.0)));
        
        // samples[2 * i + 1] = 3.0 * Math.log(...)
        float term1 = std::pow(Gr, -1.0 / 3.0);
        float term2 = std::pow(Gr, 1.0 / 3.0);
        float r = 3.0 * std::log((1.0 + term1 + term2) / (4.0 * y));

		m_samples[i] = Vec4(theta, r, 0.0f, 0.0f);
    }
}

// ---------------------------------------------------------------------------
// Pass setup
// ---------------------------------------------------------------------------

void SSSPass::setup_attachments(std::vector<Graphics::AttachmentInfo>&    attachments,
                                std::vector<Graphics::SubPassDependency>& dependencies) {
    attachments.resize(2);

    // att 0: scattered HDR — replaces the FORWARD_PASS HDR in the downstream chain
    attachments[0] = Graphics::AttachmentInfo(SRGBA_32F,
                                              1,
                                              LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                              LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                              IMAGE_USAGE_COLOR_ATTACHMENT | IMAGE_USAGE_SAMPLED,
                                              COLOR_ATTACHMENT,
                                              ASPECT_COLOR,
                                              TEXTURE_2D,
                                              FILTER_LINEAR,
                                              ADDRESS_MODE_CLAMP_TO_EDGE);
    attachments[0].isDefault = false;

    // att 1: Bright pass-through — forwarded unmodified to Bloom so Bloom has a single source
    attachments[1] = Graphics::AttachmentInfo(SRGBA_32F,
                                              1,
                                              LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                              LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                              IMAGE_USAGE_COLOR_ATTACHMENT | IMAGE_USAGE_SAMPLED,
                                              COLOR_ATTACHMENT,
                                              ASPECT_COLOR,
                                              TEXTURE_2D,
                                              FILTER_LINEAR,
                                              ADDRESS_MODE_CLAMP_TO_EDGE);
    attachments[1].isDefault = false;

    dependencies.resize(2);

    dependencies[0] = Graphics::SubPassDependency(
        STAGE_FRAGMENT_SHADER, STAGE_COLOR_ATTACHMENT_OUTPUT, ACCESS_COLOR_ATTACHMENT_WRITE);
    dependencies[0].srcAccessMask   = ACCESS_SHADER_READ;
    dependencies[0].dependencyFlags = SUBPASS_DEPENDENCY_NONE;

    dependencies[1] =
        Graphics::SubPassDependency(STAGE_COLOR_ATTACHMENT_OUTPUT, STAGE_FRAGMENT_SHADER, ACCESS_SHADER_READ);
    dependencies[1].srcAccessMask   = ACCESS_COLOR_ATTACHMENT_WRITE;
    dependencies[1].srcSubpass      = 0;
    dependencies[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
    dependencies[1].dependencyFlags = SUBPASS_DEPENDENCY_NONE;
}

void SSSPass::setup_uniforms(std::vector<Graphics::Frame>& frames) {
    // 1 set, 1 UBO, 0 dynamic, 0 storage, 8 combined image samplers (bindings 0-5, 7, 8)
    m_descriptorPool = m_device->create_descriptor_pool(1, 1, 0, 0, 8);

    LayoutBinding hdrBinding       (UNIFORM_COMBINED_IMAGE_SAMPLER, SHADER_STAGE_FRAGMENT, 0);
    LayoutBinding albedoBinding    (UNIFORM_COMBINED_IMAGE_SAMPLER, SHADER_STAGE_FRAGMENT, 1);
    LayoutBinding diffIrrBinding   (UNIFORM_COMBINED_IMAGE_SAMPLER, SHADER_STAGE_FRAGMENT, 2);
    LayoutBinding backIrrBinding   (UNIFORM_COMBINED_IMAGE_SAMPLER, SHADER_STAGE_FRAGMENT, 3);
    LayoutBinding depthBinding     (UNIFORM_COMBINED_IMAGE_SAMPLER, SHADER_STAGE_FRAGMENT, 4);
    LayoutBinding aoBinding        (UNIFORM_COMBINED_IMAGE_SAMPLER, SHADER_STAGE_FRAGMENT, 5);
    LayoutBinding uboBinding       (UNIFORM_BUFFER,                 SHADER_STAGE_FRAGMENT, 6);
    LayoutBinding brightBinding    (UNIFORM_COMBINED_IMAGE_SAMPLER, SHADER_STAGE_FRAGMENT, 7);
    LayoutBinding scatterLutBinding(UNIFORM_COMBINED_IMAGE_SAMPLER, SHADER_STAGE_FRAGMENT, 8);

    m_descriptorPool.set_layout(
        GLOBAL_LAYOUT,
        {hdrBinding, albedoBinding, diffIrrBinding, backIrrBinding, depthBinding, aoBinding,
         uboBinding, brightBinding, scatterLutBinding});
    m_descriptorPool.allocate_descriptor_set(GLOBAL_LAYOUT, &m_descriptorSet);

    m_ubo = m_device->create_buffer(sizeof(SSSUniforms),
                                    BUFFER_USAGE_UNIFORM_BUFFER,
                                    MEMORY_PROPERTY_HOST_VISIBLE | MEMORY_PROPERTY_HOST_COHERENT);

    // Upload static sample positions now; dynamic fields updated each frame in render()
    SSSUniforms data{};
    for (uint32_t i = 0; i < MAX_SAMPLES; i++)
        data.samples[i] = m_samples[i];
    data.sampleCount     = m_sampleCount;
    data.maxScatter      = m_maxScatter;
    data.extinctionCoeff = m_extinctionCoeff;
    data.Fdr             = m_Fdr;
    m_ubo.upload_data(&data, sizeof(SSSUniforms));

    m_descriptorPool.set_descriptor_write(&m_ubo, sizeof(SSSUniforms), 0, &m_descriptorSet, UNIFORM_BUFFER, 6);

    // Binding 8: use fallback texture until load_scatter_lut() is called from the app
    m_descriptorPool.set_descriptor_write(
        get_image(ResourceManager::FALLBACK_TEXTURE), LAYOUT_SHADER_READ_ONLY_OPTIMAL, &m_descriptorSet, 8);
}

void SSSPass::setup_shader_passes() {
    GraphicShaderPass* sssPass = new GraphicShaderPass(
        m_device->get_handle(), m_renderpass, m_imageExtent, ENGINE_RESOURCES_PATH "shaders/misc/ssss.glsl");
    sssPass->settings.descriptorSetLayoutIDs = {{GLOBAL_LAYOUT, true}};
    // Two color attachments: att 0 = scattered HDR, att 1 = bright pass-through
    sssPass->graphicSettings.blendAttachments = {Init::color_blend_attachment_state(false),
                                                 Init::color_blend_attachment_state(false)};
    sssPass->graphicSettings.attributes      = {{POSITION_ATTRIBUTE, true},
                                                {NORMAL_ATTRIBUTE, false},
                                                {UV_ATTRIBUTE, true},
                                                {TANGENT_ATTRIBUTE, false},
                                                {COLOR_ATTRIBUTE, false}};

    sssPass->build_shader_stages();
    sssPass->build(m_descriptorPool);

    m_shaderPasses[0] = sssPass;
}

// ---------------------------------------------------------------------------
// Per-frame render
// ---------------------------------------------------------------------------

void SSSPass::render(Graphics::Frame& currentFrame, Scene* const scene, uint32_t presentImageIndex) {
    Camera* cam = scene->get_active_camera();

    SSSUniforms data{};
    for (uint32_t i = 0; i < MAX_SAMPLES; i++)
        data.samples[i] = m_samples[i];
    // sampleCount == 0 signals passthrough to the shader when SSS is disabled
    data.sampleCount     = (m_sssEnabled != 0) ? m_sampleCount : 0;
    data.maxScatter      = m_maxScatter;
    data.extinctionCoeff = m_extinctionCoeff;
    data.Fdr             = m_Fdr;
    data.screenSize      = Vec2(static_cast<float>(m_imageExtent.width),
                                   static_cast<float>(m_imageExtent.height));
    data.projection    = cam->get_projection();
    data.invProjection = glm::inverse(cam->get_projection());
    m_ubo.upload_data(&data, sizeof(SSSUniforms));

    CommandBuffer cmd = currentFrame.commandBuffer;
    cmd.begin_renderpass(m_renderpass, m_framebuffers[0]);
    cmd.set_viewport(m_imageExtent);

    ShaderPass* shaderPass = m_shaderPasses[0];
    cmd.bind_shaderpass(*shaderPass);
    cmd.bind_descriptor_set(m_descriptorSet, 0, *shaderPass);

    Geometry* g = m_vignette->get_geometry();
    cmd.draw_geometry(*get_VAO(g));

    cmd.end_renderpass(m_renderpass, m_framebuffers[0]);
}

void SSSPass::load_scatter_lut(const std::string& path) {
    if (m_scatterDistLUT)
        ResourceManager::destroy_texture_data(m_scatterDistLUT);

    TextureSettings settings{};
    settings.useMipmaps = false;
    settings.adressMode = ADDRESS_MODE_CLAMP_TO_EDGE;
    settings.filter     = FILTER_LINEAR;
    settings.format     = RGBA_8U;

    m_scatterDistLUT = new Core::Texture(settings);
    Tools::Loaders::load_texture(m_scatterDistLUT, path, TEXTURE_FORMAT_TYPE_NORMAL, false); // sync, UNORM so shader can linearize manually
    ResourceManager::upload_texture_data(m_device, m_scatterDistLUT);

    m_descriptorPool.set_descriptor_write(
        get_image(m_scatterDistLUT), LAYOUT_SHADER_READ_ONLY_OPTIMAL, &m_descriptorSet, 8);
}

void SSSPass::link_previous_images(std::vector<Graphics::Image> images) {
    // Order matches the dependency table in ForwardRenderer::create_passes():
    //   [0] HDR color          (forward att 7 / 0)   → shader binding 0
    //   [1] Albedo + mask      (forward att 10 / 3)  → shader binding 1
    //   [2] Diffuse irradiance (forward att 11 / 4)  → shader binding 2
    //   [3] Back irradiance    (forward att 12 / 5)  → shader binding 3
    //   [4] Linear depth       (forward att 13 / 6)  → shader binding 4
    //   [5] Bright             (forward att 8 / 1)   → shader binding 7 (pass-through to att 1 output)
    //   [6] AO + thickness     (SSAO pass att 0)     → shader binding 5
    m_hdrImage         = images[0];
    m_albedoMaskImage  = images[1];
    m_diffuseIrrImage  = images[2];
    m_backIrrImage     = images[3];
    m_depthImage       = images[4];
    m_brightImage      = images[5];
    m_aoThicknessImage = images[6];

    m_descriptorPool.set_descriptor_write(&m_hdrImage,         LAYOUT_SHADER_READ_ONLY_OPTIMAL, &m_descriptorSet, 0);
    m_descriptorPool.set_descriptor_write(&m_albedoMaskImage,  LAYOUT_SHADER_READ_ONLY_OPTIMAL, &m_descriptorSet, 1);
    m_descriptorPool.set_descriptor_write(&m_diffuseIrrImage,  LAYOUT_SHADER_READ_ONLY_OPTIMAL, &m_descriptorSet, 2);
    m_descriptorPool.set_descriptor_write(&m_backIrrImage,     LAYOUT_SHADER_READ_ONLY_OPTIMAL, &m_descriptorSet, 3);
    m_descriptorPool.set_descriptor_write(&m_depthImage,       LAYOUT_SHADER_READ_ONLY_OPTIMAL, &m_descriptorSet, 4);
    m_descriptorPool.set_descriptor_write(&m_aoThicknessImage, LAYOUT_SHADER_READ_ONLY_OPTIMAL, &m_descriptorSet, 5);
    m_descriptorPool.set_descriptor_write(&m_brightImage,      LAYOUT_SHADER_READ_ONLY_OPTIMAL, &m_descriptorSet, 7);
}

void SSSPass::cleanup() {
    if (m_scatterDistLUT)
        ResourceManager::destroy_texture_data(m_scatterDistLUT);
    m_ubo.cleanup();
    BasePass::cleanup();
}

} // namespace Core
VULKAN_ENGINE_NAMESPACE_END
