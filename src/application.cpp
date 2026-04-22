#include "application.h"
#include <filesystem>

// #define USE_NEURAL_MODELS
#define USE_GLB_MODELS
#define LOAD_ALEX

void HairViewer::init(Systems::RendererSettings settings) {
    m_window = new WindowGLFW("Hair Viewer", 1024, 1024);

    m_window->init();
    m_window->set_window_icon(RESOURCES_PATH "textures/icon.png");

    m_window->set_window_size_callback(std::bind(&HairViewer::window_resize_callback, this, std::placeholders::_1, std::placeholders::_2));
    m_window->set_mouse_callback(std::bind(&HairViewer::mouse_callback, this, std::placeholders::_1, std::placeholders::_2));
    m_window->set_key_callback(
        std::bind(&HairViewer::keyboard_callback, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));

    settings.clearColor = Vec4(0.0, 0.0, 0.0, 1.0);
    m_renderer          = new Systems::ForwardRenderer(m_window, ShadowResolution::HIGH, settings);

    setup();

    m_interface.init(m_window, m_scene, m_renderer, &animateLight);
    // m_renderer->set_gui_overlay(m_interface.overlay);
}

void HairViewer::run(Systems::RendererSettings settings) {

    init(settings);
    while (!m_window->get_window_should_close())
    {
        // I-O
        m_window->poll_events();

        tick();

        if (m_maxFrames > 0 && ++m_frameCount >= m_maxFrames)
            break;
    }
    m_renderer->shutdown(m_scene);
}

void HairViewer::setup() {
    const std::string MESH_PATH(RESOURCES_PATH "models/");
    const std::string TEXTURE_PATH(RESOURCES_PATH "textures/");
    const std::string ENGINE_MESH_PATH(ENGINE_RESOURCES_PATH "meshes/");

    camera = new Camera();
    camera->set_position(Vec3(0.0f, 0.0f, -16.0f));
    camera->set_far(100.0f);
    camera->set_near(0.1f);
    camera->set_field_of_view(40.0f);

    m_scene = new Scene(camera);

    PointLight* light = new PointLight();
    light->set_position({-5.0f, 1.0f, -5.0f});
    light->set_shadow_fov(120.0f);
    light->set_intensity(1.0f);
    light->set_shadow_bias(0.0002f);
    light->set_shadow_near(0.1f);
    light->set_area_of_effect(30.0f);
    light->set_name("PointLight");

    Mesh* lightDummy = new Mesh();
    Tools::Loaders::load_3D_file(lightDummy, ENGINE_MESH_PATH + "sphere.obj", false);
    lightDummy->push_material(new UnlitMaterial());
    lightDummy->cast_shadows(false);
    lightDummy->set_name("LightDummy");
    light->add_child(lightDummy);

    m_scene->add(light);

#ifdef USE_GLB_MODELS

    // ALEX
    #ifdef LOAD_ALEX
    Mesh* character0 = new Mesh();
    std::vector<Texture*> glbTextures0;
    Tools::Loaders::load_GLB(character0, MESH_PATH + "alex/alex.glb", 0, &glbTextures0);
    character0->set_position({0.0f, -12.6f, 0.2f});
    character0->set_scale(10.0f);
    character0->set_rotation({0.0f, 180.0f, 0.0f});
    auto char0Mat = new PhysicallyBasedMaterial();
    if (!glbTextures0.empty())
        char0Mat->set_albedo_texture(glbTextures0[0]);
    char0Mat->set_albedo(Vec3(204.0f, 123.0f, 85.0f) / 255.0f);
    char0Mat->set_metalness(0.0f);
    char0Mat->set_roughness(0.5f);
    character0->push_material(char0Mat);
    character0->set_name("Alex");
    m_scene->add(character0);

    // Debug: list morph targets and joints so test JSONs can use real names.
    if (Geometry* g = character0->get_geometry(0)) {
        const auto& props = g->get_properties();

        if (props.morphTargetData.has_value()) {
            LOG_DEBUG("Alex morph targets:");
            for (const auto& name : props.morphTargetData->targetNames)
                LOG_DEBUG("  morph: " + name);
        }

        if (props.skinData.has_value()) {
            LOG_DEBUG("Alex joints:");
            const auto& skin = *props.skinData;
            for (size_t j = 0; j < skin.jointNames.size(); ++j) {
                std::string parentName = (skin.parentIndices[j] >= 0)
                    ? skin.jointNames[skin.parentIndices[j]]
                    : "ROOT";
                LOG_DEBUG("  joint[" + std::to_string(j) + "] " + skin.jointNames[j] +
                          "  parent=" + parentName);
            }
        }

        // Load test animation (covers both morph targets and skeletal skinning).
        try {
            static const SkinData        emptySkin;
            static const MorphTargetData emptyMorphs;
            const SkinData*        skin   = props.skinData.has_value()       ? &*props.skinData       : &emptySkin;
            const MorphTargetData* morphs = props.morphTargetData.has_value() ? &*props.morphTargetData : &emptyMorphs;
            Animation anim = load_animation_json(
                RESOURCES_PATH "animations/test_anim.json",
                *skin, *morphs);
            character0->set_animation(std::make_unique<Animation>(std::move(anim)));
            LOG_DEBUG("Test animation loaded for Alex.");
        } catch (const std::exception& e) {
            LOG_ERROR(std::string("Animation load failed: ") + e.what());
        }
    }

    Vec3 hairCards0_offset = Vec3{0.0f, 0.8f, 0.2f};
    Mesh* hairCards0 = new Mesh();
    Tools::Loaders::load_3D_file(hairCards0, MESH_PATH + "alex/hair_fauxmohawk.obj", false);
    hairCards0->set_position(character0->get_position() + hairCards0_offset);
    hairCards0->set_scale(0.1f);
    hairCards0->set_rotation({0.0f, 180.0f, 0.0f});
    HairCardMaterial* hc0Mat = new HairCardMaterial();
    hc0Mat->set_hair_color(Vec3(0.05f, 0.02f, 0.01f));
    Texture* hair0DataTex = new Texture();
    Tools::Loaders::load_texture(hair0DataTex, TEXTURE_PATH + "alex/hair_fauxmohawk_attribute.png", TEXTURE_FORMAT_TYPE_NORMAL);
    hc0Mat->set_hair_data_texture(hair0DataTex);
    Texture* hair0TangentTex = new Texture();
    Tools::Loaders::load_texture(hair0TangentTex, TEXTURE_PATH + "alex/hair_fauxmohawk_tangent.png", TEXTURE_FORMAT_TYPE_NORMAL);
    hc0Mat->set_tangent_texture(hair0TangentTex);
    hairCards0->push_material(hc0Mat);
    hairCards0->set_name("HairCards0");
    m_scene->add(hairCards0);

    // JAVI
    #elif defined(LOAD_JAVI)
    Mesh* character1 = new Mesh();
    std::vector<Texture*> glbTextures1;
    Tools::Loaders::load_GLB(character1, MESH_PATH + "javi/javi.glb", 0, &glbTextures1);
    character1->set_position({0.0f, -12.6f, 0.2f});
    character1->set_scale(10.0f);
    character1->set_rotation({0.0f, 180.0f, 0.0f});
    auto char1Mat = new PhysicallyBasedMaterial();
    if (!glbTextures1.empty())
        char1Mat->set_albedo_texture(glbTextures1[0]);
    char1Mat->set_albedo(Vec3(204.0f, 123.0f, 85.0f) / 255.0f);
    char1Mat->set_metalness(0.0f);
    char1Mat->set_roughness(0.5f);
    character1->push_material(char1Mat);
    character1->set_name("Javi");
    m_scene->add(character1);

    Vec3  hairCards1_offset = Vec3{0.0f, -0.55f, 0.1f};
    Mesh* hairCards1 = new Mesh();
    Tools::Loaders::load_3D_file(hairCards1, MESH_PATH + "javi/hair_brushcut.obj", false);
    hairCards1->set_position(character1->get_position() + hairCards1_offset);
    hairCards1->set_scale(0.1f);
    hairCards1->set_rotation({0.0f, 180.0f, 0.0f});
    HairCardMaterial* hc1Mat = new HairCardMaterial();
    hc1Mat->set_hair_color(Vec3(0.05f, 0.02f, 0.01f));
    Texture* hair1DataTex = new Texture();
    Tools::Loaders::load_texture(hair1DataTex, TEXTURE_PATH + "javi/hair_brushcut_attribute.png", TEXTURE_FORMAT_TYPE_NORMAL);
    hc1Mat->set_hair_data_texture(hair1DataTex);
    Texture* hair1TangentTex = new Texture();
    Tools::Loaders::load_texture(hair1TangentTex, TEXTURE_PATH + "javi/hair_brushcut_tangent.png", TEXTURE_FORMAT_TYPE_NORMAL);
    hc1Mat->set_tangent_texture(hair1TangentTex);
    hairCards1->push_material(hc1Mat);
    hairCards1->set_name("HairCards1");
    m_scene->add(hairCards1);

    // MARIA
    #elif defined(LOAD_MARIA)
    Mesh* character2 = new Mesh();
    std::vector<Texture*> glbTextures2;
    Tools::Loaders::load_GLB(character2, MESH_PATH + "maria/maria.glb", 0, &glbTextures2);
    character2->set_position({0.0f, -12.6f, 0.2f});
    character2->set_scale(10.0f);
    character2->set_rotation({0.0f, 180.0f, 0.0f});
    auto char2Mat = new PhysicallyBasedMaterial();
    if (!glbTextures2.empty())
        char2Mat->set_albedo_texture(glbTextures2[0]);
    char2Mat->set_albedo(Vec3(204.0f, 123.0f, 85.0f) / 255.0f);
    char2Mat->set_metalness(0.0f);
    char2Mat->set_roughness(0.5f);
    character2->push_material(char2Mat);
    character2->set_name("Maria");
    m_scene->add(character2);

    Vec3  hairCards2_offset = Vec3{0.0f, 1.0f, -0.1f};
    Mesh* hairCards2 = new Mesh();
    Tools::Loaders::load_3D_file(hairCards2, MESH_PATH + "maria/hair_bobmessy.obj", false);
    hairCards2->set_position(character2->get_position() + hairCards2_offset);
    hairCards2->set_scale(0.1f);
    hairCards2->set_rotation({0.0f, 180.0f, 0.0f});
    HairCardMaterial* hc2Mat = new HairCardMaterial();
    hc2Mat->set_hair_color(Vec3(0.05f, 0.02f, 0.01f));
    Texture* hair2DataTex = new Texture();
    Tools::Loaders::load_texture(hair2DataTex, TEXTURE_PATH + "maria/hair_bobmessy_attribute.png", TEXTURE_FORMAT_TYPE_NORMAL);
    hc2Mat->set_hair_data_texture(hair2DataTex);
    Texture* hair2TangentTex = new Texture();
    Tools::Loaders::load_texture(hair2TangentTex, TEXTURE_PATH + "maria/hair_bobmessy_tangent.png", TEXTURE_FORMAT_TYPE_NORMAL);
    hc2Mat->set_tangent_texture(hair2TangentTex);
    hairCards2->push_material(hc2Mat);
    hairCards2->set_name("HairCards2");
    m_scene->add(hairCards2);

    // NADIA
    #elif defined(LOAD_NADIA)
    Mesh* character3 = new Mesh();
    std::vector<Texture*> glbTextures3;
    Tools::Loaders::load_GLB(character3, MESH_PATH + "nadia/nadia.glb", 0, &glbTextures3);
    character3->set_position({0.0f, -12.6f, 0.2f});
    character3->set_scale(10.0f);
    character3->set_rotation({0.0f, 180.0f, 0.0f});
    auto char3Mat = new PhysicallyBasedMaterial();
    if (!glbTextures3.empty())
        char3Mat->set_albedo_texture(glbTextures3[0]);
    char3Mat->set_albedo(Vec3(204.0f, 123.0f, 85.0f) / 255.0f);
    char3Mat->set_metalness(0.0f);
    char3Mat->set_roughness(0.5f);
    character3->push_material(char3Mat);
    character3->set_name("Maria");
    m_scene->add(character3);

    Vec3 hairCards3_offset = Vec3{0.0f, 0.4f, -0.1f};
    Mesh* hairCards3 = new Mesh();
    Tools::Loaders::load_3D_file(hairCards3, MESH_PATH + "nadia/hair_afrocurly.obj", false);
    hairCards3->set_position(character3->get_position() + hairCards3_offset);
    hairCards3->set_scale(0.1f);
    hairCards3->set_rotation({0.0f, 180.0f, 0.0f});
    HairCardMaterial* hc3Mat = new HairCardMaterial();
    hc3Mat->set_hair_color(Vec3(0.05f, 0.02f, 0.01f));
    Texture* hair3DataTex = new Texture();
    Tools::Loaders::load_texture(hair3DataTex, TEXTURE_PATH + "nadia/hair_afrocurly_attribute.png", TEXTURE_FORMAT_TYPE_NORMAL);
    hc3Mat->set_hair_data_texture(hair3DataTex);
    Texture* hair3TangentTex = new Texture();
    Tools::Loaders::load_texture(hair3TangentTex, TEXTURE_PATH + "nadia/hair_afrocurly_tangent.png", TEXTURE_FORMAT_TYPE_NORMAL);
    hc3Mat->set_tangent_texture(hair3TangentTex);
    hairCards3->push_material(hc3Mat);
    hairCards3->set_name("HairCards3");
    m_scene->add(hairCards3);
#endif
#elif defined(USE_NEURAL_MODELS)
    // load_neural_avatar(
    //     RESOURCES_PATH "models/neural_hair_PABLO.ply", RESOURCES_PATH "models/neural_head_PABLO.ply", "Pablo", {0.32, 0.12, 1.0}, Vec3(0.0), -175.0f);
    // load_neural_avatar(RESOURCES_PATH "models/neural_hair_TONY.ply",
    //                    RESOURCES_PATH "models/neural_head_TONY.ply",
    //                    "Alvaro",
    //                    {0.8, 0.2, 4.0},
    //                    {-5.5f, 0.1f, -0.4f},
    //                    -35.0f);
    load_neural_avatar(RESOURCES_PATH "models/neural_hair_TONO.ply",
                       RESOURCES_PATH "models/neural_head_TONO.ply",
                       "Antonio",
                       {0.4, 0.2, 24.0},
                       //    {5.5f, 0.0f, 0.0f},
                       {0.0f, 0.0f, 0.0f},
                       -320.0f,
                       true);
    //    {9, 6, 3}
#else
    Mesh* hair = new Mesh();
    Tools::Loaders::load_3D_file(hair, MESH_PATH + "straight.hair", false);
	hair->set_position({0.0f, 2.9f, -0.2f});
    // hair->set_scale(0.053f);
    hair->set_scale(0.025f);
    hair->set_rotation({90.0, 180.0f, -90.0f});
    // HairDisneyMaterial* hmat = new HairDisneyMaterial();
    HairEpicMaterial* hmat = new HairEpicMaterial();
    hmat->set_thickness(0.0015f);
    hair->push_material(hmat);
    hair->set_name("Hair");

    // Mesh* hair2 = new Mesh();
    // Tools::Loaders::load_3D_file(hair2, MESH_PATH + "curly.hair", false);
    // hair2->set_scale(0.053f);
    // hair2->set_rotation({90.0, 180.0f, 0.0f});
    // HairMaterial* hmat2 = new HairMaterial(0.8);
    // hmat2->set_thickness(0.0025f);
    // hair2->push_material(hmat2);
    // hair2->set_name("Hair Curly");
    // hair2->set_active(false);

    Mesh* human = new Mesh();
    // Tools::Loaders::load_3D_file(human , MESH_PATH + "head.ply", false);
    Tools::Loaders::load_3D_file(human, MESH_PATH + "bust.obj", false);
	// human ->set_scale(0.053f);
    human->set_scale(10.0f);
    // human ->set_rotation({90.0, 180.0f, -90.0f});
    human->set_rotation({0.0, 180.0f, 0.0f});
    auto humanMat        = new PhysicallyBasedMaterial();
    Texture* humanAlbedo = new Texture();
    Texture* humanNormal = new Texture();
    Tools::Loaders::load_texture(humanAlbedo, TEXTURE_PATH + "albedo.png");
    Tools::Loaders::load_texture(humanNormal, TEXTURE_PATH + "normal.png", TEXTURE_FORMAT_TYPE_NORMAL);
    humanMat->set_albedo_texture(humanAlbedo);
    humanMat->set_normal_texture(humanNormal);
    humanMat->set_albedo(Vec3(204.0f, 123.0f, 85.0f) / 255.0f);
    humanMat->set_albedo_weight(0.75f);
    humanMat->set_metalness(0.0f);
    humanMat->set_roughness(0.5f);
    human->push_material(humanMat);
    human->set_name("Human");
    // Mesh* eyes = new Mesh();
    // Tools::Loaders::load_3D_file(eyes, MESH_PATH + "eyes.ply");
    // auto eyesMat = new PhysicallyBasedMaterial();
    // eyes->push_material(eyesMat);
    // Texture* eyesAlbedo = new Texture();
    // Tools::Loaders::load_texture(eyesAlbedo, TEXTURE_PATH + "eye.png");
    // eyesMat->set_albedo_texture(eyesAlbedo);
    // eyesMat->set_metalness(0.0f);
    // eyesMat->set_roughness(0.1f);
    // eyes->set_name("Eyes");
    // alex->add_child(eyes);

    // hmat->set_skull(head);
    // head->add_child(hair);
    m_scene->add(human);
    m_scene->add(hair);
    // m_scene->add(hair2);
#endif

    m_scene->set_ambient_color({0.05, 0.05, 0.05});
    m_scene->set_ambient_intensity(0.0f);

    TextureHDR* envMap = new TextureHDR();
    Tools::Loaders::load_HDRi(envMap, TEXTURE_PATH + "room.hdr");
    Skybox* sky = new Skybox(envMap);
    sky->set_color_intensity(0.1);
    m_scene->set_skybox(sky);
    // sky->set_active(false);
    m_scene->set_use_IBL(false);

    m_scene->enable_fog(false);

    m_controller = new Tools::Controller(camera, m_window, ControllerMovementType::ORBITAL);

    static_cast<Systems::ForwardRenderer*>(m_renderer)->load_sss_scatter_lut(TEXTURE_PATH + "scatterDistance.png");
}

void HairViewer::update() {
    if (!m_interface.overlay->wants_to_handle_input())
        m_controller->handle_keyboard(0, 0, m_time.delta);

    for (Mesh* mesh : m_scene->get_meshes())
        mesh->advance_animation(m_time.delta);

    // Rotate the vector around the ZX plane
    auto light = m_scene->get_lights()[0];
    if (animateLight)
    {
        float rotationAngle = glm::radians(10.0f * m_time.delta);
        float _x            = light->get_position().x * cos(rotationAngle) - light->get_position().z * sin(rotationAngle);
        float _z            = light->get_position().x * sin(rotationAngle) + light->get_position().z * cos(rotationAngle);

        light->set_position({_x, light->get_position().y, _z});
        static_cast<UnlitMaterial*>(static_cast<Mesh*>(light->get_children().front())->get_material(0))->set_color({light->get_color() * 4.0f, 1.0f});
    }

    m_interface.objectWidget->set_object(m_interface.sceneWidget->get_selected_object());
}

void HairViewer::tick() {
    float currentTime      = (float)m_window->get_time_elapsed();
    m_time.delta           = currentTime - m_time.last;
    m_time.last            = currentTime;
    m_time.framesPerSecond = 1.0f / m_time.delta;

    update();

    m_interface.overlay->render();
    m_renderer->render(m_scene);
}
void HairViewer::load_neural_avatar(const char* hairFile,
                                    const char* headFile,
                                    const char* objName,
                                    math::vec3  hairColor,
                                    Vec3        position,
                                    float       rotation,
                                    bool        active) {

    Mesh*       hair = new Mesh();
    std::thread loadThread1(hair_loaders::load_neural_hair, hair, hairFile, nullptr, true, false, false, false);
    loadThread1.detach();

    HairEpicMaterial* hmat = new HairEpicMaterial();
    hair->push_material(hmat);
    hair->set_name(std::string(objName) + " hair");

    const std::string HEAD_PATH(headFile);
    Mesh*             head = new Mesh();
    Tools::Loaders::load_3D_file(head, HEAD_PATH);

    // Transform
    head->set_position(position);
    head->set_scale(3.f);
    head->set_rotation({-90.0, 0.0f, 215.0f + rotation}); // Correct blender axis

    auto headMat = new PhysicallyBasedMaterial();
    headMat->set_albedo(Vec3(204.0f, 123.0f, 85.0f) / 255.0f);
    headMat->set_metalness(0.0f);
    headMat->set_roughness(0.55f);
    head->push_material(headMat);
    head->set_name(std::string(objName) + " head");

    head->add_child(hair);
    m_scene->add(head);
    head->set_active(active);
}
