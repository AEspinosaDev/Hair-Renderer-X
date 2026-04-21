#include <engine/core/animation.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

VULKAN_ENGINE_NAMESPACE_BEGIN

namespace Core {

// ─── helpers ────────────────────────────────────────────────────────────────

static float clamp_time(float t, float duration, bool loop) {
    if (duration <= 0.0f)
        return 0.0f;
    if (loop)
        t = std::fmod(t, duration);
    else
        t = std::min(t, duration);
    return t < 0.0f ? 0.0f : t;
}

// Returns the index of the last keyframe whose time <= t (binary search).
// Returns 0 if t is before the first key; returns size-2 if t is after the last.
static size_t find_bracket(const std::vector<Keyframe>& keys, float t) {
    if (keys.size() <= 1)
        return 0;

    size_t lo = 0, hi = keys.size() - 1;
    while (lo + 1 < hi) {
        size_t mid = (lo + hi) / 2;
        if (keys[mid].time <= t)
            lo = mid;
        else
            hi = mid;
    }
    return lo;
}

static Vec4 interpolate(const Keyframe& k0,
                        const Keyframe& k1,
                        float           t,
                        InterpolationMode mode,
                        bool            isQuat)
{
    if (mode == InterpolationMode::STEP || k0.time >= k1.time)
        return k0.value;

    float alpha = (t - k0.time) / (k1.time - k0.time);
    alpha        = glm::clamp(alpha, 0.0f, 1.0f);

    if (isQuat) {
        glm::quat q0(k0.value.w, k0.value.x, k0.value.y, k0.value.z);
        glm::quat q1(k1.value.w, k1.value.x, k1.value.y, k1.value.z);
        glm::quat q = glm::slerp(q0, q1, alpha);
        return Vec4(q.x, q.y, q.z, q.w);
    }

    if (mode == InterpolationMode::CUBICSPLINE) {
        // Catmull-Rom / glTF cubic: hermite with in/out tangents stored as
        // three-value groups. We treat it as linear as a safe fallback because
        // the IR doesn't carry tangent data yet.
        return glm::mix(k0.value, k1.value, alpha);
    }

    return glm::mix(k0.value, k1.value, alpha);
}

// ─── Animation::sample ──────────────────────────────────────────────────────

void Animation::sample(float                  tSeconds,
                       const SkinData&        skinData,
                       const MorphTargetData& morphData,
                       AnimationPose&         out) const
{
    // Initialise output buffers to bind-pose defaults if they haven't been sized.
    const size_t nMorph = morphData.targets.size();
    const size_t nJoint = skinData.jointNames.size();

    if (out.morphWeights.size() != nMorph)
        out.morphWeights.assign(nMorph, 0.0f);
    if (out.jointMatrices.size() != nJoint)
        out.jointMatrices.assign(nJoint, Mat4(1.0f));

    const float t = clamp_time(tSeconds, duration, loop);

    for (const Track& track : tracks) {
        if (track.keyframes.empty())
            continue;

        const size_t i0 = find_bracket(track.keyframes, t);
        const size_t i1 = std::min(i0 + 1, track.keyframes.size() - 1);

        const bool isQuat = (track.type == AnimationTargetType::JOINT_ROTATION ||
                             track.type == AnimationTargetType::NODE_ROTATION);

        Vec4 value = interpolate(track.keyframes[i0], track.keyframes[i1],
                                 t, track.interp, isQuat);

        switch (track.type) {
        case AnimationTargetType::MORPH_WEIGHT: {
            // Find the morph target by name.
            for (size_t m = 0; m < morphData.targetNames.size(); ++m) {
                if (morphData.targetNames[m] == track.targetName) {
                    out.morphWeights[m] = value.x;
                    break;
                }
            }
            break;
        }
        case AnimationTargetType::JOINT_TRANSLATION:
        case AnimationTargetType::JOINT_ROTATION:
        case AnimationTargetType::JOINT_SCALE:
        case AnimationTargetType::NODE_TRANSLATION:
        case AnimationTargetType::NODE_ROTATION:
        case AnimationTargetType::NODE_SCALE: {
            // Find joint by name.
            size_t jointIdx = SIZE_MAX;
            for (size_t j = 0; j < skinData.jointNames.size(); ++j) {
                if (skinData.jointNames[j] == track.targetName) {
                    jointIdx = j;
                    break;
                }
            }
            if (jointIdx == SIZE_MAX)
                break;

            // Decompose the current matrix into T/R/S, apply the channel,
            // then recompose.
            Mat4& M = out.jointMatrices[jointIdx];

            glm::vec3 scale, trans, skew;
            glm::vec4 persp;
            glm::quat rot;
            glm::decompose(M, scale, rot, trans, skew, persp);

            switch (track.type) {
            case AnimationTargetType::JOINT_TRANSLATION:
            case AnimationTargetType::NODE_TRANSLATION:
                trans = Vec3(value);
                break;
            case AnimationTargetType::JOINT_ROTATION:
            case AnimationTargetType::NODE_ROTATION:
                rot = glm::quat(value.w, value.x, value.y, value.z);
                break;
            case AnimationTargetType::JOINT_SCALE:
            case AnimationTargetType::NODE_SCALE:
                scale = Vec3(value);
                break;
            default:
                break;
            }

            M = glm::translate(Mat4(1.0f), trans) *
                glm::mat4_cast(rot) *
                glm::scale(Mat4(1.0f), scale);
            break;
        }
        }
    }
}

} // namespace Core

VULKAN_ENGINE_NAMESPACE_END
