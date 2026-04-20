#shader vertex
#version 460
#include camera.glsl
#include object.glsl

// Input
layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;
layout(location = 3) in vec3 tangent;

// Output
layout(location = 0) out vec3 v_pos;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec3 v_modelNormal;
layout(location = 3) out vec2 v_uv;
layout(location = 4) out vec3 v_modelPos;
layout(location = 5) out vec2 v_screenExtent;
layout(location = 6) out mat3 v_TBN; // columns: T, B, N (view space)

// Uniforms (vertex only reads tileUV-equivalent — none for hair cards, just for layout compliance)
layout(set = 1, binding = 1) uniform MaterialUniforms {
    vec3  hairColor;
    float alphaThreshold;
    float roughness;
    float specularIntensity;
    float specularShift;
    float colorVariation;
    vec3  rootColor;
    float hasHairDataTexture;
    vec3  tipColor;
    float hasNormalTexture;
    vec4  _pad5;
    vec4  _pad6;
    vec4  _pad7;
    vec4  _pad8;
} material;

void main() {
    gl_Position = camera.viewProj * object.model * vec4(pos, 1.0);

    // Flip V to match the OpenGL / PBR shader convention
    v_uv = vec2(uv.x, 1.0f - uv.y);

    mat4 mv  = camera.view * object.model;
    v_pos    = (mv * vec4(pos, 1.0)).xyz;
    v_normal = normalize(mat3(transpose(inverse(mv))) * normal);

    // Always compute TBN — tangent direction is required for Kajiya-Kay regardless of normal map
    vec3 T = -normalize(vec3(mv * vec4(tangent, 0.0)));
    vec3 N = normalize(vec3(mv * vec4(normal, 0.0)));
    vec3 B = cross(N, T);
    v_TBN  = mat3(T, B, N);

    v_modelPos    = (object.model * vec4(pos, 1.0)).xyz;
    v_modelNormal = normalize(mat3(transpose(inverse(object.model))) * normal);

    v_screenExtent = camera.screenExtent;
}

// ─────────────────────────────────────────────────────────────────────────────

#shader fragment
#version 460

#include camera.glsl
#include light.glsl
#include scene.glsl
#include object.glsl
#include shadow_mapping.glsl
#include IBL.glsl

#ifndef EPSILON
#define EPSILON 0.001
#endif

// Input
layout(location = 0) in vec3 v_pos;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec3 v_modelNormal;
layout(location = 3) in vec2 v_uv;
layout(location = 4) in vec3 v_modelPos;
layout(location = 5) in vec2 v_screenExtent;
layout(location = 6) in mat3 v_TBN;

// Output — 7 MRTs matching the forward pass framebuffer
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outBrightColor;
layout(location = 2) out vec4 outNormals;
layout(location = 3) out vec4 outAlbedoMask;
layout(location = 4) out vec4 outDiffuseIrr;
layout(location = 5) out vec4 outBackIrr;
layout(location = 6) out vec4 outLinearDepth;

// Global set (0)
layout(set = 0, binding = 2) uniform sampler2DArray shadowMap;
layout(set = 0, binding = 4) uniform samplerCube    irradianceMap;

// Per-object set (1)
layout(set = 1, binding = 1) uniform MaterialUniforms {
    vec3  hairColor;
    float alphaThreshold;
    float roughness;
    float specularIntensity;
    float specularShift;
    float colorVariation;
    vec3  rootColor;
    float hasHairDataTexture;
    vec3  tipColor;
    float hasNormalTexture;
    vec4  _pad5;
    vec4  _pad6;
    vec4  _pad7;
    vec4  _pad8;
} material;

// Texture set (2)
layout(set = 2, binding = 0) uniform sampler2D hairDataTex; // R=alpha, G=root-to-tip, B=strand ID
layout(set = 2, binding = 1) uniform sampler2D normalTex;   // Optional tangent-space normal map

// ─── Kajiya-Kay lighting ──────────────────────────────────────────────────────

// Diffuse: sin of angle between fiber tangent and light direction.
// Unlike Lambert, this gives a rim-like glow at grazing light angles.
// Wrap factor softens the falloff so light bleeds past the terminator,
// preventing hard dark edges on hair cards.
float hairDiffuse(vec3 T, vec3 L, vec3 N) {
    float sinTL = sqrt(max(0.0, 1.0 - pow(clamp(dot(T, L), -1.0, 1.0), 2.0)));
    // Wrap lighting: shift NdotL so the lit hemisphere extends past 90 degrees
    const float wrap = 0.5;
    float NdotL = (dot(N, L) + wrap) / (1.0 + wrap);
    return sinTL * clamp(NdotL, 0.0, 1.0);
}

// Specular: (sinTL * sinTV - cosTL * cosTV)^shininess, with optional tangent shift.
// Shift moves the highlight along the fiber (positive = toward root, negative = toward tip).
float hairSpecular(vec3 T, vec3 L, vec3 V, vec3 N, float shift, float shininess) {
    vec3  Ts    = normalize(T + shift * N);
    float TdotL = dot(Ts, L);
    float TdotV = dot(Ts, V);
    float sinTL = sqrt(max(0.0, 1.0 - TdotL * TdotL));
    float sinTV = sqrt(max(0.0, 1.0 - TdotV * TdotV));
    return pow(max(0.0, sinTL * sinTV - TdotL * TdotV), shininess);
}

// ─────────────────────────────────────────────────────────────────────────────

void main() {

    // ── Sample packed data texture ────────────────────────────────────────────
    float alpha     = 1.0;
    float rootToTip = 0.5;
    float strandId  = 0.5;

    if (material.hasHairDataTexture > 0.5) {
        vec3 data = texture(hairDataTex, v_uv).rgb;
        alpha     = data.r;
        rootToTip = data.g;
        strandId  = data.b;
    }

    // ── Alpha test ────────────────────────────────────────────────────────────
    if (alpha < material.alphaThreshold) discard;

    // ── Base hair color ───────────────────────────────────────────────────────
    // Blend root and tip colors by the root-to-tip gradient (G channel),
    // then multiply by the global hair color multiplier.
    vec3  baseColor = mix(material.rootColor, material.tipColor, rootToTip) * material.hairColor;
    // Per-strand color variation driven by strand ID (B channel)
    float variation = 1.0 + (strandId - 0.5) * 2.0 * material.colorVariation;
    baseColor      *= clamp(variation, 0.5, 2.0);

    // ── Surface normal ────────────────────────────────────────────────────────
    vec3 N;
    if (material.hasNormalTexture > 0.5) {
        N = normalize(v_TBN * (texture(normalTex, v_uv).rgb * 2.0 - 1.0));
    } else {
        // Cylindrical normal warping: rotate the flat polygon normal around the
        // tangent axis based on the U coordinate, faking a rounded cross-section
        // so each card catches light as if it were a tube of hair.
        const float PI = 3.14159265358979;
        float angle = (v_uv.x - 0.5) * PI;
        N = normalize(cos(angle) * v_TBN[2] + sin(angle) * v_TBN[1]);
    }

    // ── Bump mapping from alpha channel ──────────────────────────────────────
    // Treat the R channel (opacity) as a height map and derive per-pixel normal
    // perturbations.  This breaks up the flat-card look by giving each strand
    // cluster a rounded surface profile that catches light individually.
    if (material.hasHairDataTexture > 0.5) {
        const float bumpScale = 3.0;
        vec2  texelSize = 1.0 / vec2(textureSize(hairDataTex, 0));
        float hL = texture(hairDataTex, v_uv - vec2(texelSize.x, 0.0)).r;
        float hR = texture(hairDataTex, v_uv + vec2(texelSize.x, 0.0)).r;
        float hD = texture(hairDataTex, v_uv - vec2(0.0, texelSize.y)).r;
        float hU = texture(hairDataTex, v_uv + vec2(0.0, texelSize.y)).r;
        float dhdU = hR - hL;
        float dhdV = hU - hD;
        N = normalize(N - bumpScale * (dhdU * v_TBN[0] + dhdV * v_TBN[1]));
    }

    // ── Kajiya-Kay setup ──────────────────────────────────────────────────────
    vec3  T        = v_TBN[0];          // Fiber tangent direction (view space)
    vec3  V        = normalize(-v_pos); // View direction toward camera
    float shininess = max(1.0, (1.0 - material.roughness) * 128.0);

    // ── Per-light loop ────────────────────────────────────────────────────────
    vec3 color      = vec3(0.0);
    vec3 diffuseIrr = vec3(0.0);
    vec3 backIrr    = vec3(0.0);

    const float distortion        = 0.2;
    const float backRadiancePower = 5.0;
    const float backRadianceScale = 2.0;
    const float ambientBack       = 0.05;

    for (int i = 0; i < scene.numLights; i++) {
        if (!isInAreaOfInfluence(scene.lights[i], v_pos)) continue;

        vec3 wi       = scene.lights[i].type != DIRECTIONAL_LIGHT
                            ? normalize(scene.lights[i].position - v_pos)
                            : normalize(scene.lights[i].position.xyz);
        vec3 radiance = scene.lights[i].color
                        * computeAttenuation(scene.lights[i], v_pos)
                        * scene.lights[i].intensity;

        // Kajiya-Kay diffuse + specular
        float diff    = hairDiffuse(T, wi, N);
        float spec    = hairSpecular(T, wi, V, N, material.specularShift, shininess);
        vec3  specColor = mix(baseColor, vec3(1.0), 0.3); // 70% hair-colored, 30% white
        vec3  lighting  = (baseColor * diff + specColor * spec * material.specularIntensity) * radiance;

        // Transmittance: forward scattering through hair volume.
        // Light wraps around and through thin hair fibers, visible as a warm
        // glow at edges and thin regions. Uses view-light alignment (not just
        // back-face) so it works regardless of light placement.
        vec3  HT        = normalize(wi + N * 0.3);           // distorted half-vector
        float forwardSc = pow(clamp(dot(V, -HT), 0.0, 1.0), 3.0); // forward scatter lobe
        float thickness = clamp(alpha, 0.1, 1.0);
        vec3  transColor = baseColor * 1.5;                   // slightly boosted hair color
        lighting        += transColor * forwardSc * (1.0 - thickness * 0.6) * radiance;

        // Shadow factor
        float shadowFactor = 1.0;
        if (int(object.otherParams.y) == 1 && scene.lights[i].shadowCast == 1) {
            if (scene.lights[i].shadowType == 0)
                shadowFactor = computeShadow(shadowMap, scene.lights[i], i, v_modelPos);
            if (scene.lights[i].shadowType == 1)
                shadowFactor = computeVarianceShadow(shadowMap, scene.lights[i], i, v_modelPos);
            // shadowType == 2 (raytraced) not supported; shadowFactor stays 1.0
        }
        lighting *= shadowFactor;

        color += lighting;
        diffuseIrr += max(dot(N, wi), 0.0) * radiance * shadowFactor;

        // Back-lighting irradiance (used by the SSS post-process pass)
        vec3  HBack = wi + N * distortion;
        float VDotH = pow(clamp(dot(V, -HBack), EPSILON, 1.0), backRadiancePower) * backRadianceScale;
        backIrr    += radiance * (VDotH + ambientBack);
    }

    // ── Ambient ───────────────────────────────────────────────────────────────
    vec3 ambientColor;
    if (scene.useIBL)
        ambientColor = computeAmbient(irradianceMap, scene.envRotation, v_modelNormal,
                                      normalize(camera.position.xyz - v_modelPos),
                                      baseColor, vec3(0.04), 0.0, material.roughness,
                                      scene.ambientIntensity);
    else
        ambientColor = scene.ambientIntensity * scene.ambientColor * baseColor;

    color += ambientColor;

    // ── Back-lighting contribution ───────────────────────────────────────────
    // Since SSS is disabled for hair cards (scatterMask = 0), the backIrr MRT
    // output is ignored by the post-process pass.  Fold it directly into the
    // final color so the hair shows translucent glow from behind.
    color += backIrr * baseColor * 0.3;

    // ── Fog ───────────────────────────────────────────────────────────────────
    if (int(object.otherParams.x) == 1 && scene.enableFog) {
        float f = computeFog(gl_FragCoord.z);
        color   = f * color + (1.0 - f) * scene.fogColor.rgb;
    }

    // ── MRT outputs ───────────────────────────────────────────────────────────
    outColor = vec4(color, 1.0);

    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
    outBrightColor   = brightness > 1.0 ? vec4(color, 1.0) : vec4(0.0, 0.0, 0.0, 1.0);

    outNormals     = vec4(N, 0.0);
    outAlbedoMask  = vec4(baseColor, 0.0); // scatterMask = 0 → skip SSS (hair, not skin)
    outDiffuseIrr  = vec4(diffuseIrr, 0.0);
    outBackIrr     = vec4(backIrr, 0.0);
    outLinearDepth = vec4(gl_FragCoord.z, 0.0, 0.0, 0.0);
}
