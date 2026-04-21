/*
    This file is part of Vulkan-Engine, a simple to use Vulkan based 3D library

    MIT License

    Copyright (c) 2023 Antonio Espinosa Garcia

*/
#pragma once

#include <engine/common.h>
#include <engine/core/geometries/geometry.h>
#include <string>
#include <vector>

VULKAN_ENGINE_NAMESPACE_BEGIN

namespace Core {

enum class AnimationTargetType
{
    MORPH_WEIGHT,
    JOINT_TRANSLATION,
    JOINT_ROTATION, // stored as vec4 (x,y,z,w quat)
    JOINT_SCALE,
    NODE_TRANSLATION,
    NODE_ROTATION,
    NODE_SCALE,
};

enum class InterpolationMode
{
    STEP,
    LINEAR,
    CUBICSPLINE,
};

struct Keyframe {
    float time;  // seconds
    Vec4  value; // scalars in .x; vec3 in .xyz; quat in .xyzw
};

struct Track {
    AnimationTargetType   type;
    std::string           targetName; // joint name or morph target name
    InterpolationMode     interp = InterpolationMode::LINEAR;
    std::vector<Keyframe> keyframes; // sorted ascending by time
};

struct AnimationPose {
    std::vector<float> morphWeights;  // one per morph target
    std::vector<Mat4>  jointMatrices; // one per joint, model-space local→bind
};

struct Animation {
    std::string        name;
    float              duration = 0.0f;
    float              fps      = 30.0f; // reference; sampling is time-based
    bool               loop     = true;
    std::vector<Track> tracks;

    /*
    Sample the animation at tSeconds (in animation-local time), writing morph
    weights and joint local-TRS matrices into out. skinData and morphData are
    used for name→index lookups. out is resized to fit if needed.
    */
    void sample(float                 tSeconds,
                const SkinData&       skinData,
                const MorphTargetData& morphData,
                AnimationPose&        out) const;
};

} // namespace Core

VULKAN_ENGINE_NAMESPACE_END
