#include <engine/core/animation_json.h>

#include <fstream>
#include <sstream>
#include <stdexcept>

VULKAN_ENGINE_NAMESPACE_BEGIN

namespace Core {

// ─── helpers ────────────────────────────────────────────────────────────────

static InterpolationMode parse_interp(const nlohmann::json& track) {
    if (!track.contains("interp"))
        return InterpolationMode::LINEAR;

    const std::string s = track["interp"].get<std::string>();
    if (s == "step")        return InterpolationMode::STEP;
    if (s == "cubicspline") return InterpolationMode::CUBICSPLINE;
    return InterpolationMode::LINEAR;
}

// Parse one keyframe value entry. entry is the second element of [time, value].
// isQuat distinguishes vec3 (xyz→.xyz) from quat (xyzw→.xyzw).
static Vec4 parse_value(const nlohmann::json& entry, bool isScalar, bool isQuat) {
    if (isScalar) {
        return Vec4(entry.get<float>(), 0.0f, 0.0f, 0.0f);
    }
    if (isQuat) {
        return Vec4(entry[0].get<float>(), entry[1].get<float>(),
                    entry[2].get<float>(), entry[3].get<float>());
    }
    // vec3
    return Vec4(entry[0].get<float>(), entry[1].get<float>(),
                entry[2].get<float>(), 0.0f);
}

// Parse "morph:<name>", "joint:<name>/<channel>", or "node:<name>/<channel>".
// Returns false if the string does not match any known prefix.
static bool parse_target(const std::string& target,
                         AnimationTargetType& outType,
                         std::string& outName)
{
    if (target.rfind("morph:", 0) == 0) {
        outType = AnimationTargetType::MORPH_WEIGHT;
        outName = target.substr(6);
        return true;
    }

    auto parse_node_or_joint = [&](const std::string& prefix,
                                   AnimationTargetType trans,
                                   AnimationTargetType rot,
                                   AnimationTargetType scale) -> bool {
        if (target.rfind(prefix, 0) != 0)
            return false;
        const std::string rest = target.substr(prefix.size()); // "<name>/<channel>"
        const auto slash = rest.rfind('/');
        if (slash == std::string::npos)
            return false;
        outName = rest.substr(0, slash);
        const std::string channel = rest.substr(slash + 1);
        if      (channel == "translation") outType = trans;
        else if (channel == "rotation")    outType = rot;
        else if (channel == "scale")       outType = scale;
        else return false;
        return true;
    };

    if (parse_node_or_joint("joint:", AnimationTargetType::JOINT_TRANSLATION,
                                      AnimationTargetType::JOINT_ROTATION,
                                      AnimationTargetType::JOINT_SCALE))
        return true;

    if (parse_node_or_joint("node:", AnimationTargetType::NODE_TRANSLATION,
                                     AnimationTargetType::NODE_ROTATION,
                                     AnimationTargetType::NODE_SCALE))
        return true;

    return false;
}

// ─── DefaultAnimationJsonAdapter::parse ─────────────────────────────────────

Animation DefaultAnimationJsonAdapter::parse(const nlohmann::json& root,
                                             const SkinData&        /*skin*/,
                                             const MorphTargetData& /*morphs*/)
{
    Animation anim;
    anim.name     = root.value("name",     std::string("unnamed"));
    anim.duration = root.value("duration", 0.0f);
    anim.fps      = root.value("fps",      30.0f);
    anim.loop     = root.value("loop",     true);

    if (!root.contains("tracks"))
        return anim;

    for (const auto& jtrack : root["tracks"]) {
        if (!jtrack.contains("target") || !jtrack.contains("keys"))
            continue;

        const std::string targetStr = jtrack["target"].get<std::string>();

        AnimationTargetType type;
        std::string         targetName;
        if (!parse_target(targetStr, type, targetName))
            continue;

        const bool isScalar = (type == AnimationTargetType::MORPH_WEIGHT);
        const bool isQuat   = (type == AnimationTargetType::JOINT_ROTATION ||
                               type == AnimationTargetType::NODE_ROTATION);

        Track track;
        track.type       = type;
        track.targetName = std::move(targetName);
        track.interp     = parse_interp(jtrack);

        for (const auto& key : jtrack["keys"]) {
            // key = [time, value]
            if (key.size() < 2)
                continue;
            Keyframe kf;
            kf.time  = key[0].get<float>();
            kf.value = parse_value(key[1], isScalar, isQuat);
            track.keyframes.push_back(kf);
        }

        if (!track.keyframes.empty())
            anim.tracks.push_back(std::move(track));
    }

    return anim;
}

// ─── load_animation_json ────────────────────────────────────────────────────

Animation load_animation_json(const std::string&     path,
                              const SkinData&        skin,
                              const MorphTargetData& morphs,
                              IAnimationJsonAdapter* adapter)
{
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error("load_animation_json: cannot open " + path);

    nlohmann::json root;
    file >> root;

    if (adapter) {
        return adapter->parse(root, skin, morphs);
    }

    DefaultAnimationJsonAdapter defaultAdapter;
    return defaultAdapter.parse(root, skin, morphs);
}

} // namespace Core

VULKAN_ENGINE_NAMESPACE_END
