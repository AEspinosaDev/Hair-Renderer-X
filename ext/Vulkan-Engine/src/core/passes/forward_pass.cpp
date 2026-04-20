#include <engine/core/materials/hair.h>
#include <engine/core/passes/forward_pass.h>

#define FAST_HAIR_GEOMETRY 0

VULKAN_ENGINE_NAMESPACE_BEGIN
using namespace Graphics;
namespace Core {

void ForwardPass::setup_attachments(std::vector<Graphics::AttachmentInfo>& attachments, std::vector<Graphics::SubPassDependency>& dependencies) {

    uint16_t samples      = static_cast<uint16_t>(m_aa);
    bool     multisampled = samples > 1;

    // 7 color attachments + 7 resolve attachments (MSAA only). Depth pushed at end.
    // Color layout : [0] HDR, [1] Bright, [2] Normals, [3] AlbedoMask, [4] DiffuseIrr, [5] BackIrr, [6] LinearDepth
    // Resolve layout (MSAA): [7..13] mirror [0..6]
    attachments.resize(multisampled ? 14 : 7);

    // [0] HDR color
    attachments[0] = Graphics::AttachmentInfo(
        m_colorFormat,
        samples,
        m_isDefault ? (multisampled ? LAYOUT_COLOR_ATTACHMENT_OPTIMAL : LAYOUT_PRESENT) : LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        !m_isDefault ? IMAGE_USAGE_COLOR_ATTACHMENT | IMAGE_USAGE_SAMPLED : IMAGE_USAGE_TRANSIENT_ATTACHMENT | IMAGE_USAGE_COLOR_ATTACHMENT,
        COLOR_ATTACHMENT,
        ASPECT_COLOR,
        TEXTURE_2D,
        FILTER_LINEAR,
        ADDRESS_MODE_CLAMP_TO_EDGE);
    attachments[0].isDefault = m_isDefault ? (multisampled ? false : true) : false;

    // [1] Bright color buffer (for bloom)
    attachments[1] = Graphics::AttachmentInfo(m_colorFormat, samples,
        LAYOUT_SHADER_READ_ONLY_OPTIMAL, LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        IMAGE_USAGE_COLOR_ATTACHMENT | IMAGE_USAGE_SAMPLED,
        COLOR_ATTACHMENT, ASPECT_COLOR, TEXTURE_2D, FILTER_LINEAR, ADDRESS_MODE_CLAMP_TO_EDGE);

    // [2] View-space normals (RGB)
    attachments[2] = Graphics::AttachmentInfo(SRGBA_16F, samples,
        LAYOUT_SHADER_READ_ONLY_OPTIMAL, LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        IMAGE_USAGE_COLOR_ATTACHMENT | IMAGE_USAGE_SAMPLED,
        COLOR_ATTACHMENT, ASPECT_COLOR, TEXTURE_2D, FILTER_LINEAR, ADDRESS_MODE_CLAMP_TO_EDGE);

    // [3] Albedo (RGB) + scatterMask (A). Clear alpha=0 so undrawn pixels skip SSS.
    attachments[3] = Graphics::AttachmentInfo(SRGBA_16F, samples,
        LAYOUT_SHADER_READ_ONLY_OPTIMAL, LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        IMAGE_USAGE_COLOR_ATTACHMENT | IMAGE_USAGE_SAMPLED,
        COLOR_ATTACHMENT, ASPECT_COLOR, TEXTURE_2D, FILTER_LINEAR, ADDRESS_MODE_CLAMP_TO_EDGE,
        {{{0.0f, 0.0f, 0.0f, 0.0f}}});

    // [4] Diffuse irradiance (front-facing, no albedo multiply, no specular)
    attachments[4] = Graphics::AttachmentInfo(SRGBA_16F, samples,
        LAYOUT_SHADER_READ_ONLY_OPTIMAL, LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        IMAGE_USAGE_COLOR_ATTACHMENT | IMAGE_USAGE_SAMPLED,
        COLOR_ATTACHMENT, ASPECT_COLOR, TEXTURE_2D, FILTER_LINEAR, ADDRESS_MODE_CLAMP_TO_EDGE);

    // [5] Back irradiance (lighting with flipped normal, for single-scatter translucency)
    attachments[5] = Graphics::AttachmentInfo(SRGBA_16F, samples,
        LAYOUT_SHADER_READ_ONLY_OPTIMAL, LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        IMAGE_USAGE_COLOR_ATTACHMENT | IMAGE_USAGE_SAMPLED,
        COLOR_ATTACHMENT, ASPECT_COLOR, TEXTURE_2D, FILTER_LINEAR, ADDRESS_MODE_CLAMP_TO_EDGE);

    // [6] Linear depth copy (gl_FragCoord.z). Same sample count as other attachments; resolved to single-sample for post-process.
    attachments[6] = Graphics::AttachmentInfo(SR_32F, samples,
        LAYOUT_SHADER_READ_ONLY_OPTIMAL, LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        IMAGE_USAGE_COLOR_ATTACHMENT | IMAGE_USAGE_SAMPLED,
        COLOR_ATTACHMENT, ASPECT_COLOR, TEXTURE_2D, FILTER_NEAREST, ADDRESS_MODE_CLAMP_TO_EDGE,
        {{{1.0f, 0.0f, 0.0f, 0.0f}}});

    if (multisampled)
    {
        // [7] Resolve HDR
        attachments[7] = Graphics::AttachmentInfo(
            m_colorFormat, 1,
            m_isDefault ? LAYOUT_PRESENT : LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            !m_isDefault ? IMAGE_USAGE_COLOR_ATTACHMENT | IMAGE_USAGE_SAMPLED : IMAGE_USAGE_TRANSIENT_ATTACHMENT | IMAGE_USAGE_COLOR_ATTACHMENT,
            RESOLVE_ATTACHMENT, ASPECT_COLOR, TEXTURE_2D);
        attachments[7].isDefault = m_isDefault ? true : false;

        // [8] Resolve Bright
        attachments[8] = Graphics::AttachmentInfo(m_colorFormat, 1,
            LAYOUT_SHADER_READ_ONLY_OPTIMAL, LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            IMAGE_USAGE_COLOR_ATTACHMENT | IMAGE_USAGE_SAMPLED,
            RESOLVE_ATTACHMENT, ASPECT_COLOR, TEXTURE_2D, FILTER_LINEAR, ADDRESS_MODE_CLAMP_TO_EDGE);

        // [9] Resolve Normals
        attachments[9] = Graphics::AttachmentInfo(SRGBA_16F, 1,
            LAYOUT_SHADER_READ_ONLY_OPTIMAL, LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            IMAGE_USAGE_COLOR_ATTACHMENT | IMAGE_USAGE_SAMPLED,
            RESOLVE_ATTACHMENT, ASPECT_COLOR, TEXTURE_2D, FILTER_LINEAR, ADDRESS_MODE_CLAMP_TO_EDGE);

        // [10] Resolve AlbedoMask
        attachments[10] = Graphics::AttachmentInfo(SRGBA_16F, 1,
            LAYOUT_SHADER_READ_ONLY_OPTIMAL, LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            IMAGE_USAGE_COLOR_ATTACHMENT | IMAGE_USAGE_SAMPLED,
            RESOLVE_ATTACHMENT, ASPECT_COLOR, TEXTURE_2D, FILTER_LINEAR, ADDRESS_MODE_CLAMP_TO_EDGE,
            {{{0.0f, 0.0f, 0.0f, 0.0f}}});

        // [11] Resolve DiffuseIrradiance
        attachments[11] = Graphics::AttachmentInfo(SRGBA_16F, 1,
            LAYOUT_SHADER_READ_ONLY_OPTIMAL, LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            IMAGE_USAGE_COLOR_ATTACHMENT | IMAGE_USAGE_SAMPLED,
            RESOLVE_ATTACHMENT, ASPECT_COLOR, TEXTURE_2D, FILTER_LINEAR, ADDRESS_MODE_CLAMP_TO_EDGE);

        // [12] Resolve BackIrradiance
        attachments[12] = Graphics::AttachmentInfo(SRGBA_16F, 1,
            LAYOUT_SHADER_READ_ONLY_OPTIMAL, LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            IMAGE_USAGE_COLOR_ATTACHMENT | IMAGE_USAGE_SAMPLED,
            RESOLVE_ATTACHMENT, ASPECT_COLOR, TEXTURE_2D, FILTER_LINEAR, ADDRESS_MODE_CLAMP_TO_EDGE);

        // [13] Resolve LinearDepth
        attachments[13] = Graphics::AttachmentInfo(SR_32F, 1,
            LAYOUT_SHADER_READ_ONLY_OPTIMAL, LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            IMAGE_USAGE_COLOR_ATTACHMENT | IMAGE_USAGE_SAMPLED,
            RESOLVE_ATTACHMENT, ASPECT_COLOR, TEXTURE_2D, FILTER_NEAREST, ADDRESS_MODE_CLAMP_TO_EDGE,
            {{{1.0f, 0.0f, 0.0f, 0.0f}}});
    }

    Graphics::AttachmentInfo depthAttachment = Graphics::AttachmentInfo(m_depthFormat,
                                                                        samples,
                                                                        LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                                                        LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                                                        IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT,
                                                                        DEPTH_ATTACHMENT,
                                                                        ASPECT_DEPTH,
                                                                        TEXTURE_2D);
    attachments.push_back(depthAttachment);

    // Dependencies
    dependencies.resize(2);
    dependencies[0] = Graphics::SubPassDependency(STAGE_COLOR_ATTACHMENT_OUTPUT, STAGE_COLOR_ATTACHMENT_OUTPUT, ACCESS_COLOR_ATTACHMENT_WRITE);
    dependencies[1] = Graphics::SubPassDependency(STAGE_EARLY_FRAGMENT_TESTS, STAGE_EARLY_FRAGMENT_TESTS, ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE);
}
void ForwardPass::setup_uniforms(std::vector<Graphics::Frame>& frames) {

    m_descriptorPool = m_device->create_descriptor_pool(ENGINE_MAX_OBJECTS, ENGINE_MAX_OBJECTS, ENGINE_MAX_OBJECTS, ENGINE_MAX_OBJECTS * 3, ENGINE_MAX_OBJECTS);
    m_descriptors.resize(frames.size());

    // GLOBAL SET
    LayoutBinding camBufferBinding(UNIFORM_DYNAMIC_BUFFER, SHADER_STAGE_VERTEX | SHADER_STAGE_GEOMETRY | SHADER_STAGE_FRAGMENT, 0);
    LayoutBinding sceneBufferBinding(UNIFORM_DYNAMIC_BUFFER, SHADER_STAGE_VERTEX | SHADER_STAGE_GEOMETRY | SHADER_STAGE_FRAGMENT, 1);
    LayoutBinding shadowBinding(UNIFORM_COMBINED_IMAGE_SAMPLER, SHADER_STAGE_FRAGMENT, 2);
    LayoutBinding envBinding(UNIFORM_COMBINED_IMAGE_SAMPLER, SHADER_STAGE_FRAGMENT, 3);
    LayoutBinding iblBinding(UNIFORM_COMBINED_IMAGE_SAMPLER, SHADER_STAGE_FRAGMENT, 4);
    LayoutBinding accelBinding(UNIFORM_ACCELERATION_STRUCTURE, SHADER_STAGE_FRAGMENT, 5);
    LayoutBinding noiseBinding(UNIFORM_COMBINED_IMAGE_SAMPLER, SHADER_STAGE_FRAGMENT, 6);
    LayoutBinding DpBinding(UNIFORM_COMBINED_IMAGE_SAMPLER, SHADER_STAGE_FRAGMENT, 7);
    LayoutBinding hairVoxels(UNIFORM_COMBINED_IMAGE_SAMPLER, SHADER_STAGE_FRAGMENT, 9);
    LayoutBinding hairng(UNIFORM_COMBINED_IMAGE_SAMPLER, SHADER_STAGE_FRAGMENT, 10);
    LayoutBinding hairngt(UNIFORM_COMBINED_IMAGE_SAMPLER, SHADER_STAGE_FRAGMENT, 11);
    LayoutBinding hairGI(UNIFORM_COMBINED_IMAGE_SAMPLER, SHADER_STAGE_FRAGMENT, 12);
    LayoutBinding hairVoxelsDensity(UNIFORM_COMBINED_IMAGE_SAMPLER, SHADER_STAGE_FRAGMENT, 13);
    m_descriptorPool.set_layout(GLOBAL_LAYOUT,
                                {camBufferBinding,
                                 sceneBufferBinding,
                                 shadowBinding,
                                 envBinding,
                                 iblBinding,
                                 accelBinding,
                                 noiseBinding,
                                 DpBinding,
                                 hairVoxels,
                                 hairng,
                                 hairngt,
                                 hairGI,
                                 hairVoxelsDensity});

    // PER-OBJECT SET
    LayoutBinding objectBufferBinding(UNIFORM_DYNAMIC_BUFFER, SHADER_STAGE_VERTEX | SHADER_STAGE_GEOMETRY | SHADER_STAGE_FRAGMENT, 0);
    LayoutBinding materialBufferBinding(UNIFORM_DYNAMIC_BUFFER, SHADER_STAGE_VERTEX | SHADER_STAGE_GEOMETRY | SHADER_STAGE_FRAGMENT, 1);
    m_descriptorPool.set_layout(OBJECT_LAYOUT, {objectBufferBinding, materialBufferBinding});

    // MATERIAL TEXTURE SET
    LayoutBinding textureBinding1(UNIFORM_COMBINED_IMAGE_SAMPLER, SHADER_STAGE_FRAGMENT, 0);
    LayoutBinding textureBinding2(UNIFORM_COMBINED_IMAGE_SAMPLER, SHADER_STAGE_FRAGMENT, 1);
    LayoutBinding textureBinding3(UNIFORM_COMBINED_IMAGE_SAMPLER, SHADER_STAGE_FRAGMENT, 2);
    LayoutBinding textureBinding4(UNIFORM_COMBINED_IMAGE_SAMPLER, SHADER_STAGE_FRAGMENT, 3);
    LayoutBinding textureBinding5(UNIFORM_COMBINED_IMAGE_SAMPLER, SHADER_STAGE_FRAGMENT, 4);
    LayoutBinding textureBinding6(UNIFORM_COMBINED_IMAGE_SAMPLER, SHADER_STAGE_FRAGMENT, 5);
    LayoutBinding textureBinding7(UNIFORM_COMBINED_IMAGE_SAMPLER, SHADER_STAGE_FRAGMENT, 6);
    m_descriptorPool.set_layout(
        OBJECT_TEXTURE_LAYOUT, {textureBinding1, textureBinding2, textureBinding3, textureBinding4, textureBinding5, textureBinding6, textureBinding7});

    // BINDLESS SETs
    LayoutBinding bindlessVAOs(UNIFORM_STORAGE_BUFFER, SHADER_STAGE_VERTEX, 0, ENGINE_MAX_OBJECTS);
    LayoutBinding bindlessIBOs(UNIFORM_STORAGE_BUFFER, SHADER_STAGE_VERTEX, 1, ENGINE_MAX_OBJECTS);
    m_descriptorPool.set_layout(3,
                                {bindlessVAOs, bindlessIBOs},
                                0,
                                {VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT,
                                 VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT});

    for (size_t i = 0; i < frames.size(); i++)
    {
        // Global
        m_descriptorPool.allocate_descriptor_set(GLOBAL_LAYOUT, &m_descriptors[i].globalDescritor);
        m_descriptorPool.set_descriptor_write(
            &frames[i].uniformBuffers[GLOBAL_LAYOUT], sizeof(CameraUniforms), 0, &m_descriptors[i].globalDescritor, UNIFORM_DYNAMIC_BUFFER, 0);
        m_descriptorPool.set_descriptor_write(&frames[i].uniformBuffers[GLOBAL_LAYOUT],
                                              sizeof(SceneUniforms),
                                              m_device->pad_uniform_buffer_size(sizeof(CameraUniforms)),
                                              &m_descriptors[i].globalDescritor,
                                              UNIFORM_DYNAMIC_BUFFER,
                                              1);

        m_descriptorPool.set_descriptor_write(
            get_image(ResourceManager::FALLBACK_TEXTURE), LAYOUT_SHADER_READ_ONLY_OPTIMAL, &m_descriptors[i].globalDescritor, 3);

        m_descriptorPool.set_descriptor_write(
            get_image(ResourceManager::BLUE_NOISE_TEXTURE), LAYOUT_SHADER_READ_ONLY_OPTIMAL, &m_descriptors[i].globalDescritor, 6);
        m_descriptorPool.set_descriptor_write(
            get_image(ResourceManager::HAIR_FAR_FIELD_DIST), LAYOUT_SHADER_READ_ONLY_OPTIMAL, &m_descriptors[i].globalDescritor, 7);
        m_descriptorPool.set_descriptor_write(
            &ResourceManager::HAIR_PERECEIVED_DENSITY_VOLUME, LAYOUT_SHADER_READ_ONLY_OPTIMAL, &m_descriptors[i].globalDescritor, 9);
        m_descriptorPool.set_descriptor_write(&ResourceManager::HAIR_NG, LAYOUT_SHADER_READ_ONLY_OPTIMAL, &m_descriptors[i].globalDescritor, 10);
        m_descriptorPool.set_descriptor_write(&ResourceManager::HAIR_NG_TRT, LAYOUT_SHADER_READ_ONLY_OPTIMAL, &m_descriptors[i].globalDescritor, 11);
        m_descriptorPool.set_descriptor_write(&ResourceManager::HAIR_GI, LAYOUT_SHADER_READ_ONLY_OPTIMAL, &m_descriptors[i].globalDescritor, 12);
        m_descriptorPool.set_descriptor_write(&ResourceManager::HAIR_VOXEL_VOLUME, LAYOUT_SHADER_READ_ONLY_OPTIMAL, &m_descriptors[i].globalDescritor, 13);
        // m_descriptorPool.set_descriptor_write( get_image(ResourceManager::HAIR_GI_FALLBACK), LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        // &m_descriptors[i].globalDescritor, 13);

        // Per-object
        m_descriptorPool.allocate_descriptor_set(OBJECT_LAYOUT, &m_descriptors[i].objectDescritor);
        m_descriptorPool.set_descriptor_write(
            &frames[i].uniformBuffers[OBJECT_LAYOUT], sizeof(ObjectUniforms), 0, &m_descriptors[i].objectDescritor, UNIFORM_DYNAMIC_BUFFER, 0);
        m_descriptorPool.set_descriptor_write(&frames[i].uniformBuffers[OBJECT_LAYOUT],
                                              sizeof(MaterialUniforms),
                                              m_device->pad_uniform_buffer_size(sizeof(MaterialUniforms)),
                                              &m_descriptors[i].objectDescritor,
                                              UNIFORM_DYNAMIC_BUFFER,
                                              1);
        // Set up enviroment fallback texture
        m_descriptorPool.set_descriptor_write(
            get_image(ResourceManager::FALLBACK_CUBEMAP), LAYOUT_SHADER_READ_ONLY_OPTIMAL, &m_descriptors[i].globalDescritor, 3);
        m_descriptorPool.set_descriptor_write(
            get_image(ResourceManager::FALLBACK_CUBEMAP), LAYOUT_SHADER_READ_ONLY_OPTIMAL, &m_descriptors[i].globalDescritor, 4);

        // Bindless
        m_descriptorPool.allocate_varaible_descriptor_set(3, &m_descriptors[i].bindlessDescriptor, ENGINE_MAX_OBJECTS);
    }
}
void ForwardPass::setup_shader_passes() {

    std::vector<VkDynamicState>                      dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
                                                                      VK_DYNAMIC_STATE_SCISSOR,
                                                                      VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
                                                                      VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
                                                                      VK_DYNAMIC_STATE_CULL_MODE};
    std::vector<VkPipelineColorBlendAttachmentState> blendAttachments{
        Init::color_blend_attachment_state(true),  // [0] HDR — needs blending for transparency
        Init::color_blend_attachment_state(true),  // [1] Bright
        Init::color_blend_attachment_state(false), // [2] Normals — data MRT, no blending
        Init::color_blend_attachment_state(false), // [3] AlbedoMask — data MRT, no blending
        Init::color_blend_attachment_state(false), // [4] DiffuseIrradiance — data MRT, no blending
        Init::color_blend_attachment_state(false), // [5] BackIrradiance — data MRT, no blending
        Init::color_blend_attachment_state(false), // [6] LinearDepth — data MRT, no blending
    };

    VkSampleCountFlagBits samples = static_cast<VkSampleCountFlagBits>(m_aa);

    // Setup shaderpasses
    GraphicShaderPass* unlitPass =
        new GraphicShaderPass(m_device->get_handle(), m_renderpass, m_imageExtent, ENGINE_RESOURCES_PATH "shaders/forward/unlit.glsl");
    unlitPass->settings.descriptorSetLayoutIDs = {{GLOBAL_LAYOUT, true}, {OBJECT_LAYOUT, true}, {OBJECT_TEXTURE_LAYOUT, false}};
    unlitPass->graphicSettings.attributes      = {
        {POSITION_ATTRIBUTE, true}, {NORMAL_ATTRIBUTE, false}, {UV_ATTRIBUTE, false}, {TANGENT_ATTRIBUTE, false}, {COLOR_ATTRIBUTE, false}};
    unlitPass->graphicSettings.blendAttachments = blendAttachments;
    unlitPass->graphicSettings.dynamicStates    = dynamicStates;
    unlitPass->graphicSettings.samples          = samples;
    m_shaderPasses[IMaterial::Type::UNLIT_TYPE] = unlitPass;

    GraphicShaderPass* phongPass =
        new GraphicShaderPass(m_device->get_handle(), m_renderpass, m_imageExtent, ENGINE_RESOURCES_PATH "shaders/forward/phong.glsl");
    phongPass->settings.descriptorSetLayoutIDs = {{GLOBAL_LAYOUT, true}, {OBJECT_LAYOUT, true}, {OBJECT_TEXTURE_LAYOUT, true}};
    phongPass->graphicSettings.attributes      = {
        {POSITION_ATTRIBUTE, true}, {NORMAL_ATTRIBUTE, true}, {UV_ATTRIBUTE, true}, {TANGENT_ATTRIBUTE, false}, {COLOR_ATTRIBUTE, false}};
    phongPass->graphicSettings.blendAttachments = blendAttachments;
    phongPass->graphicSettings.dynamicStates    = dynamicStates;
    phongPass->graphicSettings.samples          = samples;
    m_shaderPasses[IMaterial::Type::PHONG_TYPE] = phongPass;

    GraphicShaderPass* PBRPass =
        new GraphicShaderPass(m_device->get_handle(), m_renderpass, m_imageExtent, ENGINE_RESOURCES_PATH "shaders/forward/physically_based.glsl");
    PBRPass->settings.descriptorSetLayoutIDs = {{GLOBAL_LAYOUT, true}, {OBJECT_LAYOUT, true}, {OBJECT_TEXTURE_LAYOUT, true}};
    PBRPass->graphicSettings.attributes      = {
        {POSITION_ATTRIBUTE, true}, {NORMAL_ATTRIBUTE, true}, {UV_ATTRIBUTE, true}, {TANGENT_ATTRIBUTE, true}, {COLOR_ATTRIBUTE, false}};
    PBRPass->graphicSettings.blendAttachments = blendAttachments;
    PBRPass->graphicSettings.dynamicStates    = dynamicStates;
    PBRPass->graphicSettings.samples          = samples;
    m_shaderPasses[IMaterial::Type::PBR_TYPE] = PBRPass;

    GraphicShaderPass* hairStrandPass =
        new GraphicShaderPass(m_device->get_handle(), m_renderpass, m_imageExtent, ENGINE_RESOURCES_PATH "shaders/forward/hair_strand.glsl");
    hairStrandPass->settings.descriptorSetLayoutIDs = {{GLOBAL_LAYOUT, true}, {OBJECT_LAYOUT, true}, {OBJECT_TEXTURE_LAYOUT, false}};
    hairStrandPass->graphicSettings.attributes      = {
        {POSITION_ATTRIBUTE, true}, {NORMAL_ATTRIBUTE, false}, {UV_ATTRIBUTE, false}, {TANGENT_ATTRIBUTE, true}, {COLOR_ATTRIBUTE, true}};
    hairStrandPass->graphicSettings.dynamicStates    = dynamicStates;
    hairStrandPass->graphicSettings.blendAttachments = blendAttachments;
    hairStrandPass->graphicSettings.samples          = samples;
    hairStrandPass->graphicSettings.sampleShading    = false;
    hairStrandPass->graphicSettings.topology         = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    m_shaderPasses[IMaterial::Type::HAIR_STR_TYPE]   = hairStrandPass;
#if FAST_HAIR_GEOMETRY == 1
    GraphicShaderPass* hairStrandPass2 =
        new GraphicShaderPass(m_device->get_handle(), m_renderpass, m_imageExtent, ENGINE_RESOURCES_PATH "shaders/forward/fast_hair_strand_epic.glsl");
    hairStrandPass2->settings.descriptorSetLayoutIDs    = {{0, true}, {1, true}, {2, true}, {3, true}};
    hairStrandPass2->settings.pushConstants             = {Graphics::PushConstant(SHADER_STAGE_VERTEX | SHADER_STAGE_FRAGMENT, sizeof(Vec4))};
    hairStrandPass2->graphicSettings.dynamicStates      = dynamicStates;
    hairStrandPass2->graphicSettings.samples            = samples;
    hairStrandPass2->graphicSettings.sampleShading      = false;
    hairStrandPass2->graphicSettings.blendAttachments   = blendAttachments;
    hairStrandPass2->graphicSettings.topology           = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    m_shaderPasses[IMaterial::Type::HAIR_STR_EPIC_TYPE] = hairStrandPass2;
#else
    GraphicShaderPass* hairStrandPass2 =
        new GraphicShaderPass(m_device->get_handle(), m_renderpass, m_imageExtent, ENGINE_RESOURCES_PATH "shaders/forward/hair_strand_epic.glsl");
    hairStrandPass2->settings.descriptorSetLayoutIDs = {{GLOBAL_LAYOUT, true}, {OBJECT_LAYOUT, true}, {OBJECT_TEXTURE_LAYOUT, true}};
    hairStrandPass2->settings.pushConstants          = {Graphics::PushConstant(SHADER_STAGE_FRAGMENT, sizeof(Vec4))};
    hairStrandPass2->graphicSettings.attributes      = {
        {POSITION_ATTRIBUTE, true}, {NORMAL_ATTRIBUTE, false}, {UV_ATTRIBUTE, true}, {TANGENT_ATTRIBUTE, true}, {COLOR_ATTRIBUTE, true}};
    hairStrandPass2->graphicSettings.dynamicStates      = dynamicStates;
    hairStrandPass2->graphicSettings.samples            = samples;
    hairStrandPass2->graphicSettings.sampleShading      = true;
    hairStrandPass2->graphicSettings.blendAttachments   = blendAttachments;
    hairStrandPass2->graphicSettings.topology           = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    hairStrandPass2->graphicSettings.alphaToCoverage    = false;
    hairStrandPass2->graphicSettings.alphaToOne    = false;
    m_shaderPasses[IMaterial::Type::HAIR_STR_EPIC_TYPE] = hairStrandPass2;

#endif
    GraphicShaderPass* hairCardPass =
        new GraphicShaderPass(m_device->get_handle(), m_renderpass, m_imageExtent, ENGINE_RESOURCES_PATH "shaders/forward/hair_card.glsl");
    hairCardPass->settings.descriptorSetLayoutIDs = {{GLOBAL_LAYOUT, true}, {OBJECT_LAYOUT, true}, {OBJECT_TEXTURE_LAYOUT, true}};
    hairCardPass->graphicSettings.attributes      = {
        {POSITION_ATTRIBUTE, true}, {NORMAL_ATTRIBUTE, true}, {UV_ATTRIBUTE, true}, {TANGENT_ATTRIBUTE, true}, {COLOR_ATTRIBUTE, false}};
    hairCardPass->graphicSettings.blendAttachments = blendAttachments;
    hairCardPass->graphicSettings.dynamicStates    = dynamicStates;
    hairCardPass->graphicSettings.samples          = samples;
    m_shaderPasses[IMaterial::Type::HAIR_CARD_TYPE] = hairCardPass;

    GraphicShaderPass* hairStrandPassDisney =
        new GraphicShaderPass(m_device->get_handle(), m_renderpass, m_imageExtent, ENGINE_RESOURCES_PATH "shaders/forward/hair_strand_disney.glsl");
    hairStrandPassDisney->settings.descriptorSetLayoutIDs = {{GLOBAL_LAYOUT, true}, {OBJECT_LAYOUT, true}, {OBJECT_TEXTURE_LAYOUT, true}};
    hairStrandPassDisney->graphicSettings.attributes      = {
        {POSITION_ATTRIBUTE, true}, {NORMAL_ATTRIBUTE, false}, {UV_ATTRIBUTE, false}, {TANGENT_ATTRIBUTE, true}, {COLOR_ATTRIBUTE, true}};
    hairStrandPassDisney->graphicSettings.dynamicStates    = dynamicStates;
    hairStrandPassDisney->graphicSettings.samples          = samples;
    hairStrandPassDisney->graphicSettings.sampleShading    = false;
    hairStrandPassDisney->graphicSettings.blendAttachments = blendAttachments;
    hairStrandPassDisney->graphicSettings.topology         = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    m_shaderPasses[IMaterial::Type::HAIR_STR_DISNEY_TYPE]  = hairStrandPassDisney;

    GraphicShaderPass* skyboxPass =
        new GraphicShaderPass(m_device->get_handle(), m_renderpass, m_imageExtent, ENGINE_RESOURCES_PATH "shaders/forward/skybox.glsl");
    skyboxPass->settings.descriptorSetLayoutIDs = {{GLOBAL_LAYOUT, true}, {OBJECT_LAYOUT, false}, {OBJECT_TEXTURE_LAYOUT, false}};
    skyboxPass->graphicSettings.attributes      = {
        {POSITION_ATTRIBUTE, true}, {NORMAL_ATTRIBUTE, false}, {UV_ATTRIBUTE, false}, {TANGENT_ATTRIBUTE, false}, {COLOR_ATTRIBUTE, false}};
    skyboxPass->graphicSettings.dynamicStates    = dynamicStates;
    skyboxPass->graphicSettings.samples          = samples;
    skyboxPass->graphicSettings.blendAttachments = blendAttachments;
    skyboxPass->graphicSettings.depthOp          = VK_COMPARE_OP_LESS_OR_EQUAL;
    m_shaderPasses[hash_string("skybox")]        = skyboxPass;

    for (auto pair : m_shaderPasses)
    {
        ShaderPass* pass = pair.second;

        pass->build_shader_stages();
        pass->build(m_descriptorPool);
    }
}

void ForwardPass::render(Graphics::Frame& currentFrame, Scene* const scene, uint32_t presentImageIndex) {
    PROFILING_EVENT()

    CommandBuffer cmd = currentFrame.commandBuffer;
    cmd.begin_renderpass(m_renderpass, m_framebuffers[0]);
    cmd.set_viewport(m_imageExtent);

    if (scene->get_active_camera() && scene->get_active_camera()->is_active())
    {

        unsigned int mesh_idx = 0;
        for (Mesh* m : scene->get_meshes())
        {
            if (m)
            {
                if (m->is_active() &&              // Check if is active
                    m->get_num_geometries() > 0 && // Check if has geometry
                    (scene->get_active_camera()->get_frustrum_culling() && m->get_bounding_volume()
                         ? m->get_bounding_volume()->is_on_frustrum(scene->get_active_camera()->get_frustrum())
                         : true)) // Check if is inside frustrum
                {
                    // Offset calculation
                    uint32_t objectOffset = currentFrame.uniformBuffers[1].strideSize * mesh_idx;

                    for (size_t i = 0; i < m->get_num_geometries(); i++)
                    {
                        Geometry*  g   = m->get_geometry(i);
                        IMaterial* mat = m->get_material(g->get_material_ID());

                        cmd.set_depth_test_enable(mat->get_parameters().depthTest);
                        cmd.set_depth_write_enable(mat->get_parameters().depthWrite);
                        cmd.set_cull_mode(mat->get_parameters().faceCulling ? mat->get_parameters().culling : CullingMode::NO_CULLING);

                        ShaderPass* shaderPass = m_shaderPasses[mat->get_type()];

                        // Bind pipeline
                        cmd.bind_shaderpass(*shaderPass);

                        // GLOBAL LAYOUT BINDING
                        cmd.bind_descriptor_set(m_descriptors[currentFrame.index].globalDescritor, 0, *shaderPass, {0, 0});
                        // PER OBJECT LAYOUT BINDING
                        cmd.bind_descriptor_set(m_descriptors[currentFrame.index].objectDescritor, 1, *shaderPass, {objectOffset, objectOffset});
                        // TEXTURE LAYOUT BINDING
                        if (shaderPass->settings.descriptorSetLayoutIDs[OBJECT_TEXTURE_LAYOUT])
                            cmd.bind_descriptor_set(mat->get_texture_descriptor(), 2, *shaderPass);

#if FAST_HAIR_GEOMETRY == 1
                        // DRAW
                        if (mat->get_type() == IMaterial::Type::HAIR_STR_EPIC_TYPE)
                        {
                            // SSBO Bindless
                            cmd.bind_descriptor_set(m_descriptors[currentFrame.index].bindlessDescriptor, 3, *shaderPass, {});

                            uint32_t numSegments   = g->get_properties().vertexIndex.size() * 0.5;
                            float    avgHairLength = g->get_properties().avgFiberLength * m->get_scale().x;
                            Vec4     data = Vec4(float(mesh_idx), float(numSegments), static_cast<HairEpicMaterial*>(mat)->get_thickness(), avgHairLength);
                            cmd.push_constants(*shaderPass, SHADER_STAGE_VERTEX | SHADER_STAGE_FRAGMENT, &data, sizeof(Vec4));

                            cmd.draw_geometry(4, numSegments);
                        } else
                            cmd.draw_geometry(*get_VAO(g));

#else

                        if (mat->get_type() == IMaterial::Type::HAIR_STR_EPIC_TYPE)
                        {
                            float avgHairLength = g->get_properties().avgFiberLength * m->get_scale().x;
                            Vec4  data          = Vec4(float(mesh_idx), avgHairLength, 0.0, 0.0);
                            cmd.push_constants(*shaderPass, SHADER_STAGE_FRAGMENT, &data, sizeof(Vec4));
                        }

                        cmd.draw_geometry(*get_VAO(g));
#endif
                    }
                }
            }
            mesh_idx++;
        }
        // Skybox
        if (scene->get_skybox())
        {
            if (scene->get_skybox()->is_active())
            {

                cmd.set_depth_test_enable(true);
                cmd.set_depth_write_enable(true);
                cmd.set_cull_mode(CullingMode::NO_CULLING);

                ShaderPass* shaderPass = m_shaderPasses[hash_string("skybox")];

                // Bind pipeline
                cmd.bind_shaderpass(*shaderPass);

                // GLOBAL LAYOUT BINDING
                cmd.bind_descriptor_set(m_descriptors[currentFrame.index].globalDescritor, 0, *shaderPass, {0, 0});
                cmd.draw_geometry(*get_VAO(scene->get_skybox()->get_box()));
            }
        }
    }

    // Draw gui contents
    if (m_isDefault && Frame::guiEnabled)
        cmd.draw_gui_data();

    cmd.end_renderpass(m_renderpass, m_framebuffers[0]);
}

void ForwardPass::update_uniforms(uint32_t frameIndex, Scene* const scene) {
    uint32_t meshIdx = 0;
    for (Mesh* m : scene->get_meshes())
    {
        if (m)
        {
            for (size_t i = 0; i < m->get_num_geometries(); i++)
            {
                Geometry*  g   = m->get_geometry(i);
                IMaterial* mat = m->get_material(g->get_material_ID());
                setup_material_descriptor(mat);

                VAO* vao = get_VAO(g);
                if (vao->loadedOnGPU)
                {
                    // Pos SSBO binding
                    m_descriptorPool.set_descriptor_write(
                        &vao->posSSBO, vao->posSSBO.size, 0, &m_descriptors[frameIndex].bindlessDescriptor, UNIFORM_STORAGE_BUFFER, 0, meshIdx);
                    // IBO binding
                    m_descriptorPool.set_descriptor_write(
                        &vao->indexSSBO, vao->indexSSBO.size, 0, &m_descriptors[frameIndex].bindlessDescriptor, UNIFORM_STORAGE_BUFFER, 1, meshIdx);
                }
            }
        }
        meshIdx++;
    }
    if (!get_TLAS(scene)->binded)
    {
        for (size_t i = 0; i < m_descriptors.size(); i++)
        {
            m_descriptorPool.set_descriptor_write(get_TLAS(scene), &m_descriptors[i].globalDescritor, 5);
        }
        get_TLAS(scene)->binded = true;
    }
}
void ForwardPass::link_previous_images(std::vector<Image> images) {
    for (size_t i = 0; i < m_descriptors.size(); i++)
    {
        m_descriptorPool.set_descriptor_write(&images[0],

                                              //   VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                                              LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                              &m_descriptors[i].globalDescritor,
                                              2);
    }
}

void ForwardPass::set_envmap_descriptor(Graphics::Image env, Graphics::Image irr) {
    for (size_t i = 0; i < m_descriptors.size(); i++)
    {
        m_descriptorPool.set_descriptor_write(&env, LAYOUT_SHADER_READ_ONLY_OPTIMAL, &m_descriptors[i].globalDescritor, 3);
        m_descriptorPool.set_descriptor_write(&irr, LAYOUT_SHADER_READ_ONLY_OPTIMAL, &m_descriptors[i].globalDescritor, 4);
    }
}
void ForwardPass::set_hair_scattering_map_descriptor(Graphics::Image frontAtt, Graphics::Image backAtt) {
    for (size_t i = 0; i < m_descriptors.size(); i++)
    {
        m_descriptorPool.set_descriptor_write(&frontAtt, LAYOUT_SHADER_READ_ONLY_OPTIMAL, &m_descriptors[i].globalDescritor, 8);
        m_descriptorPool.set_descriptor_write(&backAtt, LAYOUT_SHADER_READ_ONLY_OPTIMAL, &m_descriptors[i].globalDescritor, 9);
    }
}
void ForwardPass::setup_material_descriptor(IMaterial* mat) {
    if (!mat->get_texture_descriptor().allocated)
        m_descriptorPool.allocate_descriptor_set(OBJECT_TEXTURE_LAYOUT, &mat->get_texture_descriptor());

    auto textures = mat->get_textures();
    for (auto pair : textures)
    {
        ITexture* texture = pair.second;
        if (texture && texture->loaded_on_GPU())
        {

            // Set texture write
            if (!mat->get_texture_binding_state()[pair.first] || texture->is_dirty())
            {
                m_descriptorPool.set_descriptor_write(get_image(texture), LAYOUT_SHADER_READ_ONLY_OPTIMAL, &mat->get_texture_descriptor(), pair.first);
                mat->set_texture_binding_state(pair.first, true);
                texture->set_dirty(false);
            }
        } else
        {
            // SET DUMMY TEXTURE
            if (!mat->get_texture_binding_state()[pair.first])
                m_descriptorPool.set_descriptor_write(
                    get_image(ResourceManager::FALLBACK_TEXTURE), LAYOUT_SHADER_READ_ONLY_OPTIMAL, &mat->get_texture_descriptor(), pair.first);
            mat->set_texture_binding_state(pair.first, true);
        }
    }
}
} // namespace Core
VULKAN_ENGINE_NAMESPACE_END