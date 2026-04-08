#shader vertex
#version 460

layout(location = 0) in vec3 pos;
layout(location = 2) in vec2 uv;

layout(location = 0) out vec2 v_uv;

void main() {
    gl_Position = vec4(pos, 1.0);
    v_uv = uv;
}

#shader fragment
#version 460

layout(location = 0) in vec2 v_uv;

// Binding 0: linear depth  (gl_FragCoord.z stored in R channel)
layout(set = 0, binding = 0) uniform sampler2D depthTex;
// Binding 1: view-space normals (from forward pass MRT loc 2)
layout(set = 0, binding = 1) uniform sampler2D normalsTex;
// Binding 2: small random noise texture (4x4 RGBA, tiled over screen)
layout(set = 0, binding = 2) uniform sampler2D noiseTex;
// Binding 3: SSAO parameters
layout(set = 0, binding = 3) uniform SSAOBlock {
    vec4  samples[64];   // hemisphere kernel (w component unused)
    mat4  projection;
    mat4  invProjection;
    vec2  screenSize;
    float radius;
    float bias;
    int   kernelSize;
    float _pad[3];
} ssao;

layout(location = 0) out vec4 outAOThickness; // R = AO, G = thickness

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Reconstruct view-space position from a NDC depth value [0,1] and screen UV [0,1].
// Uses perspectiveRH_ZO convention (Vulkan / GLM default for this engine).
vec3 reconstructPosition(vec2 uv, float depth) {
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = ssao.invProjection * clipPos;
    return viewPos.xyz / viewPos.w;
}

// ---------------------------------------------------------------------------
// Hemisphere sampling
// ---------------------------------------------------------------------------

float occlusion(vec3 fragPos, vec3 normal, vec2 uv) {
    vec2 noiseScale = ssao.screenSize / 4.0; // tile the 4x4 noise over the screen
    vec3 randomVec  = normalize(vec3(texture(noiseTex, uv * noiseScale).rg * 2.0 - 1.0, 0.0));

    // Build a TBN matrix that orients the hemisphere along the surface normal
    vec3 tangent   = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN       = mat3(tangent, bitangent, normal);

    float occ = 0.0;
    for (int i = 0; i < ssao.kernelSize; ++i)
    {
        // Transform kernel sample into view space
        vec3 samplePos = TBN * ssao.samples[i].xyz;
        samplePos = fragPos + samplePos * ssao.radius;

        // Project sample to get screen UV
        vec4 offset = ssao.projection * vec4(samplePos, 1.0);
        offset.xyz /= offset.w;
        offset.xy = offset.xy * 0.5 + 0.5; // NDC [-1,1] -> UV [0,1]

        // Sample actual geometry depth at that screen position
        float sampleDepth   = texture(depthTex, offset.xy).r;
        vec3  sampleViewPos = reconstructPosition(offset.xy, sampleDepth);

        // Range check: ignore samples whose geometry is too far from the fragment
        float rangeCheck = smoothstep(0.0, 1.0, ssao.radius / max(abs(fragPos.z - sampleViewPos.z), 0.001));

        // If actual surface is closer to camera (larger Z in RH view space) → occluded
        occ += (sampleViewPos.z >= samplePos.z + ssao.bias ? 1.0 : 0.0) * rangeCheck;
    }

    return 1.0 - (occ / float(ssao.kernelSize));
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

void main() {
    float depth = texture(depthTex, v_uv).r;

    // Background / skybox — cleared depth is 1.0.  Output no occlusion / zero thickness.
    if (depth >= 1.0) {
        outAOThickness = vec4(1.0, 0.0, 0.0, 0.0);
        return;
    }

    vec3 fragPos = reconstructPosition(v_uv, depth);
    vec3 normal  = normalize(texture(normalsTex, v_uv).xyz);

    float ao    = occlusion(fragPos, normal, v_uv);
    float thick = 1.0 - occlusion(fragPos, -normal, v_uv);

    outAOThickness = vec4(ao, thick, 0.0, 0.0);
}
