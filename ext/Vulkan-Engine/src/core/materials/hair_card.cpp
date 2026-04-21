#include "engine/core/materials/hair_card.h"

VULKAN_ENGINE_NAMESPACE_BEGIN
namespace Core {

Graphics::MaterialUniforms HairCardMaterial::get_uniforms() const {
    // Uniform layout (each slot is a vec4):
    //   dataSlot1: { hairColor.r,  hairColor.g,  hairColor.b,  alphaThreshold }
    //   dataSlot2: { roughness,    specularIntensity, specularShift, colorVariation }
    //   dataSlot3: { rootColor.r,  rootColor.g,  rootColor.b,  hasHairDataTexture }
    //   dataSlot4: { tipColor.r,   tipColor.g,   tipColor.b,   hasTangentTexture  }
    //   dataSlot5-8: unused (zero-initialized by struct default)
    Graphics::MaterialUniforms uniforms;
    uniforms.dataSlot1 = Vec4(m_hairColor, m_alphaThreshold);
    uniforms.dataSlot2 = {m_roughness, m_specularIntensity, m_specularShift, m_colorVariation};
    uniforms.dataSlot3 = Vec4(m_rootColor, (float)m_hasHairDataTexture);
    uniforms.dataSlot4 = Vec4(m_tipColor, (float)m_hasTangentTexture);
    return uniforms;
}

} // namespace Core
VULKAN_ENGINE_NAMESPACE_END
