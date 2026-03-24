#include <engine/core/passes/ssao_pass.h>
#include <engine/core/resource_manager.h>

#include <random>

VULKAN_ENGINE_NAMESPACE_BEGIN
using namespace Graphics;
namespace Core {

// ---------------------------------------------------------------------------
// CPU-side helpers
// ---------------------------------------------------------------------------

void SSAOPass::generate_kernel() {
    std::uniform_real_distribution<float> rnd(0.0f, 1.0f);
    std::default_random_engine            eng;

    m_kernel.resize(KERNEL_SIZE);
    for (uint32_t i = 0; i < KERNEL_SIZE; ++i)
    {
        Vec3 sample(rnd(eng) * 2.0f - 1.0f, rnd(eng) * 2.0f - 1.0f, rnd(eng));
        sample = glm::normalize(sample);
        sample *= rnd(eng);

        // Accelerating interpolation — denser samples near origin
        float scale = static_cast<float>(i) / static_cast<float>(KERNEL_SIZE);
        scale       = glm::mix(0.1f, 1.0f, scale * scale);
        sample *= scale;

        m_kernel[i] = Vec4(sample, 0.0f);
    }
}

void SSAOPass::generate_noise_texture() {
    std::uniform_int_distribution<int> rnd(0, 255);
    std::default_random_engine         eng(42); // fixed seed for reproducibility

    for (uint32_t i = 0; i < NOISE_TEX_DIM * NOISE_TEX_DIM; ++i)
    {
        m_noiseData[i * 4 + 0] = static_cast<unsigned char>(rnd(eng)); // R — random tangent X
        m_noiseData[i * 4 + 1] = static_cast<unsigned char>(rnd(eng)); // G — random tangent Y
        m_noiseData[i * 4 + 2] = 0;                                    // B — unused
        m_noiseData[i * 4 + 3] = 255;                                  // A — unused
    }

    TextureSettings settings{};
    settings.useMipmaps = false;
    settings.adressMode = ADDRESS_MODE_REPEAT;
    settings.filter     = FILTER_NEAREST;
    settings.format     = RGBA_8U;

    m_noiseTexture = new Core::Texture(m_noiseData, {NOISE_TEX_DIM, NOISE_TEX_DIM, 1}, 4, settings);
    ResourceManager::upload_texture_data(m_device, m_noiseTexture);
}

// ---------------------------------------------------------------------------
// Pass setup
// ---------------------------------------------------------------------------

void SSAOPass::setup_attachments(std::vector<Graphics::AttachmentInfo>&    attachments,
                                 std::vector<Graphics::SubPassDependency>& dependencies) {
    attachments.resize(1);

    // R = AO, G = thickness  — RGBA16F so both channels are written cleanly
    attachments[0] = Graphics::AttachmentInfo(SRGBA_16F,
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

void SSAOPass::setup_uniforms(std::vector<Graphics::Frame>& frames) {
    // Pool: 1 set, 1 regular UBO, 0 dynamic, 0 storage, 3 combined image samplers
    m_descriptorPool = m_device->create_descriptor_pool(1, 1, 0, 0, 3);

    LayoutBinding depthBinding(UNIFORM_COMBINED_IMAGE_SAMPLER, SHADER_STAGE_FRAGMENT, 0);
    LayoutBinding normalsBinding(UNIFORM_COMBINED_IMAGE_SAMPLER, SHADER_STAGE_FRAGMENT, 1);
    LayoutBinding noiseBinding(UNIFORM_COMBINED_IMAGE_SAMPLER, SHADER_STAGE_FRAGMENT, 2);
    LayoutBinding uboBinding(UNIFORM_BUFFER, SHADER_STAGE_FRAGMENT, 3);

    m_descriptorPool.set_layout(GLOBAL_LAYOUT, {depthBinding, normalsBinding, noiseBinding, uboBinding});
    m_descriptorPool.allocate_descriptor_set(GLOBAL_LAYOUT, &m_descriptorSet);

    // Create host-visible UBO
    m_ubo = m_device->create_buffer(sizeof(SSAOUniforms),
                                    BUFFER_USAGE_UNIFORM_BUFFER,
                                    MEMORY_PROPERTY_HOST_VISIBLE | MEMORY_PROPERTY_HOST_COHERENT);

    // Upload static kernel data now so it is ready at first render
    SSAOUniforms data{};
    for (uint32_t i = 0; i < KERNEL_SIZE; ++i)
        data.samples[i] = m_kernel[i];
    data.radius     = m_radius;
    data.bias       = m_bias;
    data.kernelSize = m_kernelSize;
    m_ubo.upload_data(&data, sizeof(SSAOUniforms));

    m_descriptorPool.set_descriptor_write(&m_ubo, sizeof(SSAOUniforms), 0, &m_descriptorSet, UNIFORM_BUFFER, 3);

    // Noise texture (depth + normals written later in link_previous_images)
    generate_noise_texture();
    m_descriptorPool.set_descriptor_write(
        get_image(m_noiseTexture), LAYOUT_SHADER_READ_ONLY_OPTIMAL, &m_descriptorSet, 2);
}

void SSAOPass::setup_shader_passes() {
    GraphicShaderPass* ssaoPass = new GraphicShaderPass(
        m_device->get_handle(), m_renderpass, m_imageExtent, ENGINE_RESOURCES_PATH "shaders/misc/ssao_thickness.glsl");
    ssaoPass->settings.descriptorSetLayoutIDs = {{GLOBAL_LAYOUT, true}};
    ssaoPass->graphicSettings.attributes      = {{POSITION_ATTRIBUTE, true},
                                                 {NORMAL_ATTRIBUTE, false},
                                                 {UV_ATTRIBUTE, true},
                                                 {TANGENT_ATTRIBUTE, false},
                                                 {COLOR_ATTRIBUTE, false}};

    ssaoPass->build_shader_stages();
    ssaoPass->build(m_descriptorPool);

    m_shaderPasses[0] = ssaoPass;
}

// ---------------------------------------------------------------------------
// Per-frame render
// ---------------------------------------------------------------------------

void SSAOPass::render(Graphics::Frame& currentFrame, Scene* const scene, uint32_t presentImageIndex) {
    // Update dynamic UBO fields: projection, invProjection, screenSize, radius, bias, kernelSize
    Camera* cam = scene->get_active_camera();

    SSAOUniforms data{};
    for (uint32_t i = 0; i < KERNEL_SIZE; ++i)
        data.samples[i] = m_kernel[i];
    data.projection    = cam->get_projection();
    data.invProjection = glm::inverse(cam->get_projection());
    data.screenSize    = Vec2(static_cast<float>(m_imageExtent.width), static_cast<float>(m_imageExtent.height));
    data.radius        = m_radius;
    data.bias          = m_bias;
    data.kernelSize    = m_kernelSize;
    m_ubo.upload_data(&data, sizeof(SSAOUniforms));

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

void SSAOPass::link_previous_images(std::vector<Graphics::Image> images) {
    // images[0] = LinearDepth, images[1] = Normals  (order matches dependency table)
    m_depthImage   = images[0];
    m_normalsImage = images[1];

    m_descriptorPool.set_descriptor_write(&m_depthImage, LAYOUT_SHADER_READ_ONLY_OPTIMAL, &m_descriptorSet, 0);
    m_descriptorPool.set_descriptor_write(&m_normalsImage, LAYOUT_SHADER_READ_ONLY_OPTIMAL, &m_descriptorSet, 1);
}

void SSAOPass::cleanup() {
    m_ubo.cleanup();

    if (m_noiseTexture)
    {
        ResourceManager::destroy_texture_data(m_noiseTexture);
        delete m_noiseTexture;
        m_noiseTexture = nullptr;
    }

    BasePass::cleanup();
}

} // namespace Core
VULKAN_ENGINE_NAMESPACE_END
