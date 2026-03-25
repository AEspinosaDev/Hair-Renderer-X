#include <engine/core/passes/hair_voxelization_pass.h>
#include <engine/core/materials/hair.h>


VULKAN_ENGINE_NAMESPACE_BEGIN
using namespace Graphics;
namespace Core {

void HairVoxelizationPass::create_voxelization_image() {

    // Actual Voxel Image
    ResourceManager::HAIR_VOXEL_VOLUME.cleanup();

    ImageConfig config                 = {};
    config.viewType                    = TEXTURE_3D;
    config.format                      = SR_32F;
    config.usageFlags                  = IMAGE_USAGE_SAMPLED | IMAGE_USAGE_TRANSFER_DST | IMAGE_USAGE_TRANSFER_SRC | IMAGE_USAGE_STORAGE;
    config.mipLevels                   = MIP_LEVELS;
    config.baseMipLevel                = 0;
    ResourceManager::HAIR_VOXEL_VOLUME = m_device->create_image({m_imageExtent.width, m_imageExtent.width, m_imageExtent.width}, config, true);
    ResourceManager::HAIR_VOXEL_VOLUME.create_view(config);

    SamplerConfig samplerConfig      = {};
    samplerConfig.samplerAddressMode = ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerConfig.border             = BorderColor::FLOAT_OPAQUE_BLACK;
    samplerConfig.minLod             = 0;
    samplerConfig.maxLod             = MIP_LEVELS;
    ResourceManager::HAIR_VOXEL_VOLUME.create_sampler(samplerConfig);

    // Configure mip map images
    m_voxelMips.resize(MIP_LEVELS);
    // Create auxiliar images for mipmaps
    for (size_t i = 0; i < MIP_LEVELS; i++)
    {
        m_voxelMips[i]              = ResourceManager::HAIR_VOXEL_VOLUME.clone();
        m_voxelMips[i].baseMipLevel = i;
        m_voxelMips[i].mipLevels    = 1;
        m_voxelMips[i].create_view(config);
    }

    // Codified percieved Density
    ResourceManager::HAIR_PERECEIVED_DENSITY_VOLUME.cleanup();

    config            = {};
    config.viewType   = TEXTURE_3D;
    config.format     = SRGBA_32F;
    config.usageFlags = IMAGE_USAGE_SAMPLED | IMAGE_USAGE_TRANSFER_DST | IMAGE_USAGE_TRANSFER_SRC | IMAGE_USAGE_STORAGE;
    config.mipLevels  = 1;
    ResourceManager::HAIR_PERECEIVED_DENSITY_VOLUME =
        m_device->create_image({m_imageExtent.width >> 2, m_imageExtent.width >> 2, m_imageExtent.width >> 2}, config, false);
    ResourceManager::HAIR_PERECEIVED_DENSITY_VOLUME.create_view(config);

    samplerConfig                    = {};
    samplerConfig.samplerAddressMode = ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerConfig.border             = BorderColor::FLOAT_OPAQUE_BLACK;
    ResourceManager::HAIR_PERECEIVED_DENSITY_VOLUME.create_sampler(samplerConfig);
}

void HairVoxelizationPass::setup_attachments(std::vector<Graphics::AttachmentInfo>& attachments, std::vector<Graphics::SubPassDependency>& dependencies) {

    attachments.resize(1);

    attachments[0] = Graphics::AttachmentInfo(R_8U,
                                              1,
                                              LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                              LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                              IMAGE_USAGE_COLOR_ATTACHMENT | IMAGE_USAGE_SAMPLED,
                                              COLOR_ATTACHMENT,
                                              ASPECT_COLOR);
    create_voxelization_image();
    // Depdencies
    dependencies.resize(2);

    dependencies[0]               = Graphics::SubPassDependency(STAGE_BOTTOM_OF_PIPE, STAGE_COLOR_ATTACHMENT_OUTPUT, ACCESS_COLOR_ATTACHMENT_WRITE);
    dependencies[0].srcAccessMask = ACCESS_MEMORY_READ;
    dependencies[1]               = Graphics::SubPassDependency(STAGE_COLOR_ATTACHMENT_OUTPUT, STAGE_BOTTOM_OF_PIPE, ACCESS_MEMORY_READ);
    dependencies[1].srcAccessMask = ACCESS_COLOR_ATTACHMENT_WRITE;
    dependencies[1].srcSubpass    = 0;
    dependencies[1].dstSubpass    = VK_SUBPASS_EXTERNAL;

    m_isResizeable = false;
}
void HairVoxelizationPass::setup_uniforms(std::vector<Graphics::Frame>& frames) {

    // CREATE AND POPULATE DIRECTION BUFFER
    // //////////////////////////////////////
    const size_t BUFFER_SIZE = m_device->pad_uniform_buffer_size(sizeof(Vec4) * MAX_DIRECTIONS);
    m_directionsBuffer       = m_device->create_buffer_VMA(BUFFER_SIZE, BUFFER_USAGE_STORAGE_BUFFER, VMA_MEMORY_USAGE_CPU_TO_GPU);

    std::vector<Vec4> directions(MAX_DIRECTIONS);

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
    const float goldenRatio    = (1.0f + std::sqrt(5.0f)) * 0.5f;
    const float angleIncrement = 2.0f * float(M_PI) * (1.0f - 1.0f / goldenRatio);

    for (unsigned int i = 0; i < MAX_DIRECTIONS; ++i)
    {
        float t           = float(i) / float(MAX_DIRECTIONS);
        float inclination = std::acos(1.0f - 2.0f * t); // polar angle
        float azimuth     = angleIncrement * i;         // azimuthal angle

        float x = std::sin(inclination) * std::cos(azimuth);
        float y = std::sin(inclination) * std::sin(azimuth);
        float z = std::cos(inclination);

        directions[i] = Vec4(x, y, z, 0.0f);
    }

    m_directionsBuffer.upload_data(directions.data(), BUFFER_SIZE);

    // //////////////////////////////////////

    m_descriptorPool = m_device->create_descriptor_pool(ENGINE_MAX_OBJECTS, ENGINE_MAX_OBJECTS, ENGINE_MAX_OBJECTS, ENGINE_MAX_OBJECTS*3, ENGINE_MAX_OBJECTS);
    m_descriptors.resize(frames.size());

    // GLOBAL SET
    LayoutBinding camBufferBinding(UNIFORM_DYNAMIC_BUFFER, SHADER_STAGE_VERTEX | SHADER_STAGE_COMPUTE | SHADER_STAGE_FRAGMENT, 0);
    LayoutBinding sceneBufferBinding(UNIFORM_DYNAMIC_BUFFER, SHADER_STAGE_VERTEX | SHADER_STAGE_COMPUTE | SHADER_STAGE_FRAGMENT, 1);
    LayoutBinding voxelBinding(UNIFORM_STORAGE_IMAGE, SHADER_STAGE_FRAGMENT, 2);
    LayoutBinding shBinding(UNIFORM_STORAGE_IMAGE, SHADER_STAGE_COMPUTE, 3);
    LayoutBinding dirBinding(UNIFORM_STORAGE_BUFFER, SHADER_STAGE_COMPUTE, 4);
    LayoutBinding voxelSamplerBindng(UNIFORM_COMBINED_IMAGE_SAMPLER, SHADER_STAGE_COMPUTE, 5);
    LayoutBinding vexelMipmapsBindings(UNIFORM_STORAGE_IMAGE, SHADER_STAGE_COMPUTE, 6, MIP_LEVELS);
    LayoutBinding vexelMipmapsBindings2(UNIFORM_STORAGE_IMAGE, SHADER_STAGE_COMPUTE, 7, MIP_LEVELS);
    m_descriptorPool.set_layout(
        GLOBAL_LAYOUT,
        {camBufferBinding, sceneBufferBinding, voxelBinding, shBinding, dirBinding, voxelSamplerBindng, vexelMipmapsBindings, vexelMipmapsBindings2});

    // PER-OBJECT SET
    LayoutBinding objectBufferBinding(UNIFORM_DYNAMIC_BUFFER, SHADER_STAGE_VERTEX | SHADER_STAGE_GEOMETRY | SHADER_STAGE_FRAGMENT, 0);
    LayoutBinding materialBufferBinding(UNIFORM_DYNAMIC_BUFFER, SHADER_STAGE_VERTEX | SHADER_STAGE_GEOMETRY | SHADER_STAGE_FRAGMENT, 1);
    m_descriptorPool.set_layout(OBJECT_LAYOUT, {objectBufferBinding, materialBufferBinding});

    // BINDLESS SETs
    LayoutBinding bindlessVAOs(UNIFORM_STORAGE_BUFFER, SHADER_STAGE_COMPUTE, 0, ENGINE_MAX_OBJECTS);
    LayoutBinding bindlessIBOs(UNIFORM_STORAGE_BUFFER, SHADER_STAGE_COMPUTE, 1, ENGINE_MAX_OBJECTS);
    m_descriptorPool.set_layout(2,
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
        // Voxelization Image
        m_descriptorPool.set_descriptor_write(&ResourceManager::HAIR_VOXEL_VOLUME, LAYOUT_GENERAL, &m_descriptors[i].globalDescritor, 2, UNIFORM_STORAGE_IMAGE);
        m_descriptorPool.set_descriptor_write(
            &ResourceManager::HAIR_PERECEIVED_DENSITY_VOLUME, LAYOUT_GENERAL, &m_descriptors[i].globalDescritor, 3, UNIFORM_STORAGE_IMAGE);
        m_descriptorPool.set_descriptor_write(&m_directionsBuffer, BUFFER_SIZE, 0, &m_descriptors[i].globalDescritor, UNIFORM_STORAGE_BUFFER, 4);
        m_descriptorPool.set_descriptor_write(&ResourceManager::HAIR_VOXEL_VOLUME, LAYOUT_SHADER_READ_ONLY_OPTIMAL, &m_descriptors[i].globalDescritor, 5);
        m_descriptorPool.set_descriptor_write(m_voxelMips, LAYOUT_GENERAL, &m_descriptors[i].globalDescritor, 6, UNIFORM_STORAGE_IMAGE);
        m_descriptorPool.set_descriptor_write(m_voxelMips, LAYOUT_GENERAL, &m_descriptors[i].globalDescritor, 7, UNIFORM_STORAGE_IMAGE);

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

        m_descriptorPool.allocate_varaible_descriptor_set(2, &m_descriptors[i].bufferDescritor, ENGINE_MAX_OBJECTS);
    }
}
void HairVoxelizationPass::setup_shader_passes() {

#if OPTICAL_DENSITY == 1
    ComputeShaderPass* voxelPass = new ComputeShaderPass(m_device->get_handle(), ENGINE_RESOURCES_PATH "shaders/misc/DDA_fiber_optical_density.glsl");
    voxelPass->settings.descriptorSetLayoutIDs = {{0, true}, {1, true}, {2, true}};
    voxelPass->settings.pushConstants          = {Graphics::PushConstant(SHADER_STAGE_COMPUTE, sizeof(Vec4))};
    voxelPass->build_shader_stages();
    voxelPass->build(m_descriptorPool);

    m_shaderPasses[0] = voxelPass;

#endif
#if DDA_VOXELIZATION == 1
    ComputeShaderPass* voxelPass = new ComputeShaderPass(m_device->get_handle(), ENGINE_RESOURCES_PATH "shaders/misc/DDA_density_voxelization.glsl");
    voxelPass->settings.descriptorSetLayoutIDs = {{0, true}, {1, true}, {2, true}};
    voxelPass->settings.pushConstants          = {Graphics::PushConstant(SHADER_STAGE_COMPUTE, sizeof(Vec4))};
    voxelPass->build_shader_stages();
    voxelPass->build(m_descriptorPool);

    m_shaderPasses[0] = voxelPass;
#elif RASTER_VOXELIZATION == 1

    GraphicShaderPass* voxelPass = new GraphicShaderPass(
        m_device->get_handle(), m_renderpass, m_imageExtent, ENGINE_RESOURCES_PATH "shaders/misc/CLASSIC_fiber_density_voxelization.glsl");
    voxelPass->settings.descriptorSetLayoutIDs = {{GLOBAL_LAYOUT, true}, {OBJECT_LAYOUT, true}, {OBJECT_TEXTURE_LAYOUT, false}};
    voxelPass->graphicSettings.attributes      = {
        {POSITION_ATTRIBUTE, true}, {NORMAL_ATTRIBUTE, false}, {UV_ATTRIBUTE, false}, {TANGENT_ATTRIBUTE, false}, {COLOR_ATTRIBUTE, false}};
    voxelPass->graphicSettings.dynamicStates    = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineColorBlendAttachmentState state   = Init::color_blend_attachment_state(false);
    state.colorWriteMask                        = 0;
    voxelPass->graphicSettings.depthTest        = false;
    voxelPass->graphicSettings.blendAttachments = {state};
    voxelPass->graphicSettings.topology         = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    voxelPass->build_shader_stages();
    voxelPass->build(m_descriptorPool);

    m_shaderPasses[0] = voxelPass;
#endif

    ComputeShaderPass* shPass               = new ComputeShaderPass(m_device->get_handle(), ENGINE_RESOURCES_PATH "shaders/misc/encode_density_SH.glsl");
    shPass->settings.descriptorSetLayoutIDs = {{GLOBAL_LAYOUT, true}, {OBJECT_LAYOUT, true}, {OBJECT_TEXTURE_LAYOUT, false}};

    shPass->build_shader_stages();
    shPass->build(m_descriptorPool);

    m_shaderPasses[1] = shPass;

    ComputeShaderPass* mipPass = new ComputeShaderPass(m_device->get_handle(), ENGINE_RESOURCES_PATH "shaders/misc/density_conserving_mipmaps.glsl");
    mipPass->settings.descriptorSetLayoutIDs = {{GLOBAL_LAYOUT, true}, {OBJECT_LAYOUT, false}, {OBJECT_TEXTURE_LAYOUT, false}};
    mipPass->settings.pushConstants          = {Graphics::PushConstant(SHADER_STAGE_COMPUTE, sizeof(Vec4))};
    mipPass->build_shader_stages();
    mipPass->build(m_descriptorPool);

    m_shaderPasses[3] = mipPass;

    GraphicShaderPass* skullVoxelPass =
        new GraphicShaderPass(m_device->get_handle(), m_renderpass, m_imageExtent, ENGINE_RESOURCES_PATH "shaders/misc/CLASSIC_density_voxelization.glsl");
    skullVoxelPass->settings.descriptorSetLayoutIDs = {{GLOBAL_LAYOUT, true}, {OBJECT_LAYOUT, false}, {OBJECT_TEXTURE_LAYOUT, false}};
    skullVoxelPass->graphicSettings.attributes      = {
        {POSITION_ATTRIBUTE, true}, {NORMAL_ATTRIBUTE, false}, {UV_ATTRIBUTE, false}, {TANGENT_ATTRIBUTE, false}, {COLOR_ATTRIBUTE, false}};
    skullVoxelPass->graphicSettings.dynamicStates    = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineColorBlendAttachmentState state        = Init::color_blend_attachment_state(false);
    state.colorWriteMask                             = 0;
    skullVoxelPass->graphicSettings.depthTest        = false;
    skullVoxelPass->graphicSettings.blendAttachments = {state};
    skullVoxelPass->settings.pushConstants                  = {Graphics::PushConstant(SHADER_STAGE_FRAGMENT, sizeof(VolumeData))};

    skullVoxelPass->build_shader_stages();
    skullVoxelPass->build(m_descriptorPool);

    m_shaderPasses[4] = skullVoxelPass;
}
void HairVoxelizationPass::render(Graphics::Frame& currentFrame, Scene* const scene, uint32_t presentImageIndex) {
    PROFILING_EVENT()

    CommandBuffer cmd = currentFrame.commandBuffer;

    /*
    PREPARE VOXEL IMAGES TO BE USED IN SHADERS
    */
    if (ResourceManager::HAIR_VOXEL_VOLUME.currentLayout == LAYOUT_UNDEFINED)
    {
        cmd.pipeline_barrier(
            ResourceManager::HAIR_VOXEL_VOLUME, LAYOUT_UNDEFINED, LAYOUT_GENERAL, ACCESS_NONE, ACCESS_SHADER_WRITE, STAGE_TOP_OF_PIPE, STAGE_FRAGMENT_SHADER);
        cmd.pipeline_barrier(ResourceManager::HAIR_PERECEIVED_DENSITY_VOLUME,
                             LAYOUT_UNDEFINED,
                             LAYOUT_GENERAL,
                             ACCESS_NONE,
                             ACCESS_SHADER_WRITE,
                             STAGE_TOP_OF_PIPE,
                             STAGE_FRAGMENT_SHADER);
    } else
    {

        cmd.pipeline_barrier(ResourceManager::HAIR_VOXEL_VOLUME,
                             LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             LAYOUT_GENERAL,
                             ACCESS_SHADER_READ,
                             ACCESS_SHADER_WRITE,
                             STAGE_COMPUTE_SHADER,
                             STAGE_FRAGMENT_SHADER);

        cmd.pipeline_barrier(ResourceManager::HAIR_PERECEIVED_DENSITY_VOLUME,
                             LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             LAYOUT_GENERAL,
                             ACCESS_SHADER_READ,
                             ACCESS_SHADER_WRITE,
                             STAGE_FRAGMENT_SHADER,
                             STAGE_COMPUTE_SHADER);
    }
    /*
    CLEAR IMAGES
    */
    cmd.clear_image(ResourceManager::HAIR_VOXEL_VOLUME, LAYOUT_GENERAL, ASPECT_COLOR, Vec4(0.0));
    cmd.clear_image(ResourceManager::HAIR_PERECEIVED_DENSITY_VOLUME, LAYOUT_GENERAL, ASPECT_COLOR, Vec4(0.0));

    if (scene->get_active_camera() && scene->get_active_camera()->is_active())
    {

        unsigned int mesh_idx = 0;
        for (Mesh* m : scene->get_meshes())
        {
            if (m)
            {
                if (m->is_active() &&  // Check if is active
                    m->get_geometry()) // Check if is inside frustrum
                {
                    auto mat = m->get_material();
                    if (mat->get_type() == Core::IMaterial::Type::HAIR_STR_TYPE || mat->get_type() == Core::IMaterial::Type::HAIR_STR_EPIC_TYPE)
                    {

                        uint32_t objectOffset = currentFrame.uniformBuffers[1].strideSize * mesh_idx;
#if DDA_VOXELIZATION == 1 || OPTICAL_DENSITY == 1

                        ShaderPass* shPass = m_shaderPasses[0];
                        cmd.bind_shaderpass(*shPass);

                        cmd.bind_descriptor_set(m_descriptors[currentFrame.index].globalDescritor, 0, *shPass, {0, 0}, BINDING_TYPE_COMPUTE);
                        cmd.bind_descriptor_set(
                            m_descriptors[currentFrame.index].objectDescritor, 1, *shPass, {objectOffset, objectOffset}, BINDING_TYPE_COMPUTE);
                            cmd.bind_descriptor_set(m_descriptors[currentFrame.index].bufferDescritor, 2, *shPass, {}, BINDING_TYPE_COMPUTE);
                            
                            uint32_t numSegments   = m->get_geometry()->get_properties().vertexIndex.size() * 0.5;
                            float fiberThickness = static_cast<HairEpicMaterial*>(mat)->get_thickness();
                            Vec4     data          = Vec4(float(mesh_idx), float(numSegments), fiberThickness, 0.0);
                            cmd.push_constants(*shPass, SHADER_STAGE_COMPUTE, &data, sizeof(Vec4));
                            
                            // Dispatch
                        uint32_t wg = (numSegments + 31) / 32; // 32 threads
                        cmd.dispatch_compute({wg, 1, 1});
                        
                        // Draw triangles of skull and add them to the voxelization
                        auto skull = static_cast<HairEpicMaterial*>(mat)->get_skull();
                        if (skull)
                        {
                            cmd.begin_renderpass(m_renderpass, m_framebuffers[0]);
                            
                            cmd.set_viewport(m_imageExtent);
                            
                            shPass = m_shaderPasses[4];
                            // Bind pipeline
                            cmd.bind_shaderpass(*shPass);
                            cmd.bind_descriptor_set(m_descriptors[currentFrame.index].globalDescritor, 0, *shPass, {0, 0});

                            // DRAW
                            VolumeData vol;
                            vol.model = skull->get_model_matrix();
                            vol.maxCoord = m->get_model_matrix() * Vec4(m->get_bounding_volume()->maxCoords, 1.0);  
                            vol.minCoord = m->get_model_matrix() * Vec4(m->get_bounding_volume()->minCoords, 1.0);
                            vol.density = FLT_MAX;  
                            vol.density = 10000.0;  
                            cmd.push_constants(*shPass, SHADER_STAGE_FRAGMENT, &vol, sizeof(VolumeData));

                            cmd.draw_geometry(*get_VAO(skull->get_geometry()));

                            cmd.end_renderpass(m_renderpass, m_framebuffers[0]);
                        }

#else
                        /*
                        POPULATE AUXILIAR IMAGES WITH DENSITY
                        */
                        cmd.begin_renderpass(m_renderpass, m_framebuffers[0]);

                        cmd.set_viewport(m_imageExtent);

                        ShaderPass* shPass = m_shaderPasses[0];
                        // Bind pipeline
                        cmd.bind_shaderpass(*shPass);
                        // GLOBAL LAYOUT BINDING
                        cmd.bind_descriptor_set(m_descriptors[currentFrame.index].globalDescritor, 0, *shPass, {0, 0});

                        // PER OBJECT LAYOUT BINDING
                        cmd.bind_descriptor_set(m_descriptors[currentFrame.index].objectDescritor, 1, *shPass, {objectOffset, objectOffset});

                        // DRAW
                        auto g = m->get_geometry();
                        cmd.draw_geometry(*get_VAO(g));

                        cmd.end_renderpass(m_renderpass, m_framebuffers[0]);
#endif

                        /*
                        COMPUTE MIPMAPS
                        */
                        shPass = m_shaderPasses[3];
                        cmd.bind_shaderpass(*shPass);
                        for (uint32_t i = 1; i < MIP_LEVELS; i++)
                        {

                            cmd.pipeline_barrier(m_voxelMips[i],
                                                 LAYOUT_GENERAL,
                                                 LAYOUT_GENERAL,
                                                 ACCESS_SHADER_READ,
                                                 ACCESS_SHADER_WRITE,
                                                 STAGE_COMPUTE_SHADER,
                                                 STAGE_COMPUTE_SHADER);

                            Vec4 data = Vec4(i, 0.0, 0.0, 0.0);
                            cmd.push_constants(*shPass, SHADER_STAGE_COMPUTE, &data, sizeof(data));
                            cmd.bind_descriptor_set(m_descriptors[currentFrame.index].globalDescritor, 0, *shPass, {0, 0}, BINDING_TYPE_COMPUTE);

                            // Dispatch the compute shader
                            const uint32_t WORK_GROUP_SIZE = 8;
                            uint32_t       mipSize         = std::max(1u, m_imageExtent.width >> i);
                            cmd.dispatch_compute({(mipSize + WORK_GROUP_SIZE - 1) / WORK_GROUP_SIZE,
                                                  (mipSize + WORK_GROUP_SIZE - 1) / WORK_GROUP_SIZE,
                                                  (mipSize + WORK_GROUP_SIZE - 1) / WORK_GROUP_SIZE});

                            cmd.pipeline_barrier(m_voxelMips[i],
                                                 LAYOUT_GENERAL,
                                                 LAYOUT_GENERAL,
                                                 ACCESS_SHADER_WRITE,
                                                 ACCESS_SHADER_READ,
                                                 STAGE_COMPUTE_SHADER,
                                                 STAGE_COMPUTE_SHADER);
                        }

                        /*
                        DISPATCH COMPUTE FOR POPULATING FINAL PERCEIVED DENSITY IMAGE
                        */

                        cmd.pipeline_barrier(ResourceManager::HAIR_VOXEL_VOLUME,
                                             LAYOUT_GENERAL,
                                             LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                             ACCESS_SHADER_WRITE,
                                             ACCESS_SHADER_READ,
                                             STAGE_FRAGMENT_SHADER,
                                             STAGE_COMPUTE_SHADER);

                        shPass = m_shaderPasses[1];
                        cmd.bind_shaderpass(*shPass);

                        cmd.bind_descriptor_set(m_descriptors[currentFrame.index].globalDescritor, 0, *shPass, {0, 0}, BINDING_TYPE_COMPUTE);
                        cmd.bind_descriptor_set(
                            m_descriptors[currentFrame.index].objectDescritor, 1, *shPass, {objectOffset, objectOffset}, BINDING_TYPE_COMPUTE);

                        // Dispatch the compute shader
                        const uint32_t WORK_GROUP_SIZE_2 = 8;
                        uint32_t       gridSize2         = std::max(1u, m_imageExtent.width >> 2);
                        gridSize2                        = (gridSize2 + WORK_GROUP_SIZE_2 - 1) / WORK_GROUP_SIZE_2;
                        cmd.dispatch_compute({gridSize2, gridSize2, gridSize2});

                        break;
                    }
                }
            }
            mesh_idx++;
        }
    }

    cmd.pipeline_barrier(ResourceManager::HAIR_PERECEIVED_DENSITY_VOLUME,
                         LAYOUT_GENERAL,
                         LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         ACCESS_SHADER_WRITE,
                         ACCESS_SHADER_READ,
                         STAGE_COMPUTE_SHADER,
                         STAGE_FRAGMENT_SHADER);
}

void HairVoxelizationPass::update_uniforms(uint32_t frameIndex, Scene* const scene) {
#if DDA_VOXELIZATION == 1 || OPTICAL_DENSITY == 1
    uint32_t meshIdx = 0;
    for (Mesh* m : scene->get_meshes())
    {
        if (m)
        {
            auto g = m->get_geometry();

            VAO* vao = get_VAO(g);
            if (vao->loadedOnGPU)
            {
                // Pos SSBO binding
                m_descriptorPool.set_descriptor_write(
                    &vao->posSSBO, vao->posSSBO.size, 0, &m_descriptors[frameIndex].bufferDescritor, UNIFORM_STORAGE_BUFFER, 0, meshIdx);
                // IBO binding
                m_descriptorPool.set_descriptor_write(
                    &vao->indexSSBO, vao->indexSSBO.size, 0, &m_descriptors[frameIndex].bufferDescritor, UNIFORM_STORAGE_BUFFER, 1, meshIdx);
            }
        }
        meshIdx++;
    }
#endif
}
void HairVoxelizationPass::cleanup() {
    ResourceManager::HAIR_VOXEL_VOLUME.cleanup();
    ResourceManager::HAIR_PERECEIVED_DENSITY_VOLUME.cleanup();
    for (Image& img : m_voxelMips)
    {
        img.handle  = VK_NULL_HANDLE;
        img.sampler = VK_NULL_HANDLE;
        img.cleanup();
    }

    m_directionsBuffer.cleanup();
    GraphicPass::cleanup();
}
} // namespace Core

VULKAN_ENGINE_NAMESPACE_END
