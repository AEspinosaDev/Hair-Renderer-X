/*
    This file is part of Vulkan-Engine, a simple to use Vulkan based 3D library

    MIT License

    Copyright (c) 2023 Antonio Espinosa Garcia

*/
#ifndef HAIR_CARD_H
#define HAIR_CARD_H

#include <engine/core/materials/material.h>
#include <engine/graphics/descriptors.h>

VULKAN_ENGINE_NAMESPACE_BEGIN

namespace Core {

/// Hair card material for alpha-tested planar hair mesh cards (e.g. Unreal MetaHuman hair).
/// Uses a packed data texture (R=opacity mask, G=root-to-tip gradient, B=per-strand ID).
/// Lighting uses the Kajiya-Kay anisotropic specular model with tangent-aligned highlights.
class HairCardMaterial : public IMaterial
{
  protected:
    Vec3  m_hairColor      = {1.0f, 1.0f, 1.0f}; // Global hair color multiplier (white = no change)
    float m_alphaThreshold = 0.1f;                // Alpha discard threshold (reads texture R channel)

    float m_roughness         = 0.4f; // Specular highlight width (0 = sharp, 1 = very diffuse)
    float m_specularIntensity = 0.05f; // Kajiya-Kay specular peak strength
    float m_specularShift     = 0.1f; // Tangent shift along normal for highlight placement
    float m_colorVariation    = 0.1f; // How much B channel (per-strand ID) shifts base color

    Vec3 m_rootColor = {0.05f, 0.03f, 0.01f}; // Hair color at root (blended by G channel)
    Vec3 m_tipColor  = {0.25f, 0.15f, 0.06f}; // Hair color at tip (blended by G channel)

    bool m_hasHairDataTexture = false;
    bool m_hasNormalTexture   = false;

    enum Textures
    {
        HAIR_DATA = 0, // Packed texture: R=alpha mask, G=root-to-tip gradient, B=strand ID
        NORMAL    = 1, // Optional tangent-space normal map for surface detail
    };

    std::unordered_map<int, ITexture*> m_textures{{HAIR_DATA, nullptr}, {NORMAL, nullptr}};
    std::unordered_map<int, bool>      m_textureBindingState;

    virtual Graphics::MaterialUniforms                get_uniforms() const;
    virtual inline std::unordered_map<int, ITexture*> get_textures() const {
        return m_textures;
    }
    virtual std::unordered_map<int, bool> get_texture_binding_state() const {
        return m_textureBindingState;
    }
    virtual void set_texture_binding_state(int id, bool state) {
        m_textureBindingState[id] = state;
    }

  public:
    HairCardMaterial()
        : IMaterial(HAIR_CARD_TYPE) {
        m_settings.alphaTest   = true;  // Discard on R channel threshold
        m_settings.faceCulling = false; // Double-sided: visible from both sides of the card
    }

    inline Vec3 get_hair_color() const {
        return m_hairColor;
    }
    inline void set_hair_color(Vec3 c) {
        m_hairColor = c;
        m_isDirty   = true;
    }

    inline float get_alpha_threshold() const {
        return m_alphaThreshold;
    }
    inline void set_alpha_threshold(float t) {
        m_alphaThreshold = t;
        m_isDirty        = true;
    }

    inline float get_roughness() const {
        return m_roughness;
    }
    inline void set_roughness(float r) {
        m_roughness = r;
        m_isDirty   = true;
    }

    inline float get_specular_intensity() const {
        return m_specularIntensity;
    }
    inline void set_specular_intensity(float s) {
        m_specularIntensity = s;
        m_isDirty           = true;
    }

    inline float get_specular_shift() const {
        return m_specularShift;
    }
    inline void set_specular_shift(float s) {
        m_specularShift = s;
        m_isDirty       = true;
    }

    inline float get_color_variation() const {
        return m_colorVariation;
    }
    inline void set_color_variation(float v) {
        m_colorVariation = v;
        m_isDirty        = true;
    }

    inline Vec3 get_root_color() const {
        return m_rootColor;
    }
    inline void set_root_color(Vec3 c) {
        m_rootColor = c;
        m_isDirty   = true;
    }

    inline Vec3 get_tip_color() const {
        return m_tipColor;
    }
    inline void set_tip_color(Vec3 c) {
        m_tipColor = c;
        m_isDirty  = true;
    }

    inline ITexture* get_hair_data_texture() {
        return m_textures[HAIR_DATA];
    }
    inline void set_hair_data_texture(ITexture* t) {
        m_hasHairDataTexture             = t ? true : false;
        m_textureBindingState[HAIR_DATA] = false;
        m_textures[HAIR_DATA]            = t;
        m_isDirty                        = true;
    }

    inline ITexture* get_normal_texture() {
        return m_textures[NORMAL];
    }
    inline void set_normal_texture(ITexture* t) {
        m_hasNormalTexture             = t ? true : false;
        m_textureBindingState[NORMAL]  = false;
        m_textures[NORMAL]             = t;
        m_isDirty                      = true;
    }
};

} // namespace Core
VULKAN_ENGINE_NAMESPACE_END
#endif
