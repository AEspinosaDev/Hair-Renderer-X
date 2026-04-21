#include <fstream>
#include <stb_image.h>                  // declarations — implementation lives in the engine's stb_image library

#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_INCLUDE_STB_IMAGE       // already included above
#define TINYGLTF_NO_STB_IMAGE_WRITE         // we don't write images
#define TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE
#include <tiny_gltf.h>

#include <glm/gtc/type_ptr.hpp>
#include <engine/tools/loaders.h>

void VKFW::Tools::Loaders::load_OBJ(Core::Mesh* const mesh, const std::string fileName, bool importMaterials, bool calculateTangents, bool overrideGeometry) {
    // std::this_thread::sleep_for(std::chrono::seconds(4)); //Debuging

    // Preparing output
    tinyobj::attrib_t                attrib;
    std::vector<tinyobj::shape_t>    shapes;
    std::vector<tinyobj::material_t> materials;
    std::string                      warn;
    std::string                      err;

    tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, fileName.c_str(), importMaterials ? nullptr : nullptr);

    // Check for errors
    if (!warn.empty())
    {
        LOG_DEBUG("WARN: " + warn);
    }
    if (!err.empty())
    {
        LOG_ERROR(err);
        LOG_DEBUG("ERROR: Couldn't load mesh");
        return;
    }

    size_t shape_id = 0;
    for (const tinyobj::shape_t& shape : shapes)
    {
        std::vector<Graphics::Vertex>                  vertices;
        std::vector<uint32_t>                          indices;
        std::unordered_map<Graphics::Vertex, uint32_t> uniqueVertices;
        if (!shape.mesh.indices.empty())
        {
            // IS INDEXED
            for (const tinyobj::index_t& index : shape.mesh.indices)
            {
                Graphics::Vertex vertex = {};

                // Position and color
                if (index.vertex_index >= 0)
                {
                    vertex.pos.x = attrib.vertices[3 * index.vertex_index + 0];
                    vertex.pos.y = attrib.vertices[3 * index.vertex_index + 1];
                    vertex.pos.z = attrib.vertices[3 * index.vertex_index + 2];

                    vertex.color.r = attrib.colors[3 + index.vertex_index + 0];
                    vertex.color.g = attrib.colors[3 + index.vertex_index + 1];
                    vertex.color.b = attrib.colors[3 + index.vertex_index + 2];
                }
                // Normal
                if (index.normal_index >= 0)
                {
                    vertex.normal.x = attrib.normals[3 * index.normal_index + 0];
                    vertex.normal.y = attrib.normals[3 * index.normal_index + 1];
                    vertex.normal.z = attrib.normals[3 * index.normal_index + 2];
                }

                // Tangent
                vertex.tangent = {0.0, 0.0, 0.0};

                // UV
                if (index.texcoord_index >= 0)
                {
                    vertex.texCoord.x = attrib.texcoords[2 * index.texcoord_index + 0];
                    vertex.texCoord.y = attrib.texcoords[2 * index.texcoord_index + 1];
                }

                // Check if the vertex is already in the map
                if (uniqueVertices.count(vertex) == 0)
                {
                    uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
                    vertices.push_back(vertex);
                }

                indices.push_back(uniqueVertices[vertex]);
            }
        } else
            // NOT INDEXED
            for (size_t i = 0; i < shape.mesh.num_face_vertices.size(); i++)
            {
                for (size_t j = 0; j < shape.mesh.num_face_vertices[i]; j++)
                {
                    Graphics::Vertex vertex{};
                    size_t           vertex_index = shape.mesh.indices[i * shape.mesh.num_face_vertices[i] + j].vertex_index;
                    // Pos
                    if (!attrib.vertices.empty())
                    {
                        vertex.pos.x = attrib.vertices[3 * vertex_index + 0];
                        vertex.pos.y = attrib.vertices[3 * vertex_index + 1];
                        vertex.pos.z = attrib.vertices[3 * vertex_index + 2];
                    }
                    // Normals
                    if (!attrib.normals.empty())
                    {
                        vertex.normal.x = attrib.normals[3 * vertex_index + 0];
                        vertex.normal.y = attrib.normals[3 * vertex_index + 1];
                        vertex.normal.z = attrib.normals[3 * vertex_index + 2];
                    }
                    // Tangents

                    vertex.tangent = {0.0, 0.0, 0.0};

                    // UV
                    if (!attrib.texcoords.empty())
                    {
                        vertex.texCoord.x = attrib.texcoords[2 * vertex_index + 0];
                        vertex.texCoord.y = attrib.texcoords[2 * vertex_index + 1];
                    }
                    // COLORS
                    if (!attrib.colors.empty())
                    {
                        vertex.color.r = attrib.colors[3 * vertex_index + 0];
                        vertex.color.g = attrib.colors[3 * vertex_index + 1];
                        vertex.color.b = attrib.colors[3 * vertex_index + 2];
                    }

                    vertices.push_back(vertex);
                }
            }

        if (calculateTangents)
        {
            compute_tangents_gram_smidt(vertices, indices);
        }

        // if (overrideGeometry)
        // {
        //     Core::Geometry* oldGeom = mesh->get_geometry(shape_id);
        //     if (oldGeom)
        //     {
        //         oldGeom->fill(vertices, indices);
        //         continue;
        //     }
        // }

        Core::Geometry* g = new Core::Geometry();
        g->fill(vertices, indices);
        mesh->push_geometry(g);

        shape_id++;
    }
    mesh->set_file_route(fileName);
    return;
}

void VKFW::Tools::Loaders::load_PLY(Core::Mesh* const mesh,
                                    const std::string fileName,
                                    bool              preload,
                                    bool              verbose,
                                    bool              calculateTangents,
                                    bool              overrideGeometry) {

    std::unique_ptr<std::istream> file_stream;
    std::vector<uint8_t>          byte_buffer;
    try
    {
        // For most files < 1gb, pre-loading the entire file upfront and wrapping it into a
        // stream is a net win for parsing speed, about 40% faster.
        if (preload)
        {
            byte_buffer = Graphics::Utils::read_file_binary(fileName);
            file_stream.reset(new Graphics::Utils::memory_stream((char*)byte_buffer.data(), byte_buffer.size()));
        } else
        {
            file_stream.reset(new std::ifstream(fileName, std::ios::binary));
        }

        if (!file_stream || file_stream->fail())
            throw std::runtime_error("file_stream failed to open " + fileName);

        file_stream->seekg(0, std::ios::end);
        const float size_mb = file_stream->tellg() * float(1e-6);
        file_stream->seekg(0, std::ios::beg);

        tinyply::PlyFile file;
        file.parse_header(*file_stream);

        if (verbose)
        {
            std::cout << "\t[ply_header] Type: " << (file.is_binary_file() ? "binary" : "ascii") << std::endl;
            for (const auto& c : file.get_comments())
                std::cout << "\t[ply_header] Comment: " << c << std::endl;
            for (const auto& c : file.get_info())
                std::cout << "\t[ply_header] Info: " << c << std::endl;

            for (const auto& e : file.get_elements())
            {
                std::cout << "\t[ply_header] element: " << e.name << " (" << e.size << ")" << std::endl;
                for (const auto& p : e.properties)
                {
                    std::cout << "\t[ply_header] \tproperty: " << p.name << " (type=" << tinyply::PropertyTable[p.propertyType].str << ")";
                    if (p.isList)
                        std::cout << " (list_type=" << tinyply::PropertyTable[p.listType].str << ")";
                    std::cout << std::endl;
                }
            }
        }
        // Because most people have their own mesh types, tinyply treats parsed data as structured/typed byte buffers.
        std::shared_ptr<tinyply::PlyData> positions, normals, colors, texcoords, faces, tripstrip;

        // // The header information can be used to programmatically extract properties on elements
        // // known to exist in the header prior to reading the data. For brevity of this sample, properties
        // // like vertex position are hard-coded:
        try
        { positions = file.request_properties_from_element("vertex", {"x", "y", "z"}); } catch (const std::exception& e)
        { std::cerr << "tinyply exception: " << e.what() << std::endl; }

        try
        { normals = file.request_properties_from_element("vertex", {"nx", "ny", "nz"}); } catch (const std::exception& e)
        {
            if (verbose)
                std::cerr << "tinyply exception: " << e.what() << std::endl;
        }

        try
        { colors = file.request_properties_from_element("vertex", {"red", "green", "blue", "alpha"}); } catch (const std::exception& e)
        {
            if (verbose)
                std::cerr << "tinyply exception: " << e.what() << std::endl;
        }

        try
        { colors = file.request_properties_from_element("vertex", {"r", "g", "b", "a"}); } catch (const std::exception& e)
        {
            if (verbose)
                std::cerr << "tinyply exception: " << e.what() << std::endl;
        }

        try
        { texcoords = file.request_properties_from_element("vertex", {"u", "v"}); } catch (const std::exception& e)
        {
            if (verbose)
                std::cerr << "tinyply exception: " << e.what() << std::endl;
        }

        try
        { texcoords = file.request_properties_from_element("vertex", {"s", "t"}); } catch (const std::exception& e)
        {
            if (verbose)
                std::cerr << "tinyply exception: " << e.what() << std::endl;
        }

        // Providing a list size hint (the last argument) is a 2x performance improvement. If you have
        // arbitrary ply files, it is best to leave this 0.
        try
        { faces = file.request_properties_from_element("face", {"vertex_indices"}, 3); } catch (const std::exception& e)
        {
            if (verbose)
                std::cerr << "tinyply exception: " << e.what() << std::endl;
        }

        // Tristrips must always be read with a 0 list size hint (unless you know exactly how many elements
        // are specifically in the file, which is unlikely);
        try
        { tripstrip = file.request_properties_from_element("tristrips", {"vertex_indices"}, 0); } catch (const std::exception& e)
        {
            if (verbose)
                std::cerr << "tinyply exception: " << e.what() << std::endl;
        }
        Graphics::Utils::ManualTimer readTimer;

        readTimer.start();
        file.read(*file_stream);
        readTimer.stop();

        if (verbose)
        {

            const float parsingTime = static_cast<float>(readTimer.get()) / 1000.f;
            std::cout << "\tparsing " << size_mb << "mb in " << parsingTime << " seconds [" << (size_mb / parsingTime) << " MBps]" << std::endl;

            if (positions)
                std::cout << "\tRead " << positions->count << " total vertices " << std::endl;
            if (normals)
                std::cout << "\tRead " << normals->count << " total vertex normals " << std::endl;
            if (colors)
                std::cout << "\tRead " << colors->count << " total vertex colors " << std::endl;
            if (texcoords)
                std::cout << "\tRead " << texcoords->count << " total vertex texcoords " << std::endl;
            if (faces)
                std::cout << "\tRead " << faces->count << " total faces (triangles) " << std::endl;
            if (tripstrip)
                std::cout << "\tRead " << (tripstrip->buffer.size_bytes() / tinyply::PropertyTable[tripstrip->t].stride) << " total indices (tristrip) "
                          << std::endl;
        }

        std::vector<Graphics::Vertex> vertices;
        std::vector<uint32_t>         indices;

        if (positions)
        {
            const float* posData = reinterpret_cast<const float*>(positions->buffer.get());
            const float* normalData;
            const float* colorData;
            const float* uvData;
            if (normals)
                normalData = reinterpret_cast<const float*>(normals->buffer.get());
            if (colors)
                colorData = reinterpret_cast<const float*>(colors->buffer.get());
            if (texcoords)
                uvData = reinterpret_cast<const float*>(texcoords->buffer.get());

            for (size_t i = 0; i < positions->count; i++)
            {

                Vec3 position = Vec3(posData[i * 3], posData[i * 3 + 1], posData[i * 3 + 2]);
                Vec3 normal   = normals ? Vec3(normalData[i * 3], normalData[i * 3 + 1], normalData[i * 3 + 2]) : Vec3(0.0f);
                Vec3 color    = colors ? Vec3(colorData[i * 3], colorData[i * 3 + 1], colorData[i * 3 + 2]) : Vec3(1.0f);
                Vec2 uv       = texcoords ? Vec2(uvData[i * 2], uvData[i * 2 + 1]) : Vec2(0.0f);

                vertices.push_back({position, normal, {0.0f, 0.0f, 0.0f}, uv, color});
            }
        }
        unsigned* facesData = reinterpret_cast<unsigned*>(faces->buffer.get());
        for (size_t i = 0; i < faces->count; ++i)
        {
            unsigned int vertexIndex1 = static_cast<unsigned int>(facesData[i]);
            unsigned int vertexIndex2 = static_cast<unsigned int>(facesData[i + 1]);
            unsigned int vertexIndex3 = static_cast<unsigned int>(facesData[i + 2]);

            indices.push_back(facesData[3 * i]);
            indices.push_back(facesData[3 * i + 1]);
            indices.push_back(facesData[3 * i + 2]);
        }

        if (calculateTangents && normals)
        {
            compute_tangents_gram_smidt(vertices, indices);
        }

        if (overrideGeometry)
        {
            Core::Geometry* oldGeom = mesh->get_geometry();
            if (oldGeom)
            {
                oldGeom->fill(vertices, indices);
            }
            return;
        }

        Core::Geometry* g = new Core::Geometry();
        g->fill(vertices, indices);
        mesh->push_geometry(g);
        mesh->set_file_route(fileName);
    } catch (const std::exception& e)
    { std::cerr << "Caught tinyply exception: " << e.what() << std::endl; }
}
void VKFW::Tools::Loaders::load_GLB(Core::Mesh* const mesh, const std::string fileName, int meshIndex,
                                     std::vector<Core::Texture*>* outTextures) {
    tinygltf::Model    model;
    tinygltf::TinyGLTF loader;
    std::string        err;
    std::string        warn;

    bool ok = loader.LoadBinaryFromFile(&model, &err, &warn, fileName);
    if (!warn.empty())
        LOG_DEBUG("GLB warn [" + fileName + "]: " + warn);
    if (!err.empty())
        LOG_ERROR("GLB error [" + fileName + "]: " + err);
    if (!ok)
    {
        LOG_ERROR("Failed to parse GLB: " + fileName);
        return;
    }

    int meshCount = static_cast<int>(model.meshes.size());
    int firstMesh = (meshIndex < 0) ? 0 : meshIndex;
    int lastMesh  = (meshIndex < 0) ? meshCount - 1 : meshIndex;

    if (firstMesh < 0 || firstMesh >= meshCount || lastMesh >= meshCount)
    {
        LOG_ERROR("GLB mesh index out of range for: " + fileName);
        return;
    }

    // --- helpers ---
    // Returns a pointer to the first byte of an accessor's data in its buffer.
    auto acc_raw = [&](int acc_idx) -> const uint8_t* {
        const auto& acc = model.accessors[acc_idx];
        const auto& bv  = model.bufferViews[acc.bufferView];
        return model.buffers[bv.buffer].data.data() + bv.byteOffset + acc.byteOffset;
    };
    auto acc_count = [&](int acc_idx) -> size_t { return model.accessors[acc_idx].count; };

    // --- process each selected glTF mesh ---
    for (int mi = firstMesh; mi <= lastMesh; ++mi)
    {
        const tinygltf::Mesh& gltfMesh = model.meshes[mi];

        for (const tinygltf::Primitive& prim : gltfMesh.primitives)
        {
            if (prim.mode != TINYGLTF_MODE_TRIANGLES)
                continue;

            // ---- POSITION (required) ----
            auto posIt = prim.attributes.find("POSITION");
            if (posIt == prim.attributes.end())
                continue;

            size_t       vertCount = acc_count(posIt->second);
            const float* posPtr    = reinterpret_cast<const float*>(acc_raw(posIt->second));

            // ---- NORMAL (optional — computed if absent) ----
            const float* nrmPtr = nullptr;
            auto         nrmIt  = prim.attributes.find("NORMAL");
            if (nrmIt != prim.attributes.end())
                nrmPtr = reinterpret_cast<const float*>(acc_raw(nrmIt->second));

            // ---- TEXCOORD_0 (optional) ----
            const float* uvPtr = nullptr;
            auto         uvIt  = prim.attributes.find("TEXCOORD_0");
            if (uvIt != prim.attributes.end())
                uvPtr = reinterpret_cast<const float*>(acc_raw(uvIt->second));

            // ---- COLOR_0 (optional) ----
            const float* colPtr = nullptr;
            auto         colIt  = prim.attributes.find("COLOR_0");
            if (colIt != prim.attributes.end())
                colPtr = reinterpret_cast<const float*>(acc_raw(colIt->second));

            // ---- build vertex array ----
            std::vector<Graphics::Vertex> vertices(vertCount);
            for (size_t i = 0; i < vertCount; ++i)
            {
                vertices[i].pos      = {posPtr[3 * i], posPtr[3 * i + 1], posPtr[3 * i + 2]};
                vertices[i].normal   = nrmPtr ? Vec3(nrmPtr[3 * i], nrmPtr[3 * i + 1], nrmPtr[3 * i + 2]) : Vec3(0.0f);
                vertices[i].texCoord = uvPtr ? Vec2(uvPtr[2 * i], 1.0f - uvPtr[2 * i + 1]) : Vec2(0.0f);
                vertices[i].color    = colPtr ? Vec3(colPtr[3 * i], colPtr[3 * i + 1], colPtr[3 * i + 2]) : Vec3(1.0f);
                vertices[i].tangent  = Vec3(0.0f);
            }

            // ---- INDICES ----
            std::vector<uint32_t> indices;
            if (prim.indices >= 0)
            {
                const auto&    acc      = model.accessors[prim.indices];
                const uint8_t* idxPtr   = acc_raw(prim.indices);
                indices.resize(acc.count);

                if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
                {
                    const uint16_t* p = reinterpret_cast<const uint16_t*>(idxPtr);
                    for (size_t i = 0; i < acc.count; ++i)
                        indices[i] = p[i];
                }
                else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
                {
                    const uint32_t* p = reinterpret_cast<const uint32_t*>(idxPtr);
                    for (size_t i = 0; i < acc.count; ++i)
                        indices[i] = p[i];
                }
                else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
                {
                    for (size_t i = 0; i < acc.count; ++i)
                        indices[i] = idxPtr[i];
                }
            }

            // ---- compute normals from topology if not supplied ----
            if (!nrmPtr && !indices.empty())
            {
                for (size_t i = 0; i + 2 < indices.size(); i += 3)
                {
                    auto& v0 = vertices[indices[i]];
                    auto& v1 = vertices[indices[i + 1]];
                    auto& v2 = vertices[indices[i + 2]];
                    Vec3  n  = glm::cross(v1.pos - v0.pos, v2.pos - v0.pos);
                    v0.normal += n;
                    v1.normal += n;
                    v2.normal += n;
                }
                for (auto& v : vertices)
                {
                    float len = glm::length(v.normal);
                    if (len > 1e-6f)
                        v.normal /= len;
                }
            }

            // ---- tangents via Gram-Schmidt (always) ----
            compute_tangents_gram_smidt(vertices, indices);

            // ---- SKIN DATA (JOINTS_0 + WEIGHTS_0) ----
            std::optional<Core::SkinData> skinData;
            auto                          jointsIt  = prim.attributes.find("JOINTS_0");
            auto                          weightsIt = prim.attributes.find("WEIGHTS_0");
            if (jointsIt != prim.attributes.end() && weightsIt != prim.attributes.end())
            {
                Core::SkinData sd;
                sd.jointIndices.resize(vertCount);
                sd.jointWeights.resize(vertCount);

                // Joint indices — may be UNSIGNED_SHORT or UNSIGNED_BYTE
                const auto&    jAcc = model.accessors[jointsIt->second];
                const uint8_t* jPtr = acc_raw(jointsIt->second);
                for (size_t i = 0; i < vertCount; ++i)
                {
                    if (jAcc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
                    {
                        const uint16_t* p = reinterpret_cast<const uint16_t*>(jPtr) + 4 * i;
                        sd.jointIndices[i] = {p[0], p[1], p[2], p[3]};
                    }
                    else // UNSIGNED_BYTE
                    {
                        const uint8_t* p = jPtr + 4 * i;
                        sd.jointIndices[i] = {p[0], p[1], p[2], p[3]};
                    }
                }

                // Joint weights — float VEC4
                const float* wPtr = reinterpret_cast<const float*>(acc_raw(weightsIt->second));
                for (size_t i = 0; i < vertCount; ++i)
                    sd.jointWeights[i] = {wPtr[4 * i], wPtr[4 * i + 1], wPtr[4 * i + 2], wPtr[4 * i + 3]};

                // Inverse bind matrices + joint names from the first skin
                if (!model.skins.empty())
                {
                    const tinygltf::Skin& skin = model.skins[0];
                    for (int ji : skin.joints)
                        sd.jointNames.push_back(ji < (int)model.nodes.size() ? model.nodes[ji].name : "");

                    if (skin.inverseBindMatrices >= 0)
                    {
                        size_t       jointCount = acc_count(skin.inverseBindMatrices);
                        const float* ibmPtr     = reinterpret_cast<const float*>(acc_raw(skin.inverseBindMatrices));
                        sd.inverseBindMatrices.resize(jointCount);
                        for (size_t j = 0; j < jointCount; ++j)
                            sd.inverseBindMatrices[j] = glm::make_mat4(ibmPtr + 16 * j);
                    }
                }

                skinData = std::move(sd);
            }

            // ---- MORPH TARGETS ----
            std::optional<Core::MorphTargetData> morphData;
            if (!prim.targets.empty())
            {
                Core::MorphTargetData md;

                // Target names come from mesh.extras["targetNames"]
                std::vector<std::string> names;
                if (gltfMesh.extras.IsObject() && gltfMesh.extras.Has("targetNames"))
                {
                    const auto& arr = gltfMesh.extras.Get("targetNames");
                    if (arr.IsArray())
                        for (int ti = 0; ti < (int)arr.ArrayLen(); ++ti)
                            names.push_back(arr.Get(ti).Get<std::string>());
                }

                for (size_t ti = 0; ti < prim.targets.size(); ++ti)
                {
                    Core::MorphTargetData::Target target;
                    auto                          dpIt = prim.targets[ti].find("POSITION");
                    if (dpIt != prim.targets[ti].end())
                    {
                        const float* dpPtr   = reinterpret_cast<const float*>(acc_raw(dpIt->second));
                        size_t       dpCount = acc_count(dpIt->second);
                        target.deltaPos.resize(dpCount);
                        for (size_t i = 0; i < dpCount; ++i)
                            target.deltaPos[i] = {dpPtr[3 * i], dpPtr[3 * i + 1], dpPtr[3 * i + 2]};
                    }
                    md.targets.push_back(std::move(target));
                    md.targetNames.push_back(ti < names.size() ? names[ti] : ("morph_" + std::to_string(ti)));
                }

                morphData = std::move(md);
            }

            // ---- build and push Geometry ----
            Core::Geometry* g = new Core::Geometry();
            g->fill(vertices, indices);
            if (skinData)
                g->set_skin_data(std::move(*skinData));
            if (morphData)
                g->set_morph_target_data(std::move(*morphData));

            mesh->push_geometry(g);

            // ---- extract embedded albedo texture if requested ----
            if (outTextures && prim.material >= 0)
            {
                const tinygltf::Material& mat = model.materials[prim.material];
                int texIdx = mat.pbrMetallicRoughness.baseColorTexture.index;
                if (texIdx >= 0)
                {
                    int imgIdx = model.textures[texIdx].source;
                    if (imgIdx >= 0)
                    {
                        const tinygltf::Image& img = model.images[imgIdx];
                        // img.image is RGBA decoded by tinygltf; copy into malloc'd buffer
                        // (same ownership convention as stbi_load — resource manager will consume it)
                        size_t         byteCount = img.width * img.height * 4;
                        unsigned char* pixels    = static_cast<unsigned char*>(malloc(byteCount));
                        if (img.image.size() >= byteCount)
                            memcpy(pixels, img.image.data(), byteCount);
                        else
                            memset(pixels, 255, byteCount); // fallback white

                        Core::Texture* tex = new Core::Texture();
                        tex->set_image_cache(
                            pixels,
                            {static_cast<unsigned int>(img.width), static_cast<unsigned int>(img.height), 1},
                            4);
                        tex->set_format(SRGBA_8);
                        outTextures->push_back(tex);
                    }
                }
            }
        }
    }

    mesh->set_file_route(fileName);
}

void VKFW::Tools::Loaders::inspect_GLB(const std::string fileName) {
    tinygltf::Model    model;
    tinygltf::TinyGLTF loader;
    std::string        err, warn;

    bool ok = loader.LoadBinaryFromFile(&model, &err, &warn, fileName);
    if (!ok)
    {
        std::cerr << "inspect_GLB: failed to load " << fileName << "\n";
        if (!err.empty())
            std::cerr << "  error: " << err << "\n";
        return;
    }

    std::string  outPath = fileName + ".params.txt";
    std::ofstream out(outPath);
    if (!out)
    {
        std::cerr << "inspect_GLB: cannot write " << outPath << "\n";
        return;
    }

    out << "=== GLB INSPECTION: " << fileName << " ===\n\n";

    // --- Nodes ---
    out << "NODES (" << model.nodes.size() << "):\n";
    for (int i = 0; i < (int)model.nodes.size(); ++i)
    {
        const auto& n = model.nodes[i];
        out << "  [" << i << "] \"" << n.name << "\"";
        if (n.mesh >= 0)
            out << "  mesh=" << n.mesh;
        if (n.skin >= 0)
            out << "  skin=" << n.skin;
        if (!n.children.empty())
        {
            out << "  children=[";
            for (int c : n.children)
                out << c << ",";
            out << "]";
        }
        if (!n.translation.empty())
            out << "  T=(" << n.translation[0] << "," << n.translation[1] << "," << n.translation[2] << ")";
        if (!n.rotation.empty())
            out << "  R=(" << n.rotation[0] << "," << n.rotation[1] << "," << n.rotation[2] << "," << n.rotation[3] << ")";
        out << "\n";
    }

    // --- Skins ---
    out << "\nSKINS (" << model.skins.size() << "):\n";
    for (int si = 0; si < (int)model.skins.size(); ++si)
    {
        const auto& skin = model.skins[si];
        out << "  Skin[" << si << "] \"" << skin.name << "\"  joints=" << skin.joints.size() << "\n";
        for (int ji = 0; ji < (int)skin.joints.size(); ++ji)
        {
            int nodeIdx = skin.joints[ji];
            std::string jName = (nodeIdx < (int)model.nodes.size()) ? model.nodes[nodeIdx].name : "<unknown>";
            out << "    joint[" << ji << "] node=" << nodeIdx << " \"" << jName << "\"\n";
        }
    }

    // --- Meshes + Morph Targets ---
    out << "\nMESHES (" << model.meshes.size() << "):\n";
    for (int mi = 0; mi < (int)model.meshes.size(); ++mi)
    {
        const auto& mesh = model.meshes[mi];
        out << "  Mesh[" << mi << "] \"" << mesh.name << "\"  primitives=" << mesh.primitives.size() << "\n";

        // morph target names from extras
        if (mesh.extras.IsObject() && mesh.extras.Has("targetNames"))
        {
            const auto& arr = mesh.extras.Get("targetNames");
            if (arr.IsArray())
            {
                out << "    targetNames (" << arr.ArrayLen() << "):\n";
                for (int ti = 0; ti < (int)arr.ArrayLen(); ++ti)
                    out << "      [" << ti << "] \"" << arr.Get(ti).Get<std::string>() << "\"\n";
            }
        }

        for (int pi = 0; pi < (int)mesh.primitives.size(); ++pi)
        {
            const auto& prim = mesh.primitives[pi];
            out << "    Primitive[" << pi << "]  morphTargets=" << prim.targets.size() << "\n";
            out << "      attributes:";
            for (const auto& kv : prim.attributes)
                out << " " << kv.first;
            out << "\n";
        }
    }

    // --- Animations ---
    out << "\nANIMATIONS (" << model.animations.size() << "):\n";
    for (int ai = 0; ai < (int)model.animations.size(); ++ai)
    {
        const auto& anim = model.animations[ai];
        out << "  Animation[" << ai << "] \"" << anim.name << "\"  channels=" << anim.channels.size()
            << "  samplers=" << anim.samplers.size() << "\n";

        // print samplers (time range)
        for (int si = 0; si < (int)anim.samplers.size(); ++si)
        {
            const auto& samp = anim.samplers[si];
            std::string interp = samp.interpolation.empty() ? "LINEAR" : samp.interpolation;
            // time range from input accessor
            const auto& timeAcc = model.accessors[samp.input];
            out << "    Sampler[" << si << "]  interp=" << interp
                << "  keys=" << timeAcc.count
                << "  tMin=" << timeAcc.minValues[0] << "  tMax=" << timeAcc.maxValues[0] << "\n";
        }

        // print channels
        for (int ci = 0; ci < (int)anim.channels.size(); ++ci)
        {
            const auto& ch = anim.channels[ci];
            std::string targetName = (ch.target_node >= 0 && ch.target_node < (int)model.nodes.size())
                                         ? model.nodes[ch.target_node].name
                                         : "<node" + std::to_string(ch.target_node) + ">";
            out << "    Channel[" << ci << "]  node=" << ch.target_node
                << " \"" << targetName << "\""
                << "  path=" << ch.target_path
                << "  sampler=" << ch.sampler << "\n";
        }
    }

    out << "\n=== END ===\n";
    out.close();
    std::cout << "inspect_GLB: wrote " << outPath << "\n";
}

void VKFW::Tools::Loaders::load_3D_file(Core::Mesh* const mesh, const std::string fileName, bool asynCall, bool overrideGeometry) {
    size_t dotPosition = fileName.find_last_of(".");

    if (dotPosition != std::string::npos)
    {

        std::string fileExtension = fileName.substr(dotPosition + 1);

        if (fileExtension == OBJ)
        {
            if (asynCall)
            {
                std::thread loadThread(Loaders::load_OBJ, mesh, fileName, false, true, overrideGeometry);
                loadThread.detach();
            } else
                Loaders::load_OBJ(mesh, fileName, false, true, overrideGeometry);

            return;
        }
        if (fileExtension == PLY)
        {
            if (asynCall)
            {
                std::thread loadThread(Loaders::load_PLY, mesh, fileName, true, false, true, overrideGeometry);
                loadThread.detach();
            } else
                Loaders::load_PLY(mesh, fileName, true, false, true, overrideGeometry);

            return;
        }
        if (fileExtension == HAIR)
        {
            if (asynCall)
            {
                std::thread loadThread(Loaders::load_hair, mesh, fileName.c_str());
                loadThread.detach();
            } else
                Loaders::load_hair(mesh, fileName.c_str());

            return;
        }
        if (fileExtension == GLB || fileExtension == GLTF)
        {
            if (asynCall)
            {
                std::thread loadThread(Loaders::load_GLB, mesh, fileName, -1, nullptr);
                loadThread.detach();
            } else
                Loaders::load_GLB(mesh, fileName, -1, nullptr);

            return;
        }

        std::cerr << "Unsupported file format: " << fileExtension << std::endl;
    } else
    {
        std::cerr << "Invalid file name: " << fileName << std::endl;
    }
}
void VKFW::Tools::Loaders::load_hair(Core::Mesh* const mesh, const char* fileName) {

#define HAIR_FILE_SEGMENTS_BIT 1
#define HAIR_FILE_POINTS_BIT 2
#define HAIR_FILE_THICKNESS_BIT 4
#define HAIR_FILE_TRANSPARENCY_BIT 8
#define HAIR_FILE_COLORS_BIT 16
#define HAIR_FILE_INFO_SIZE 88

    unsigned short* segments = nullptr;
    float*          points;
    float*          dirs;
    float*          thickness;
    float*          transparency;
    float*          colors;

    struct Header {
        char         signature[4]; //!< This should be "HAIR"
        unsigned int hair_count;   //!< number of hair strands
        unsigned int point_count;  //!< total number of points of all strands
        unsigned int arrays;       //!< bit array of data in the file

        unsigned int d_segments;     //!< default number of segments of each strand
        float        d_thickness;    //!< default thickness of hair strands
        float        d_transparency; //!< default transparency of hair strands
        float        d_color[3];     //!< default color of hair strands

        char info[HAIR_FILE_INFO_SIZE]; //!< information about the file
    };

    Header header;

    header.signature[0]   = 'H';
    header.signature[1]   = 'A';
    header.signature[2]   = 'I';
    header.signature[3]   = 'R';
    header.hair_count     = 0;
    header.point_count    = 0;
    header.arrays         = 0; // no arrays
    header.d_segments     = 0;
    header.d_thickness    = 1.0f;
    header.d_transparency = 0.0f;
    header.d_color[0]     = 1.0f;
    header.d_color[1]     = 1.0f;
    header.d_color[2]     = 1.0f;
    memset(header.info, '\0', HAIR_FILE_INFO_SIZE);

    FILE* fp;
    fp = fopen(fileName, "rb");
    if (fp == nullptr)
        return;

    // read the header
    size_t headread = fread(&header, sizeof(Header), 1, fp);

    // Check if header is correctly read
    if (headread < 1)
        return;

    // Check if this is a hair file
    if (strncmp(header.signature, "HAIR", 4) != 0)
        return;

    // Read segments array
    if (header.arrays & HAIR_FILE_SEGMENTS_BIT)
    {
        segments         = new unsigned short[header.hair_count];
        size_t readcount = fread(segments, sizeof(unsigned short), header.hair_count, fp);
        if (readcount < header.hair_count)
        {
            std::cerr << "Error reading segments" << std::endl;
            return;
        }
    }

    // Read points array
    if (header.arrays & HAIR_FILE_POINTS_BIT)
    {
        points           = new float[header.point_count * 3];
        size_t readcount = fread(points, sizeof(float), header.point_count * 3, fp);
        if (readcount < header.point_count * 3)
        {
            std::cerr << "Error reading points" << std::endl;
            return;
        }
    }

    // Read thickness array
    if (header.arrays & HAIR_FILE_THICKNESS_BIT)
    {
        thickness        = new float[header.point_count];
        size_t readcount = fread(thickness, sizeof(float), header.point_count, fp);
        if (readcount < header.point_count)
        {
            std::cerr << "Error reading thickness" << std::endl;
            return;
        }
    }

    // Read thickness array
    if (header.arrays & HAIR_FILE_TRANSPARENCY_BIT)
    {
        transparency     = new float[header.point_count];
        size_t readcount = fread(transparency, sizeof(float), header.point_count, fp);
        if (readcount < header.point_count)
        {
            std::cerr << "Error reading alpha" << std::endl;
            return;
        }
    }

    // Read colors array
    if (header.arrays & HAIR_FILE_COLORS_BIT)
    {
        colors           = new float[header.point_count * 3];
        size_t readcount = fread(colors, sizeof(float), header.point_count * 3, fp);
        if (readcount < header.point_count * 3)
        {
            std::cerr << "Error reading colors" << std::endl;
            return;
        }
    }

    fclose(fp);

    auto computeDirection = [](float* d, float& d0len, float& d1len, float const* p0, float const* p1, float const* p2) {
        // line from p0 to p1
        float d0[3];
        d0[0]         = p1[0] - p0[0];
        d0[1]         = p1[1] - p0[1];
        d0[2]         = p1[2] - p0[2];
        float d0lensq = d0[0] * d0[0] + d0[1] * d0[1] + d0[2] * d0[2];
        d0len         = (d0lensq > 0) ? (float)sqrt(d0lensq) : 1.0f;

        // line from p1 to p2
        float d1[3];
        d1[0]         = p2[0] - p1[0];
        d1[1]         = p2[1] - p1[1];
        d1[2]         = p2[2] - p1[2];
        float d1lensq = d1[0] * d1[0] + d1[1] * d1[1] + d1[2] * d1[2];
        d1len         = (d1lensq > 0) ? (float)sqrt(d1lensq) : 1.0f;

        // make sure that d0 and d1 has the same length
        d0[0] *= d1len / d0len;
        d0[1] *= d1len / d0len;
        d0[2] *= d1len / d0len;

        // direction at p1
        d[0]         = d0[0] + d1[0];
        d[1]         = d0[1] + d1[1];
        d[2]         = d0[2] + d1[2];
        float dlensq = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
        float dlen   = (dlensq > 0) ? (float)sqrt(dlensq) : 1.0f;
        d[0] /= dlen;
        d[1] /= dlen;
        d[2] /= dlen;

        // return d0len;
    };

    auto fillDirectionArray = [=](float* dir) {
        if (dir == nullptr || header.point_count <= 0 || points == nullptr)
            return;

        int p = 0; // point index
        for (unsigned int i = 0; i < header.hair_count; i++)
        {
            int s = (segments) ? segments[i] : header.d_segments;
            if (s > 1)
            {
                // direction at point1
                float len0, len1;
                computeDirection(&dir[(p + 1) * 3], len0, len1, &points[p * 3], &points[(p + 1) * 3], &points[(p + 2) * 3]);

                // direction at point0
                float d0[3];
                d0[0]          = points[(p + 1) * 3] - dir[(p + 1) * 3] * len0 * 0.3333f - points[p * 3];
                d0[1]          = points[(p + 1) * 3 + 1] - dir[(p + 1) * 3 + 1] * len0 * 0.3333f - points[p * 3 + 1];
                d0[2]          = points[(p + 1) * 3 + 2] - dir[(p + 1) * 3 + 2] * len0 * 0.3333f - points[p * 3 + 2];
                float d0lensq  = d0[0] * d0[0] + d0[1] * d0[1] + d0[2] * d0[2];
                float d0len    = (d0lensq > 0) ? (float)sqrt(d0lensq) : 1.0f;
                dir[p * 3]     = d0[0] / d0len;
                dir[p * 3 + 1] = d0[1] / d0len;
                dir[p * 3 + 2] = d0[2] / d0len;

                // We computed the first 2 points
                p += 2;

                // Compute the direction for the rest
                for (int t = 2; t < s; t++, p++)
                {
                    computeDirection(&dir[p * 3], len0, len1, &points[(p - 1) * 3], &points[p * 3], &points[(p + 1) * 3]);
                }

                // direction at the last point
                d0[0]          = -points[(p - 1) * 3] + dir[(p - 1) * 3] * len1 * 0.3333f + points[p * 3];
                d0[1]          = -points[(p - 1) * 3 + 1] + dir[(p - 1) * 3 + 1] * len1 * 0.3333f + points[p * 3 + 1];
                d0[2]          = -points[(p - 1) * 3 + 2] + dir[(p - 1) * 3 + 2] * len1 * 0.3333f + points[p * 3 + 2];
                d0lensq        = d0[0] * d0[0] + d0[1] * d0[1] + d0[2] * d0[2];
                d0len          = (d0lensq > 0) ? (float)sqrt(d0lensq) : 1.0f;
                dir[p * 3]     = d0[0] / d0len;
                dir[p * 3 + 1] = d0[1] / d0len;
                dir[p * 3 + 2] = d0[2] / d0len;
                p++;
            } else if (s > 0)
            {
                // if it has a single segment
                float d0[3];
                d0[0]                = points[(p + 1) * 3] - points[p * 3];
                d0[1]                = points[(p + 1) * 3 + 1] - points[p * 3 + 1];
                d0[2]                = points[(p + 1) * 3 + 2] - points[p * 3 + 2];
                float d0lensq        = d0[0] * d0[0] + d0[1] * d0[1] + d0[2] * d0[2];
                float d0len          = (d0lensq > 0) ? (float)sqrt(d0lensq) : 1.0f;
                dir[p * 3]           = d0[0] / d0len;
                dir[p * 3 + 1]       = d0[1] / d0len;
                dir[p * 3 + 2]       = d0[2] / d0len;
                dir[(p + 1) * 3]     = dir[p * 3];
                dir[(p + 1) * 3 + 1] = dir[p * 3 + 1];
                dir[(p + 1) * 3 + 2] = dir[p * 3 + 2];
                p += 2;
            }
            //*/
        }
    };

    dirs = new float[header.point_count * 3];
    fillDirectionArray(dirs);

    std::vector<Graphics::Vertex> vertices;
    vertices.reserve(header.point_count * 3);
    std::vector<uint32_t> indices;

    size_t index                  = 0;
    size_t pointId                = 0;
    float  totalFiberLengthGlobal = 0.0f;
    for (size_t hair = 0; hair < header.hair_count; hair++) // Hair Fiber
    {
        size_t    max_segments   = segments ? segments[hair] : header.d_segments;
        float     strandRandomID = ((float)rand()) / RAND_MAX;
        glm::vec3 color          = {strandRandomID, ((float)rand()) / RAND_MAX, ((float)rand()) / RAND_MAX};

        std::vector<float> dists;
        dists.reserve(max_segments + 1);
        dists.push_back(0.0f); // La raíz está a distancia 0
        float  totalStrandLength = 0.0f;
        size_t tempPointId       = pointId; // Us

        for (size_t i = 0; i < max_segments; i++)
        {
            // P0 (Inicio del segmento)
            float x0 = points[tempPointId];
            float y0 = points[tempPointId + 1];
            float z0 = points[tempPointId + 2];

            // P1 (Fin del segmento / Inicio del siguiente)
            float x1 = points[tempPointId + 3];
            float y1 = points[tempPointId + 4];
            float z1 = points[tempPointId + 5];

            float segLen = sqrt(pow(x1 - x0, 2) + pow(y1 - y0, 2) + pow(z1 - z0, 2));
            totalStrandLength += segLen;
            dists.push_back(totalStrandLength);

            tempPointId += 3; // Avanzamos 3 floats (1 punto)
        }
        totalFiberLengthGlobal += totalStrandLength;

        for (size_t i = 0; i < max_segments; i++)
        {
            // Calcular UV.x normalizado (0.0 a 1.0)
            // Para el vértice de inicio del segmento (i)
            float u_start = (totalStrandLength > 0.0f) ? (dists[i] / totalStrandLength) : 0.0f;

            // Para el vértice de fin del segmento (i+1)
            // Nota: En tu bucle original duplicabas vértices para líneas separadas.
            // Aquí añades el vértice 'start' del segmento.

            vertices.push_back({{points[pointId], points[pointId + 1], points[pointId + 2]}, // Pos
                                {0.0f, 0.0f, 0.0f},                                          // Normal (no usada/calculada luego?)
                                {dirs[pointId], dirs[pointId + 1], dirs[pointId + 2]},       // Tangente
                                {u_start, strandRandomID},                                   // UV: x=RootTip, y=RandomID
                                color});

            indices.push_back(index);
            indices.push_back(index + 1);
            index++;
            pointId += 3; // Avanzar cursor real
        }

        float u_end = 1.0f; // La punta siempre es 1.0

        vertices.push_back({{points[pointId], points[pointId + 1], points[pointId + 2]}, // Pos final
                            {0.0f, 0.0f, 0.0f},
                            {dirs[pointId], dirs[pointId + 1], dirs[pointId + 2]}, // Tangente final
                            {u_end, strandRandomID},                               // UV
                            color});

        pointId += 3; // Avanzar cursor real pasado el último punto
        index++;
    }

    Core::Geometry* g = new Core::Geometry();
    g->fill(vertices, indices);

    // Calcular media global
    float avgFiberLength = (header.hair_count > 0) ? (totalFiberLengthGlobal / header.hair_count) : 0.0f;
    g->set_avg_fiber_length(avgFiberLength);

    mesh->push_geometry(g);
    mesh->set_file_route(std::string(fileName));

  
    
}

void VKFW::Tools::Loaders::load_texture(Core::ITexture* const texture, const std::string fileName, TextureFormatType textureFormat, bool asyncCall) {
    size_t dotPosition = fileName.find_last_of(".");

    if (dotPosition != std::string::npos)
    {

        std::string fileExtension = fileName.substr(dotPosition + 1);

        if (fileExtension == PNG || fileExtension == JPG)
        {
            if (asyncCall)
            {
                std::thread loadThread(Loaders::load_PNG, static_cast<Core::Texture*>(texture), fileName, textureFormat);
                loadThread.detach();
            } else
                Loaders::load_PNG(static_cast<Core::Texture*>(texture), fileName, textureFormat);

            return;
        }
        if (fileExtension == HDR)
        {
            if (asyncCall)
            {
                std::thread loadThread(Loaders::load_HDRi, static_cast<Core::TextureHDR*>(texture), fileName);
                loadThread.detach();
            } else
                Loaders::load_HDRi(static_cast<Core::TextureHDR*>(texture), fileName);

            return;
        }

        std::cerr << "Unsupported file format: " << fileExtension << std::endl;
    } else
    {
        std::cerr << "Invalid file name: " << fileName << std::endl;
    }
}

void VKFW::Tools::Loaders::load_PNG(Core::Texture* const texture, const std::string fileName, TextureFormatType textureFormat) {
    int            w, h, ch;
    unsigned char* imgCache = nullptr;
    imgCache                = stbi_load(fileName.c_str(), &w, &h, &ch, STBI_rgb_alpha);
    if (imgCache)
    {
        texture->set_image_cache(imgCache, {static_cast<unsigned int>(w), static_cast<unsigned int>(h), 1}, 4);
        // Set automatically teh optimal format for each type.
        // User can override it after, I he need some other more specific format ...
        switch (textureFormat)
        {
        case TEXTURE_FORMAT_TYPE_COLOR:
            texture->set_format(SRGBA_8);
            break;
        case TEXTURE_FORMAT_TYPE_NORMAL:
            texture->set_format(RGBA_8U);
            break;
        case TEXTURE_FORMAT_TYPE_HDR:
            texture->set_format(SRGBA_16F);
            break;
        }
    } else
    {
#ifndef NDEBUG
        LOG_DEBUG("Failed to load texture PNG file" + fileName);
#endif
        return;
    };
#ifndef NDEBUG
    LOG_DEBUG("PNG Texture loaded successfully");
#endif // DEBUG
}

void VKFW::Tools::Loaders::load_HDRi(Core::TextureHDR* const texture, const std::string fileName) {
    int    w, h, ch;
    float* HDRcache = nullptr;
    HDRcache        = stbi_loadf(fileName.c_str(), &w, &h, &ch, STBI_rgb_alpha);
    if (HDRcache)
    {
        texture->set_image_cache(HDRcache, {static_cast<unsigned int>(w), static_cast<unsigned int>(h), 1}, 4);
        texture->set_format(SRGBA_32F);
    } else
    {
#ifndef NDEBUG
        LOG_DEBUG("Failed to load texture HDRi file" + fileName);
#endif
        return;
    };
#ifndef NDEBUG
    LOG_DEBUG("HDRi Texture loaded successfully");
#endif // DEBUG
}
void VKFW::Tools::Loaders::load_3D_texture(Core::ITexture* const texture, const std::string fileName, uint16_t depth, TextureFormatType textureFormat) {

    size_t dotPosition = fileName.find_last_of(".");

    if (dotPosition != std::string::npos)
    {

        std::string fileExtension = fileName.substr(dotPosition + 1);

        if (fileExtension == PNG || fileExtension == JPG)
        {
            int            w, h, ch;
            unsigned char* imgCache = nullptr;
            imgCache                = stbi_load(fileName.c_str(), &w, &h, &ch, STBI_rgb_alpha);
            if (imgCache)
            {
                texture->set_type(TextureTypeFlagBits::TEXTURE_3D);
                int      largerSide  = w > h ? w : h;
                int      shorterSide = w > h ? h : w;
                uint16_t finalDepth  = depth == 0 ? largerSide / shorterSide : depth;
                texture->set_image_cache(imgCache, {static_cast<unsigned int>(shorterSide), static_cast<unsigned int>(largerSide / finalDepth), finalDepth}, 4);
                // Set automatically the optimal format for each type.
                // User can override it after, I he need some other more specific format ...
                switch (textureFormat)
                {
                case TEXTURE_FORMAT_TYPE_COLOR:
                    texture->set_format(SRGBA_8);
                    break;
                case TEXTURE_FORMAT_TYPE_NORMAL:
                    texture->set_format(RGBA_8U);
                    break;
                case TEXTURE_FORMAT_TYPE_HDR:
                    texture->set_format(SRGBA_16F);
                    break;
                }
            } else
            {
#ifndef NDEBUG
                LOG_DEBUG("Failed to load texture 3D PNG file" + fileName);
#endif
                return;
            };
        }
        if (fileExtension == HDR)
        {
            int    w, h, ch;
            float* HDRcache = nullptr;
            HDRcache        = stbi_loadf(fileName.c_str(), &w, &h, &ch, STBI_rgb_alpha);
            if (HDRcache)
            {
                texture->set_type(TextureTypeFlagBits::TEXTURE_3D);
                int      largerSide  = w > h ? w : h;
                int      shorterSide = w > h ? h : w;
                uint16_t finalDepth  = depth == 0 ? largerSide / shorterSide : depth;
                texture->set_image_cache(HDRcache, {static_cast<unsigned int>(shorterSide), static_cast<unsigned int>(largerSide / finalDepth), finalDepth}, 4);
                texture->set_format(SRGBA_32F);

            } else
            {
#ifndef NDEBUG
                LOG_DEBUG("Failed to load texture 3D HDR file" + fileName);
#endif
                return;
            };
        }
    }

#ifndef NDEBUG
    LOG_DEBUG("3D Texture loaded successfully");
#endif // DEBUG
}
void VKFW::Tools::Loaders::compute_tangents_gram_smidt(std::vector<Graphics::Vertex>& vertices, const std::vector<uint32_t>& indices) {
    if (!indices.empty())
        for (size_t i = 0; i < indices.size(); i += 3)
        {
            size_t i0 = indices[i];
            size_t i1 = indices[i + 1];
            size_t i2 = indices[i + 2];

            Vec3 tangent = Graphics::Utils::get_tangent_gram_smidt(
                vertices[i0].pos, vertices[i1].pos, vertices[i2].pos, vertices[i0].texCoord, vertices[i1].texCoord, vertices[i2].texCoord, vertices[i0].normal);

            vertices[i0].tangent += tangent;
            vertices[i1].tangent += tangent;
            vertices[i2].tangent += tangent;
        }
    else
        for (size_t i = 0; i < vertices.size(); i += 3)
        {
            Vec3 tangent = Graphics::Utils::get_tangent_gram_smidt(vertices[i].pos,
                                                                   vertices[i + 1].pos,
                                                                   vertices[i + 2].pos,
                                                                   vertices[i].texCoord,
                                                                   vertices[i + 1].texCoord,
                                                                   vertices[i + 2].texCoord,
                                                                   vertices[i].normal);

            vertices[i].tangent += tangent;
            vertices[i + 1].tangent += tangent;
            vertices[i + 2].tangent += tangent;
        }
}
