#include <engine/core/passes/hair_scattering_pass.h>

VULKAN_ENGINE_NAMESPACE_BEGIN
using namespace Graphics;
namespace Core {

// #define HAIR_DISNEY

void HairScatteringPass::create_hair_scattering_images() {

    // Attenuation textures
    //--------------------------------------------------

    ResourceManager::HAIR_BACK_ATT.cleanup();
    ResourceManager::HAIR_BACK_SHIFTS.cleanup();
    ResourceManager::HAIR_FRONT_SHIFTS.cleanup();
    ResourceManager::HAIR_BACK_BETAS.cleanup();
    ResourceManager::HAIR_FRONT_BETAS.cleanup();
    ResourceManager::HAIR_FRONT_ATT.cleanup();
    ResourceManager::HAIR_GI.cleanup();

    ImageConfig config             = {};
    config.viewType                = TEXTURE_2D;
    config.format                  = SRGBA_32F;
    config.usageFlags              = IMAGE_USAGE_SAMPLED | IMAGE_USAGE_TRANSFER_DST | IMAGE_USAGE_TRANSFER_SRC | IMAGE_USAGE_STORAGE;
    config.mipLevels               = 1;
    ResourceManager::HAIR_BACK_ATT = m_device->create_image({m_imageExtent.width, 1, 1}, config, false);
    ResourceManager::HAIR_BACK_ATT.create_view(config);

    SamplerConfig samplerConfig      = {};
    samplerConfig.samplerAddressMode = ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerConfig.border             = BorderColor::FLOAT_OPAQUE_BLACK;
    ResourceManager::HAIR_BACK_ATT.create_sampler(samplerConfig);

    ResourceManager::HAIR_FRONT_ATT = m_device->create_image({m_imageExtent.width, 1, 1}, config, false);
    ResourceManager::HAIR_FRONT_ATT.create_view(config);
    ResourceManager::HAIR_FRONT_ATT.create_sampler(samplerConfig);

    ResourceManager::HAIR_BACK_SHIFTS = m_device->create_image({m_imageExtent.width, 1, 1}, config, false);
    ResourceManager::HAIR_BACK_SHIFTS.create_view(config);
    ResourceManager::HAIR_BACK_SHIFTS.create_sampler(samplerConfig);

    ResourceManager::HAIR_FRONT_SHIFTS = m_device->create_image({m_imageExtent.width, 1, 1}, config, false);
    ResourceManager::HAIR_FRONT_SHIFTS.create_view(config);
    ResourceManager::HAIR_FRONT_SHIFTS.create_sampler(samplerConfig);

    ResourceManager::HAIR_BACK_BETAS = m_device->create_image({m_imageExtent.width, 1, 1}, config, false);
    ResourceManager::HAIR_BACK_BETAS.create_view(config);
    ResourceManager::HAIR_BACK_BETAS.create_sampler(samplerConfig);

    ResourceManager::HAIR_FRONT_BETAS = m_device->create_image({m_imageExtent.width, 1, 1}, config, false);
    ResourceManager::HAIR_FRONT_BETAS.create_view(config);
    ResourceManager::HAIR_FRONT_BETAS.create_sampler(samplerConfig);

    // NG textures
    //--------------------------------------------------

    ResourceManager::HAIR_NG.cleanup();
    ResourceManager::HAIR_NG = m_device->create_image({m_imageExtent.width, m_imageExtent.width, 1}, config, false);
    ResourceManager::HAIR_NG.create_view(config);
    ResourceManager::HAIR_NG.create_sampler(samplerConfig);

    ResourceManager::HAIR_NG_TRT.cleanup();
    ResourceManager::HAIR_NG_TRT = m_device->create_image({m_imageExtent.width, m_imageExtent.width, 1}, config, false);
    ResourceManager::HAIR_NG_TRT.create_view(config);
    ResourceManager::HAIR_NG_TRT.create_sampler(samplerConfig);

    // LUT Texture
    //--------------------------------------------------

    ImageConfig configGI     = {};
    configGI.viewType        = TEXTURE_3D;
    configGI.format          = SRGBA_32F;
    configGI.usageFlags      = IMAGE_USAGE_SAMPLED | IMAGE_USAGE_TRANSFER_DST | IMAGE_USAGE_TRANSFER_SRC | IMAGE_USAGE_STORAGE;
    configGI.mipLevels       = 1;
    ResourceManager::HAIR_GI = m_device->create_image({m_imageExtent.width, m_imageExtent.width,  m_imageExtent.width}, configGI, false);
    ResourceManager::HAIR_GI.create_view(configGI);
    ResourceManager::HAIR_GI.create_sampler(samplerConfig);

    // // Normalizing Buffer
    //     m_normBuffer = m_device->create_buffer_VMA(
    //         sizeof(Vec4), BufferUsageFlags::BUFFER_USAGE_STORAGE_BUFFER | BufferUsageFlags::BUFFER_USAGE_TRANSFER_DST,
    //         VmaMemoryUsage::VMA_MEMORY_USAGE_GPU_ONLY);
}
void HairScatteringPass::setup_attachments(std::vector<Graphics::AttachmentInfo>& attachments, std::vector<Graphics::SubPassDependency>& dependencies) {
    create_hair_scattering_images();
}

void HairScatteringPass::setup_uniforms(std::vector<Graphics::Frame>& frames) {
    m_descriptorPool = m_device->create_descriptor_pool(ENGINE_MAX_OBJECTS, ENGINE_MAX_OBJECTS, ENGINE_MAX_OBJECTS, ENGINE_MAX_OBJECTS, ENGINE_MAX_OBJECTS);
    m_descriptors.resize(frames.size());

    // GLOBAL SET
    LayoutBinding backImgBinding(UNIFORM_STORAGE_IMAGE, SHADER_STAGE_COMPUTE, 0);
    LayoutBinding frontImgBinding(UNIFORM_STORAGE_IMAGE, SHADER_STAGE_COMPUTE, 1);
    LayoutBinding distributionBinding(UNIFORM_COMBINED_IMAGE_SAMPLER, SHADER_STAGE_COMPUTE, 2);
    LayoutBinding NGBinding(UNIFORM_STORAGE_IMAGE, SHADER_STAGE_COMPUTE, 3);
    LayoutBinding NGTRTBinding(UNIFORM_STORAGE_IMAGE, SHADER_STAGE_COMPUTE, 4);
    LayoutBinding backShiftBinding(UNIFORM_STORAGE_IMAGE, SHADER_STAGE_COMPUTE, 5);
    LayoutBinding frontShiftBinging(UNIFORM_STORAGE_IMAGE, SHADER_STAGE_COMPUTE, 6);
    LayoutBinding backBetaBinding(UNIFORM_STORAGE_IMAGE, SHADER_STAGE_COMPUTE, 7);
    LayoutBinding frontBetaBinding(UNIFORM_STORAGE_IMAGE, SHADER_STAGE_COMPUTE, 8);
    LayoutBinding GIbinding(UNIFORM_STORAGE_IMAGE, SHADER_STAGE_COMPUTE, 9);
    LayoutBinding bufferBinding(UNIFORM_STORAGE_BUFFER, SHADER_STAGE_COMPUTE, 10);
    m_descriptorPool.set_layout(GLOBAL_LAYOUT,
                                {backImgBinding,
                                 frontImgBinding,
                                 distributionBinding,
                                 NGBinding,
                                 NGTRTBinding,
                                 backShiftBinding,
                                 frontShiftBinging,
                                 backBetaBinding,
                                 frontBetaBinding,
                                 GIbinding,
                                 bufferBinding});
    // PER-OBJECT SET
    LayoutBinding objectBufferBinding(UNIFORM_DYNAMIC_BUFFER, SHADER_STAGE_COMPUTE, 0);
    LayoutBinding materialBufferBinding(UNIFORM_DYNAMIC_BUFFER, SHADER_STAGE_COMPUTE, 1);
    m_descriptorPool.set_layout(OBJECT_LAYOUT, {objectBufferBinding, materialBufferBinding});

    for (size_t i = 0; i < frames.size(); i++)
    {

        m_descriptorPool.allocate_descriptor_set(GLOBAL_LAYOUT, &m_descriptors[i].globalDescritor);
        m_descriptorPool.set_descriptor_write(&ResourceManager::HAIR_FRONT_ATT, LAYOUT_GENERAL, &m_descriptors[i].globalDescritor, 0, UNIFORM_STORAGE_IMAGE);
        m_descriptorPool.set_descriptor_write(&ResourceManager::HAIR_BACK_ATT, LAYOUT_GENERAL, &m_descriptors[i].globalDescritor, 1, UNIFORM_STORAGE_IMAGE);
        m_descriptorPool.set_descriptor_write(
            get_image(ResourceManager::HAIR_FAR_FIELD_DIST), LAYOUT_SHADER_READ_ONLY_OPTIMAL, &m_descriptors[i].globalDescritor, 2);
        m_descriptorPool.set_descriptor_write(&ResourceManager::HAIR_NG, LAYOUT_GENERAL, &m_descriptors[i].globalDescritor, 3, UNIFORM_STORAGE_IMAGE);
        m_descriptorPool.set_descriptor_write(&ResourceManager::HAIR_NG_TRT, LAYOUT_GENERAL, &m_descriptors[i].globalDescritor, 4, UNIFORM_STORAGE_IMAGE);
        m_descriptorPool.set_descriptor_write(&ResourceManager::HAIR_FRONT_SHIFTS, LAYOUT_GENERAL, &m_descriptors[i].globalDescritor, 5, UNIFORM_STORAGE_IMAGE);
        m_descriptorPool.set_descriptor_write(&ResourceManager::HAIR_BACK_SHIFTS, LAYOUT_GENERAL, &m_descriptors[i].globalDescritor, 6, UNIFORM_STORAGE_IMAGE);
        m_descriptorPool.set_descriptor_write(&ResourceManager::HAIR_FRONT_BETAS, LAYOUT_GENERAL, &m_descriptors[i].globalDescritor, 7, UNIFORM_STORAGE_IMAGE);
        m_descriptorPool.set_descriptor_write(&ResourceManager::HAIR_BACK_BETAS, LAYOUT_GENERAL, &m_descriptors[i].globalDescritor, 8, UNIFORM_STORAGE_IMAGE);
        m_descriptorPool.set_descriptor_write(&ResourceManager::HAIR_GI, LAYOUT_GENERAL, &m_descriptors[i].globalDescritor, 9, UNIFORM_STORAGE_IMAGE);
        // m_descriptorPool.set_descriptor_write(&m_normBuffer, sizeof(Vec4), 0.0f, &m_descriptors[i].globalDescritor, UNIFORM_STORAGE_BUFFER, 10);

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
    }
}

void HairScatteringPass::setup_shader_passes() {


    ComputeShaderPass* NPass               = new ComputeShaderPass(m_device->get_handle(), ENGINE_RESOURCES_PATH "shaders/misc/compute_hair_NGI.glsl");
    NPass->settings.descriptorSetLayoutIDs = {{GLOBAL_LAYOUT, true}, {OBJECT_LAYOUT, true}, {OBJECT_TEXTURE_LAYOUT, false}};

    NPass->build_shader_stages();
    NPass->build(m_descriptorPool);

    m_shaderPasses[1] = NPass;

    // ComputeShaderPass* GIPass               = new ComputeShaderPass(m_device->get_handle(), ENGINE_RESOURCES_PATH "shaders/misc/compute_hair_GI.glsl");
    // GIPass->settings.descriptorSetLayoutIDs = {{GLOBAL_LAYOUT, TRUE}, {OBJECT_LAYOUT, false}, {OBJECT_TEXTURE_LAYOUT, false}};

    // GIPass->build_shader_stages();
    // GIPass->build(m_descriptorPool);

    // m_shaderPasses[2] = GIPass;

    ComputeShaderPass* LUTPass               = new ComputeShaderPass(m_device->get_handle(), ENGINE_RESOURCES_PATH "shaders/misc/compute_hair_LUT.glsl");
    LUTPass->settings.descriptorSetLayoutIDs = {{GLOBAL_LAYOUT, true}, {OBJECT_LAYOUT, false}, {OBJECT_TEXTURE_LAYOUT, false}};
    LUTPass->build_shader_stages();
    LUTPass->build(m_descriptorPool);
    m_shaderPasses[2] = LUTPass;

   
}

void HairScatteringPass::render(Graphics::Frame& currentFrame, Scene* const scene, uint32_t presentImageIndex) {

    PROFILING_EVENT()

    CommandBuffer cmd = currentFrame.commandBuffer;

    /*
    PREPARE IMAGE TO BE USED IN SHADERS
    */
    if (ResourceManager::HAIR_BACK_ATT.currentLayout == LAYOUT_UNDEFINED)
    {
#ifdef HAIR_DISNEY
        cmd.pipeline_barrier(
            ResourceManager::HAIR_BACK_ATT, LAYOUT_UNDEFINED, LAYOUT_GENERAL, ACCESS_NONE, ACCESS_SHADER_WRITE, STAGE_TOP_OF_PIPE, STAGE_COMPUTE_SHADER);
        cmd.pipeline_barrier(
            ResourceManager::HAIR_FRONT_ATT, LAYOUT_UNDEFINED, LAYOUT_GENERAL, ACCESS_NONE, ACCESS_SHADER_WRITE, STAGE_TOP_OF_PIPE, STAGE_COMPUTE_SHADER);
        cmd.pipeline_barrier(
            ResourceManager::HAIR_NG, LAYOUT_UNDEFINED, LAYOUT_GENERAL, ACCESS_NONE, ACCESS_SHADER_WRITE, STAGE_TOP_OF_PIPE, STAGE_COMPUTE_SHADER);
        cmd.pipeline_barrier(
            ResourceManager::HAIR_NG_TRT, LAYOUT_UNDEFINED, LAYOUT_GENERAL, ACCESS_NONE, ACCESS_SHADER_WRITE, STAGE_TOP_OF_PIPE, STAGE_COMPUTE_SHADER);
        cmd.pipeline_barrier(
            ResourceManager::HAIR_BACK_SHIFTS, LAYOUT_UNDEFINED, LAYOUT_GENERAL, ACCESS_NONE, ACCESS_SHADER_WRITE, STAGE_TOP_OF_PIPE, STAGE_COMPUTE_SHADER);
        cmd.pipeline_barrier(
            ResourceManager::HAIR_BACK_BETAS, LAYOUT_UNDEFINED, LAYOUT_GENERAL, ACCESS_NONE, ACCESS_SHADER_WRITE, STAGE_TOP_OF_PIPE, STAGE_COMPUTE_SHADER);
        cmd.pipeline_barrier(
            ResourceManager::HAIR_FRONT_BETAS, LAYOUT_UNDEFINED, LAYOUT_GENERAL, ACCESS_NONE, ACCESS_SHADER_WRITE, STAGE_TOP_OF_PIPE, STAGE_COMPUTE_SHADER);
        cmd.pipeline_barrier(
            ResourceManager::HAIR_FRONT_SHIFTS, LAYOUT_UNDEFINED, LAYOUT_GENERAL, ACCESS_NONE, ACCESS_SHADER_WRITE, STAGE_TOP_OF_PIPE, STAGE_COMPUTE_SHADER);
#endif
    }
    if (ResourceManager::HAIR_GI.currentLayout == LAYOUT_UNDEFINED)
    {
        cmd.pipeline_barrier(
            ResourceManager::HAIR_GI, LAYOUT_UNDEFINED, LAYOUT_GENERAL, ACCESS_NONE, ACCESS_SHADER_WRITE, STAGE_TOP_OF_PIPE, STAGE_COMPUTE_SHADER);

        auto shaderPass = m_shaderPasses[2];
        // Bind pipeline
        cmd.bind_shaderpass(*shaderPass);
        // GLOBAL LAYOUT BINDING
        cmd.bind_descriptor_set(m_descriptors[currentFrame.index].globalDescritor, 0, *shaderPass, {}, BINDING_TYPE_COMPUTE);

        // Dispatch the compute shader
        const uint32_t WORK_GROUP_SIZE = 8;
        uint32_t       gridSize        = std::max(1u, m_imageExtent.width);
        gridSize                       = (gridSize + WORK_GROUP_SIZE - 1) / WORK_GROUP_SIZE;
        cmd.dispatch_compute({gridSize, gridSize, gridSize});

        cmd.pipeline_barrier(ResourceManager::HAIR_GI,
                             LAYOUT_GENERAL,
                             LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             ACCESS_SHADER_WRITE,
                             ACCESS_SHADER_READ,
                             STAGE_COMPUTE_SHADER,
                             STAGE_FRAGMENT_SHADER);
    }

#ifdef HAIR_DISNEY
    /*
 PREPARE FOR SHADER WRITE
  */
    if (ResourceManager::HAIR_BACK_ATT.currentLayout == LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        cmd.pipeline_barrier(ResourceManager::HAIR_BACK_ATT,
                             LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             LAYOUT_GENERAL,
                             ACCESS_SHADER_READ,
                             ACCESS_SHADER_WRITE,
                             STAGE_FRAGMENT_SHADER,
                             STAGE_COMPUTE_SHADER);
        cmd.pipeline_barrier(ResourceManager::HAIR_FRONT_ATT,
                             LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             LAYOUT_GENERAL,
                             ACCESS_SHADER_READ,
                             ACCESS_SHADER_WRITE,
                             STAGE_FRAGMENT_SHADER,
                             STAGE_COMPUTE_SHADER);
        cmd.pipeline_barrier(ResourceManager::HAIR_NG,
                             LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             LAYOUT_GENERAL,
                             ACCESS_SHADER_READ,
                             ACCESS_SHADER_WRITE,
                             STAGE_FRAGMENT_SHADER,
                             STAGE_COMPUTE_SHADER);
        cmd.pipeline_barrier(ResourceManager::HAIR_NG_TRT,
                             LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             LAYOUT_GENERAL,
                             ACCESS_SHADER_READ,
                             ACCESS_SHADER_WRITE,
                             STAGE_FRAGMENT_SHADER,
                             STAGE_COMPUTE_SHADER);
        cmd.pipeline_barrier(ResourceManager::HAIR_FRONT_SHIFTS,
                             LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             LAYOUT_GENERAL,
                             ACCESS_SHADER_READ,
                             ACCESS_SHADER_WRITE,
                             STAGE_FRAGMENT_SHADER,
                             STAGE_COMPUTE_SHADER);
        cmd.pipeline_barrier(ResourceManager::HAIR_FRONT_BETAS,
                             LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             LAYOUT_GENERAL,
                             ACCESS_SHADER_READ,
                             ACCESS_SHADER_WRITE,
                             STAGE_FRAGMENT_SHADER,
                             STAGE_COMPUTE_SHADER);
        cmd.pipeline_barrier(ResourceManager::HAIR_BACK_BETAS,
                             LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             LAYOUT_GENERAL,
                             ACCESS_SHADER_READ,
                             ACCESS_SHADER_WRITE,
                             STAGE_FRAGMENT_SHADER,
                             STAGE_COMPUTE_SHADER);
        cmd.pipeline_barrier(ResourceManager::HAIR_BACK_SHIFTS,
                             LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             LAYOUT_GENERAL,
                             ACCESS_SHADER_READ,
                             ACCESS_SHADER_WRITE,
                             STAGE_FRAGMENT_SHADER,
                             STAGE_COMPUTE_SHADER);
        // cmd.pipeline_barrier(ResourceManager::HAIR_GI,
        //                      LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        //                      LAYOUT_GENERAL,
        //                      ACCESS_SHADER_READ,
        //                      ACCESS_SHADER_WRITE,
        //                      STAGE_FRAGMENT_SHADER,
        //                      STAGE_COMPUTE_SHADER);
    }

    unsigned int mesh_idx = 0;
    for (Mesh* m : scene->get_meshes())
    {
        if (m)
        {
            if (m->is_active() &&  // Check if is active
                m->get_geometry()) // Check if is inside frustrum
            {
                auto mat = m->get_material();
                // if (mat->get_type() == Core::IMaterial::Type::HAIR_STR_DISNEY_TYPE && mat->dirty())
                if (mat->get_type() == Core::IMaterial::Type::HAIR_STR_DISNEY_TYPE)
                {

                    // Offset calculation
                    uint32_t objectOffset = currentFrame.uniformBuffers[1].strideSize * mesh_idx;

                    ShaderPass* shaderPass = m_shaderPasses[3];

                    // Bind pipeline
                    cmd.bind_shaderpass(*shaderPass);
                    // GLOBAL LAYOUT BINDING
                    cmd.bind_descriptor_set(m_descriptors[currentFrame.index].globalDescritor, 0, *shaderPass, {}, BINDING_TYPE_COMPUTE);
                    // PER OBJECT LAYOUT BINDING
                    cmd.bind_descriptor_set(
                        m_descriptors[currentFrame.index].objectDescritor, 1, *shaderPass, {objectOffset, objectOffset}, BINDING_TYPE_COMPUTE);

                    // Dispatch the compute shader
                    cmd.dispatch_compute({1, 1, 1});

                    cmd.pipeline_barrier(m_normBuffer, ACCESS_SHADER_WRITE, ACCESS_SHADER_READ, STAGE_COMPUTE_SHADER, STAGE_COMPUTE_SHADER);

                    // --------------------------------------------------------------

                    shaderPass = m_shaderPasses[0];
                    // Bind pipeline
                    cmd.bind_shaderpass(*shaderPass);
                    // GLOBAL LAYOUT BINDING
                    cmd.bind_descriptor_set(m_descriptors[currentFrame.index].globalDescritor, 0, *shaderPass, {}, BINDING_TYPE_COMPUTE);
                    // PER OBJECT LAYOUT BINDING
                    cmd.bind_descriptor_set(
                        m_descriptors[currentFrame.index].objectDescritor, 1, *shaderPass, {objectOffset, objectOffset}, BINDING_TYPE_COMPUTE);

                    // Dispatch the compute shader
                    const uint32_t WORK_GROUP_SIZE = 16;
                    uint32_t       gridSize        = std::max(1u, m_imageExtent.width);
                    gridSize                       = (gridSize + WORK_GROUP_SIZE - 1) / WORK_GROUP_SIZE;
                    cmd.dispatch_compute({gridSize, 1, 1});

                    // --------------------------------------------------------------

                    shaderPass = m_shaderPasses[1];
                    // Bind pipeline
                    cmd.bind_shaderpass(*shaderPass);
                    // GLOBAL LAYOUT BINDING
                    cmd.bind_descriptor_set(m_descriptors[currentFrame.index].globalDescritor, 0, *shaderPass, {}, BINDING_TYPE_COMPUTE);
                    // PER OBJECT LAYOUT BINDING
                    cmd.bind_descriptor_set(
                        m_descriptors[currentFrame.index].objectDescritor, 1, *shaderPass, {objectOffset, objectOffset}, BINDING_TYPE_COMPUTE);

                    // Dispatch the compute shader
                    cmd.dispatch_compute({gridSize, gridSize, 1});

                    // --------------------------------------------------------------

                    cmd.pipeline_barrier(ResourceManager::HAIR_BACK_ATT,
                                         LAYOUT_GENERAL,
                                         LAYOUT_GENERAL,
                                         ACCESS_SHADER_WRITE,
                                         ACCESS_SHADER_READ,
                                         STAGE_COMPUTE_SHADER,
                                         STAGE_COMPUTE_SHADER);
                    cmd.pipeline_barrier(ResourceManager::HAIR_FRONT_ATT,
                                         LAYOUT_GENERAL,
                                         LAYOUT_GENERAL,
                                         ACCESS_SHADER_WRITE,
                                         ACCESS_SHADER_READ,
                                         STAGE_COMPUTE_SHADER,
                                         STAGE_COMPUTE_SHADER);
                    cmd.pipeline_barrier(ResourceManager::HAIR_BACK_BETAS,
                                         LAYOUT_GENERAL,
                                         LAYOUT_GENERAL,
                                         ACCESS_SHADER_WRITE,
                                         ACCESS_SHADER_READ,
                                         STAGE_COMPUTE_SHADER,
                                         STAGE_COMPUTE_SHADER);
                    cmd.pipeline_barrier(ResourceManager::HAIR_FRONT_BETAS,
                                         LAYOUT_GENERAL,
                                         LAYOUT_GENERAL,
                                         ACCESS_SHADER_WRITE,
                                         ACCESS_SHADER_READ,
                                         STAGE_COMPUTE_SHADER,
                                         STAGE_COMPUTE_SHADER);

                    shaderPass = m_shaderPasses[2];
                    // Bind pipeline
                    cmd.bind_shaderpass(*shaderPass);
                    // GLOBAL LAYOUT BINDING
                    cmd.bind_descriptor_set(m_descriptors[currentFrame.index].globalDescritor, 0, *shaderPass, {}, BINDING_TYPE_COMPUTE);

                    // Dispatch the compute shader
                    const uint32_t WORK_GROUP_SIZE_2 = 8;
                    uint32_t       gridSize2         = std::max(1u, m_imageExtent.width);
                    gridSize2                        = (gridSize2 + WORK_GROUP_SIZE_2 - 1) / WORK_GROUP_SIZE_2;
                    uint32_t gridSize3               = 32;
                    gridSize3                        = (gridSize3 + WORK_GROUP_SIZE_2 - 1) / WORK_GROUP_SIZE_2;
                    cmd.dispatch_compute({gridSize2, gridSize2, gridSize3});

                    mat->dirty(false);

                    break; // WIP for now compute just one hair volume AND EXIT LOOP
                }
            }
        }
        mesh_idx++;
    }

    /*
      PREPARE FOR SHADER READ
       */
    cmd.pipeline_barrier(ResourceManager::HAIR_BACK_ATT,
                         LAYOUT_GENERAL,
                         LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         ACCESS_SHADER_READ,
                         ACCESS_SHADER_READ,
                         STAGE_COMPUTE_SHADER,
                         STAGE_FRAGMENT_SHADER);
    cmd.pipeline_barrier(ResourceManager::HAIR_FRONT_ATT,
                         LAYOUT_GENERAL,
                         LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         ACCESS_SHADER_READ,
                         ACCESS_SHADER_READ,
                         STAGE_COMPUTE_SHADER,
                         STAGE_FRAGMENT_SHADER);
    cmd.pipeline_barrier(ResourceManager::HAIR_NG,
                         LAYOUT_GENERAL,
                         LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         ACCESS_SHADER_WRITE,
                         ACCESS_SHADER_READ,
                         STAGE_COMPUTE_SHADER,
                         STAGE_FRAGMENT_SHADER);
    cmd.pipeline_barrier(ResourceManager::HAIR_NG_TRT,
                         LAYOUT_GENERAL,
                         LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         ACCESS_SHADER_WRITE,
                         ACCESS_SHADER_READ,
                         STAGE_COMPUTE_SHADER,
                         STAGE_FRAGMENT_SHADER);
    cmd.pipeline_barrier(ResourceManager::HAIR_BACK_SHIFTS,
                         LAYOUT_GENERAL,
                         LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         ACCESS_SHADER_WRITE,
                         ACCESS_SHADER_READ,
                         STAGE_COMPUTE_SHADER,
                         STAGE_FRAGMENT_SHADER);
    cmd.pipeline_barrier(ResourceManager::HAIR_FRONT_SHIFTS,
                         LAYOUT_GENERAL,
                         LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         ACCESS_SHADER_WRITE,
                         ACCESS_SHADER_READ,
                         STAGE_COMPUTE_SHADER,
                         STAGE_FRAGMENT_SHADER);
    cmd.pipeline_barrier(ResourceManager::HAIR_FRONT_BETAS,
                         LAYOUT_GENERAL,
                         LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         ACCESS_SHADER_READ,
                         ACCESS_SHADER_READ,
                         STAGE_COMPUTE_SHADER,
                         STAGE_FRAGMENT_SHADER);
    cmd.pipeline_barrier(ResourceManager::HAIR_BACK_BETAS,
                         LAYOUT_GENERAL,
                         LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         ACCESS_SHADER_READ,
                         ACCESS_SHADER_READ,
                         STAGE_COMPUTE_SHADER,
                         STAGE_FRAGMENT_SHADER);
    // cmd.pipeline_barrier(ResourceManager::HAIR_GI,
    //                      LAYOUT_GENERAL,
    //                      LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    //                      ACCESS_SHADER_WRITE,
    //                      ACCESS_SHADER_READ,
    //                      STAGE_COMPUTE_SHADER,
    //                      STAGE_FRAGMENT_SHADER);

#endif
}

void HairScatteringPass::cleanup() {
    ComputePass::cleanup();
    ResourceManager::HAIR_BACK_ATT.cleanup();
    ResourceManager::HAIR_FRONT_ATT.cleanup();
    ResourceManager::HAIR_NG.cleanup();
    ResourceManager::HAIR_NG_TRT.cleanup();
    ResourceManager::HAIR_BACK_SHIFTS.cleanup();
    ResourceManager::HAIR_FRONT_SHIFTS.cleanup();
    ResourceManager::HAIR_BACK_BETAS.cleanup();
    ResourceManager::HAIR_FRONT_BETAS.cleanup();
    ResourceManager::HAIR_GI.cleanup();
    m_normBuffer.cleanup();
}

} // namespace Core

VULKAN_ENGINE_NAMESPACE_END