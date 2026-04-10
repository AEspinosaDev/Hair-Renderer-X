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
layout(set = 0, binding = 8) uniform sampler2D scatterDistLUT; // 5-pixel LUT: thin→thick (sRGB)

// --- Uniform block (std140, binding 6) --------------------------------------
layout(set = 0, binding = 6) uniform SSSBlock {
    // xy = (theta, r), zw = unused (vec4 for std140 array padding)
    vec4  samples[64];

    int   sampleCount;
    float maxScatter;       // world-space scatter radius scale
    float extinctionCoeff;  // Beer-Lambert extinction coefficient
    float Fdr;              // internal Fresnel diffuse reflectance

    vec2  screenSize;

    layout(offset = 1056) mat4 projection;
    mat4 invProjection;
} sss;

// --- Outputs ----------------------------------------------------------------
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outBright; // pass-through to Bloom

// ----------------------------------------------------------------------------
const float PI  = 3.14159265358979323846;
const float EPS = 1e-6;

// Sample one of the 5 LUT pixels (sRGB → linear)
vec3 sampleProfile(int i) {
    float u = (float(i) + 0.5) / 5.0;
    return pow(texture(scatterDistLUT, vec2(u, 0.5)).rgb, vec3(2.2));
}

// Thickness-based blend across the 5 scatter distance profiles
vec3 scatterDistanceBlend(float thickness) {
    float t      = clamp(thickness, 0.0, 1.0);
    float scaled = clamp(t * 4.0, 0.0, 4.0);
    int   i0     = int(floor(scaled));
    int   i1     = min(i0 + 1, 4);
    float w      = scaled - float(i0);
    return mix(sampleProfile(i0), sampleProfile(i1), w);
}

// High-quality Interleaved Gradient Noise
float interleavedGradientNoise(vec2 pix) {
    return fract(52.9829189 * fract(dot(pix, vec2(0.06711056, 0.00583715))));
}

vec3 Rr(vec3 d, float r) // Burley's Normalized Diffusion Model
{ 
  return (exp(-r / d) + exp(-r / (d * 3.0))) / (8.0 * PI * d * r);
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

	if (sss.sampleCount == 0) {
        outColor  = texture(hdrTex, v_uv) * ao;
        outBright = texture(brightTex, v_uv);
        return;
    }

    if (depth >= 1.0) {   // background — should not happen for masked pixels, be safe
        outColor  = hdr;
        outBright = texture(brightTex, v_uv);
        return;
    }

    vec3 blendedScatterDist = scatterDistanceBlend(thick);
    vec3 scatterDist = blendedScatterDist * sss.maxScatter;
	float maxRadius = max(scatterDist.r, max(scatterDist.g, scatterDist.b));

    // -------------------------------------------------------------------------
    // Multiple scattering
    //
    // The forward pass HDR buffer contains:
    //   hdr = albedo * diffIrr + specular + ambient
    //
    // We replace the local diffuse term with the scattered diffuse integral
    // and keep everything else (specular, ambient) unchanged.
    // -------------------------------------------------------------------------
    vec3 nonDiffuse = max(hdr.rgb - albedo * diffIrr, vec3(0.0)); // specular + ambient

	// View fragment position computation (perspectiveRH_ZO: depth already in [0,1])
  	vec2 fragCoords = v_uv * 2.0 - vec2(1.0);
  	vec4 viewSpacePos = sss.invProjection * vec4(fragCoords, depth, 1.0);
  	vec3 fragViewPos = viewSpacePos.xyz / viewSpacePos.w;

    float jitter = 2.0 * PI * interleavedGradientNoise(gl_FragCoord.xy);

    vec3 scatteredIrr = vec3(0.0);
    vec3 totalWeight  = vec3(0.0);

    for (int i = 0; i < sss.sampleCount; i++) {
		float theta = sss.samples[i].x + jitter;
        float r     = sss.samples[i].y * maxRadius;
        vec2  sampleOffset  = vec2(cos(theta), sin(theta)) * r;

        // Offset the view-space fragment position in the XY plane (lateral scatter)
        vec4 sampleProjected = sss.projection * vec4(fragViewPos + vec3(sampleOffset, 0.0), 1.0);
        vec2 sampleCoords = sampleProjected.xy / sampleProjected.w;
        vec2 sampleUV = (sampleCoords + 1.0) * 0.5;

		float sampleDepth = texture(depthTex, sampleUV).r;
    	viewSpacePos = sss.invProjection * vec4(sampleCoords, sampleDepth, 1.0);
    	vec3 sampleViewPos = viewSpacePos.xyz / viewSpacePos.w;

    	float radialDistance = max(distance(sampleViewPos, fragViewPos), EPS);

        // Per-channel Burley weight — normalized below to handle spectral bias
        vec3 rRr = radialDistance * Rr(scatterDist, radialDistance);
		vec3 pr = r * Rr(vec3(maxRadius), r);
		vec3 diffusion = rRr / pr;
		totalWeight += diffusion;

		vec3 sampleDiffIrr = texture(diffuseIrrTex, sampleUV).rgb;
        scatteredIrr += diffusion * sampleDiffIrr;
    }

    // Normalize per channel
	scatteredIrr = albedo * (scatteredIrr / max(totalWeight, vec3(EPS)));

    // Modulate by AO
    scatteredIrr *= ao;

    // -------------------------------------------------------------------------
    // Single scattering (translucency via Beer-Lambert)
    // -------------------------------------------------------------------------
    float transmittance = exp(-thick * sss.extinctionCoeff);
    vec3  singleScatter = pow(blendedScatterDist, vec3(2.2)) * transmittance * backIrr;

    // -------------------------------------------------------------------------
    // Combine and output
    // -------------------------------------------------------------------------
    outColor  = vec4(nonDiffuse + scatteredIrr + singleScatter, hdr.a);
    outBright = texture(brightTex, v_uv);
}
