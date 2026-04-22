#include <engine/core/scene/mesh.h>

VULKAN_ENGINE_NAMESPACE_BEGIN
namespace Core {
int Mesh::m_instanceCount = 0;

void BoundingSphere::setup(Mesh* const mesh) {
    maxCoords = {0.0f, 0.0f, 0.0f};
    minCoords = {INFINITY, INFINITY, INFINITY};

    auto                 g     = mesh->get_geometry();
    const GeometricData& stats = g->get_properties();

    if (stats.maxCoords.x > maxCoords.x)
        maxCoords.x = stats.maxCoords.x;
    if (stats.maxCoords.y > maxCoords.y)
        maxCoords.y = stats.maxCoords.y;
    if (stats.maxCoords.z > maxCoords.z)
        maxCoords.z = stats.maxCoords.z;
    if (stats.minCoords.x < minCoords.x)
        minCoords.x = stats.minCoords.x;
    if (stats.minCoords.y < minCoords.y)
        minCoords.y = stats.minCoords.y;
    if (stats.minCoords.z < minCoords.z)
        minCoords.z = stats.minCoords.z;

    // center = (maxCoords + minCoords) * 0.5f;
    radius = math::length((maxCoords - minCoords) * 0.5f);
    // Step 1: Compute bounding box center and extent
    center      = (maxCoords + minCoords) * 0.5f;
    Vec3 extent = (maxCoords - minCoords) * 0.5f;

    // Step 2: Compute the largest extent (to make it a cube)
    float halfSize = std::max({extent.x, extent.y, extent.z});

    // Step 3: Expand min and max symmetrically around center
    minCoords = center - Vec3(halfSize);
    maxCoords = center + Vec3(halfSize);
}

bool BoundingSphere::is_on_frustrum(const Frustum& frustum) const

{
    const Vec3 globalScale = obj->get_scale();

    const Vec3 globalCenter{obj->get_model_matrix() * Vec4(center, 1.f)};

    const float maxScale     = std::max(std::max(globalScale.x, globalScale.y), globalScale.z);
    const float globalRadius = radius * maxScale;

    return (frustum.leftFace.get_signed_distance(globalCenter) >= -globalRadius && frustum.rightFace.get_signed_distance(globalCenter) >= -globalRadius &&
            frustum.farFace.get_signed_distance(globalCenter) >= -globalRadius && frustum.nearFace.get_signed_distance(globalCenter) >= -globalRadius &&
            frustum.topFace.get_signed_distance(globalCenter) >= -globalRadius && frustum.bottomFace.get_signed_distance(globalCenter) >= -globalRadius);
}
void AABB::setup(Mesh* const mesh) {
}
bool AABB::is_on_frustrum(const Frustum& frustum) const {
    return false;
}

Geometry* Mesh::change_geometry(Geometry* g, size_t id) {
    if (m_geometry.size() < id + 1)
    {
        LOG_ERROR("Not enough geometry slots");
        return nullptr;
    }
    Geometry* old_g = m_geometry[id];
    m_geometry[id]  = g;
    return old_g;
}
IMaterial* Mesh::change_material(IMaterial* m, size_t id) {
    if (m_material.size() < id + 1)
    {
        LOG_ERROR("Not enough material slots");
        return nullptr;
    }

    IMaterial* old_m = m_material[id];
    m_material[id]   = m;
    return old_m;
}

void Mesh::set_animation(std::unique_ptr<Animation> anim) {
    m_animation = std::move(anim);
    m_localTime = 0.0f;
    m_pose      = {};
}

void Mesh::advance_animation(float dtSeconds) {
    if (!m_animation || m_animPaused)
        return;
    m_localTime += dtSeconds;
    static const SkinData        emptySkin;
    static const MorphTargetData emptyMorphs;
    const SkinData*        skin   = &emptySkin;
    const MorphTargetData* morphs = &emptyMorphs;
    for (auto* g : m_geometry) {
        if (!g) continue;
        const auto& props = g->get_properties();
        if (props.skinData.has_value())        skin   = &*props.skinData;
        if (props.morphTargetData.has_value()) morphs = &*props.morphTargetData;
        break;
    }
    m_animation->sample(m_localTime, *skin, *morphs, m_pose);

    for (auto* g : m_geometry) {
        if (!g) continue;
        const auto& props = g->get_properties();
        if (props.morphTargetData.has_value() || props.skinData.has_value())
            g->apply_deformation(m_pose.morphWeights, m_pose.jointMatrices);
    }
}

Mesh* Mesh::clone() const {
    Mesh* mesh       = new Mesh();
    mesh->m_material = m_material;
    mesh->m_geometry = m_geometry;
    mesh->setup_volume();
    mesh->set_name(m_name + std::string(" clone"));
    mesh->set_transform(m_transform);
    m_instanceCount++;
    return mesh;
}

} // namespace Core
VULKAN_ENGINE_NAMESPACE_END