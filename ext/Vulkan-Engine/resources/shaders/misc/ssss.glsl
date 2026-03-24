/*
    Screen-Space Subsurface Scattering shader.

    Multiple scattering:
      Disk kernel with Burley normalized diffusion profile R(r,d).
      CPU samples are importance-sampled from the radial CDF so equal
      weights are used here; per-channel normalization corrects spectral bias.

    Single scattering (translucency):
      Beer-Lambert: T = exp(-thickness * extinctionCoeff)
      Applied to back-facing irradiance modulated by (1 - Fdr).

    Pixels with scatterMask == 0 pass through unchanged.
*/

#shader vertex
#version 460

layout(location = 0) in vec3 pos;
layout(location = 2) in vec2 uv;

layout(location = 0) out vec2 v_uv;

void main() {
    gl_Position = vec4(pos, 1.0);
    v_uv        = uv;
}

// ----------------------------------------------------------------------------
#shader fragment
#version 460

layout(location = 0) in vec2 v_uv;

// --- Input textures (all from ForwardPass MRTs + SSAO pass) -----------------
layout(set = 0, binding = 0) uniform sampler2D hdrTex;        // full HDR shading
layout(set = 0, binding = 1) uniform sampler2D albedoMaskTex; // RGB = albedo, A = scatterMask
layout(set = 0, binding = 2) uniform sampler2D diffuseIrrTex; // front diffuse irr (no albedo)
layout(set = 0, binding = 3) uniform sampler2D backIrrTex;    // back diffuse irr (flipped N)
layout(set = 0, binding = 4) uniform sampler2D depthTex;      // R = gl_FragCoord.z
layout(set = 0, binding = 5) uniform sampler2D aoThickTex;    // R = AO, G = thickness
layout(set = 0, binding = 7) uniform sampler2D brightTex;     // Bright pass-through for Bloom

// --- Uniform block (std140, binding 6) --------------------------------------
layout(set = 0, binding = 6) uniform SSSBlock {
    // xy = (r [importance-sampled from Burley CDF], theta [golden angle])
    // zw = unused (vec4 for std140 array padding)
    vec4  samples[25];

    int   sampleCount;
    float maxScatter;       // world-space scatter radius scale
    float extinctionCoeff;  // Beer-Lambert extinction coefficient
    float Fdr;              // internal Fresnel diffuse reflectance

    vec4  scatteringDistance; // rgb = per-channel scatter distance in world units

    vec2  screenSize;
    float _pad0[2];

    mat4 projection;
    mat4 invProjection;
} sss;

// --- Outputs ----------------------------------------------------------------
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outBright; // pass-through to Bloom

// ----------------------------------------------------------------------------
const float PI  = 3.14159265358979323846;
const float EPS = 0.001;

// Burley normalized diffusion profile (per-channel)
//   R(r, d) = (exp(-r/d) + exp(-r/(3d))) / (8π r d)
// Returns the unnormalized weight vector for importance-sampling normalization.
vec3 burleyWeight(float r, vec3 d) {
    r = max(r, EPS);
    d = max(d, vec3(EPS));
    return (exp(-r / d) + exp(-r / (3.0 * d))) / (8.0 * PI * r * d);
}

// Reconstruct view-space position from screen UV and depth.
// Engine uses perspectiveRH_ZO so depth is already in [0,1] — no remapping.
vec3 reconstructPosition(vec2 uv, float depth) {
    vec4 clip    = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = sss.invProjection * clip;
    return viewPos.xyz / viewPos.w;
}

// ----------------------------------------------------------------------------
void main() {
    vec4  albedoMask  = texture(albedoMaskTex, v_uv);
    float scatterMask = albedoMask.a;

    // Non-skin pixels (hair, unlit, sky) — pass through unchanged
    if (scatterMask == 0.0) {
        outColor  = texture(hdrTex, v_uv);
        outBright = texture(brightTex, v_uv);
        return;
    }

    vec3  albedo  = albedoMask.rgb;
    vec4  hdr     = texture(hdrTex, v_uv);
    vec3  diffIrr = texture(diffuseIrrTex, v_uv).rgb;
    vec3  backIrr = texture(backIrrTex, v_uv).rgb;
    float depth   = texture(depthTex, v_uv).r;
    vec2  aoThick = texture(aoThickTex, v_uv).rg;
    float ao      = aoThick.r;
    float thick   = aoThick.g;

    if (depth >= 1.0) {   // background — should not happen for masked pixels, be safe
        outColor  = hdr;
        outBright = texture(brightTex, v_uv);
        return;
    }

    vec3 fragPos     = reconstructPosition(v_uv, depth);
    vec3 scatterDist = sss.scatteringDistance.rgb;

    // -------------------------------------------------------------------------
    // Multiple scattering
    //
    // The forward pass HDR buffer contains:
    //   hdr = albedo * diffIrr + specular + ambient
    //
    // We replace the local diffuse term with the scattered diffuse integral
    // and keep everything else (specular, ambient) unchanged.
    // -------------------------------------------------------------------------
    vec3 nonDiffuse = hdr.rgb - albedo * diffIrr; // specular + ambient

    vec3 scatteredIrr = vec3(0.0);
    vec3 totalWeight  = vec3(0.0);

    for (int i = 0; i < sss.sampleCount; i++) {
        // r is in units of scatter distance; scale to world/view space
        float r     = sss.samples[i].x * sss.maxScatter;
        float theta = sss.samples[i].y;
        vec2  disk  = vec2(cos(theta), sin(theta)) * r;

        // Offset the view-space fragment position in the XY plane (lateral scatter)
        vec4 sampleClip = sss.projection * vec4(fragPos + vec3(disk, 0.0), 1.0);
        sampleClip.xyz /= sampleClip.w;
        vec2 sampleUV = clamp(sampleClip.xy * 0.5 + 0.5, vec2(0.0), vec2(1.0));

        vec3 sampleAlbedo  = texture(albedoMaskTex, sampleUV).rgb;
        vec3 sampleDiffIrr = texture(diffuseIrrTex, sampleUV).rgb;

        // Per-channel Burley weight — normalised below to handle spectral bias
        vec3 w = burleyWeight(r, scatterDist);

        scatteredIrr += sampleAlbedo * sampleDiffIrr * w;
        totalWeight  += w;
    }

    // Normalize per channel
    scatteredIrr /= max(totalWeight, vec3(EPS));

    // Modulate by AO
    scatteredIrr *= ao;

    // -------------------------------------------------------------------------
    // Single scattering (translucency via Beer-Lambert)
    // -------------------------------------------------------------------------
    float transmittance = exp(-thick * sss.extinctionCoeff);
    vec3  singleScatter = (1.0 - sss.Fdr) * albedo * backIrr * transmittance;

    // -------------------------------------------------------------------------
    // Combine and output
    // -------------------------------------------------------------------------
    outColor  = vec4(nonDiffuse + scatteredIrr + singleScatter, hdr.a);
    outBright = texture(brightTex, v_uv);
}
