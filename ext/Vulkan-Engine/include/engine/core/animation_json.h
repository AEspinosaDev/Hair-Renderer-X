/*
    This file is part of Vulkan-Engine, a simple to use Vulkan based 3D library

    MIT License

    Copyright (c) 2023 Antonio Espinosa Garcia

*/
#pragma once

#include <engine/core/animation.h>
#include <json.hpp>
#include <memory>
#include <string>

VULKAN_ENGINE_NAMESPACE_BEGIN

namespace Core {

/*
  Adapter interface: a stateless strategy that parses a nlohmann::json tree into
  an Animation IR. Implement a new subclass when the production JSON schema arrives;
  the IR, sampler, and renderer stay untouched.
*/
class IAnimationJsonAdapter {
public:
    virtual ~IAnimationJsonAdapter() = default;

    virtual Animation parse(const nlohmann::json& root,
                            const SkinData&        skin,
                            const MorphTargetData& morphs) = 0;
};

/*
  Default adapter — understands the internal JSON schema used for testing:

  {
    "name":     "demo",
    "duration": 3.0,
    "fps":      30.0,
    "loop":     true,
    "tracks": [
      { "target": "morph:Exp_000",          "interp": "linear", "keys": [[0.0, 0.0], [1.5, 1.0]] },
      { "target": "joint:head/rotation",    "interp": "linear", "keys": [[0.0,[0,0,0,1]],[1.5,[0,0.2,0,0.98]]] },
      { "target": "joint:neck/translation",                      "keys": [[0.0,[0,0,0]]] },
      { "target": "node:root/translation",                       "keys": [[0.0,[0,0,0]]] }
    ]
  }

  Target prefix convention:
    morph:<name>              → MORPH_WEIGHT      (scalar key value in .x)
    joint:<name>/translation  → JOINT_TRANSLATION (vec3 in .xyz)
    joint:<name>/rotation     → JOINT_ROTATION    (quat x,y,z,w in .xyzw)
    joint:<name>/scale        → JOINT_SCALE       (vec3 in .xyz)
    node:<name>/translation   → NODE_TRANSLATION
    node:<name>/rotation      → NODE_ROTATION
    node:<name>/scale         → NODE_SCALE

  Key array format:
    Scalar:  [time, value]           → Vec4(value, 0, 0, 0)
    Vec3:    [time, [x, y, z]]       → Vec4(x, y, z, 0)
    Quat:    [time, [x, y, z, w]]    → Vec4(x, y, z, w)

  Interp string: "step" | "linear" (default) | "cubicspline"
*/
class DefaultAnimationJsonAdapter : public IAnimationJsonAdapter {
public:
    Animation parse(const nlohmann::json& root,
                    const SkinData&        skin,
                    const MorphTargetData& morphs) override;
};

/*
  Load and parse a JSON animation file.
  Uses DefaultAnimationJsonAdapter when adapter == nullptr.
*/
Animation load_animation_json(const std::string&     path,
                              const SkinData&        skin,
                              const MorphTargetData& morphs,
                              IAnimationJsonAdapter* adapter = nullptr);

} // namespace Core

VULKAN_ENGINE_NAMESPACE_END
