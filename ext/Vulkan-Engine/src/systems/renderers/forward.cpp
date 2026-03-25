#include <engine/systems/renderers/forward.h>

VULKAN_ENGINE_NAMESPACE_BEGIN
namespace Systems {

void ForwardRenderer::on_before_render(Core::Scene* const scene) {
    BaseRenderer::on_before_render(scene);

    if (scene->get_skybox())
    {
        if (scene->get_skybox()->update_enviroment())
            static_cast<Core::ForwardPass*>(m_passes[FORWARD_PASS])
                ->set_envmap_descriptor(Core::ResourceManager::panoramaConverterPass->get_framebuffers()[0].attachmentImages[0],
                                        Core::ResourceManager::irradianceComputePass->get_framebuffers()[0].attachmentImages[0]);
    }

    m_passes[FORWARD_PASS]->set_attachment_clear_value({m_settings.clearColor.r, m_settings.clearColor.g, m_settings.clearColor.b, m_settings.clearColor.a}, 0);
    m_passes[FORWARD_PASS]->set_attachment_clear_value({m_settings.clearColor.r, m_settings.clearColor.g, m_settings.clearColor.b, m_settings.clearColor.a}, 1);
}

void ForwardRenderer::on_after_render(RenderResult& renderResult, Core::Scene* const scene) {
    BaseRenderer::on_after_render(renderResult, scene);

    if (m_updateShadows)
    {
        m_device->wait();

        const uint32_t SHADOW_RES = (uint32_t)m_shadowQuality;

        m_passes[SHADOW_PASS]->set_extent({SHADOW_RES, SHADOW_RES});
        m_passes[SHADOW_PASS]->update();

        m_updateShadows = false;

        connect_pass(m_passes[FORWARD_PASS]);
    }
}
void ForwardRenderer::create_passes() {
    const uint32_t SHADOW_RES          = (uint32_t)m_shadowQuality;
    const uint32_t totalImagesInFlight = (uint32_t)m_settings.bufferingType + 1;

    const bool msaa = m_settings.samplesMSAA > MSAASamples::x1;

    m_passes.resize(9, nullptr);
    // Shadow Pass
    m_passes[SHADOW_PASS] = new Core::VarianceShadowPass(m_device, {SHADOW_RES, SHADOW_RES}, ENGINE_MAX_LIGHTS, m_settings.depthFormat);

    // Hair Related Passes
    m_passes[HAIR_SCATTER_PASS] = new Core::HairScatteringPass(m_device, 128);
    m_passes[HAIR_VOXELIZATION_PASS] = new Core::HairVoxelizationPass(m_device, 256);
    //  m_passes[HAIR_VOXELIZATION_PASS] ->set_active(false);

    // Forward Pass
    m_passes[FORWARD_PASS] = new Core::ForwardPass(m_device, m_window->get_extent(), SRGBA_32F, m_settings.depthFormat, m_settings.samplesMSAA, false);
    m_passes[FORWARD_PASS]->set_image_dependace_table({{iVec2(SHADOW_PASS, 0), {0}}});

    // SSAO / Thickness Pass — reads LinearDepth and Normals from FORWARD_PASS
    // MSAA layout: att 13 = LinearDepth resolve, att 9 = Normals resolve
    // Non-MSAA:    att  6 = LinearDepth,          att 2 = Normals
    m_passes[SSAO_PASS] = new Core::SSAOPass(m_device, m_window->get_extent(), Core::ResourceManager::VIGNETTE);
    m_passes[SSAO_PASS]->set_image_dependace_table(
        {{iVec2(FORWARD_PASS, 0), {msaa ? 13u : 6u, msaa ? 9u : 2u}}});

    // SSS Pass — reads HDR/AlbedoMask/DiffIrr/BackIrr/Depth/Bright from FORWARD_PASS
    //            and AO+Thickness from SSAO_PASS
    // link_previous_images order: [0]=HDR [1]=AlbedoMask [2]=DiffIrr [3]=BackIrr
    //                             [4]=Depth [5]=Bright [6]=AO  (FORWARD_PASS first, lower index)
    m_passes[SSS_PASS] = new Core::SSSPass(m_device, m_window->get_extent(), Core::ResourceManager::VIGNETTE);
    m_passes[SSS_PASS]->set_image_dependace_table(
        {{iVec2(FORWARD_PASS, 0), {msaa ? 7u : 0u, msaa ? 10u : 3u, msaa ? 11u : 4u,
                                   msaa ? 12u : 5u, msaa ? 13u : 6u, msaa ? 8u  : 1u}},
         {iVec2(SSAO_PASS, 0),    {0u}}});

    // Bloom Pass — reads scattered HDR (att 0) and Bright pass-through (att 1) from SSS_PASS
    m_passes[BLOOM_PASS] = new Core::BloomPass(m_device, m_window->get_extent(), Core::ResourceManager::VIGNETTE);
    m_passes[BLOOM_PASS]->set_image_dependace_table({{iVec2(SSS_PASS, 0), {0u, 1u}}});

    // Use the actual swapchain format for default (swapchain-backed) passes.
    // The driver may select a different format (e.g. B8G8R8A8_UNORM) when the
    // requested colorFormat (R8G8B8A8_SRGB) is not available.
    const ColorFormatType presentFormat =
        static_cast<ColorFormatType>(m_device->get_swapchain().get_image_format());

    // Tonemapping
    m_passes[TONEMAPPIN_PASS] = new Core::PostProcessPass(m_device,
                                                          m_window->get_extent(),
                                                          presentFormat,
                                                          Core::ResourceManager::VIGNETTE,
                                                          ENGINE_RESOURCES_PATH "shaders/misc/tonemapping.glsl",
                                                          "TONEMAPPING",
                                                          m_settings.softwareAA ? false : true);
    m_passes[TONEMAPPIN_PASS]->set_image_dependace_table({{iVec2(BLOOM_PASS, 0), {0}}});

    // FXAA Pass
    m_passes[FXAA_PASS] = new Core::PostProcessPass(m_device,
                                                    m_window->get_extent(),
                                                    presentFormat,
                                                    Core::ResourceManager::VIGNETTE,
                                                    ENGINE_RESOURCES_PATH "shaders/aa/fxaa.glsl",
                                                    "FXAA",
                                                    m_settings.softwareAA);
    m_passes[FXAA_PASS]->set_image_dependace_table({{iVec2(TONEMAPPIN_PASS, 0), {0}}});
    if (!m_settings.softwareAA)
        m_passes[FXAA_PASS]->set_active(false);
}

} // namespace Systems
VULKAN_ENGINE_NAMESPACE_END