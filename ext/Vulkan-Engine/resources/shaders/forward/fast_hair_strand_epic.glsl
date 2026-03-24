#shader vertex
#version 460 core
#extension GL_EXT_nonuniform_qualifier : require
#include object.glsl
#include camera.glsl

layout(std430, set = 3, binding = 0) readonly buffer VertexBuffer {
    vec4 pos[];
} vertexBuffer[];

layout(std430, set = 3, binding = 1) readonly buffer IndexBuffer {
    uint index[];
} indexBuffer[];

layout(push_constant) uniform ObjectConstants {
    float id;
    float numSegments;
    float thickness;
    float avgFiberLength;
} constants;

// Output
layout(location = 0) out vec3 _pos;
layout(location = 1) out vec3 _modelPos;
layout(location = 2) out vec2 _uv;
layout(location = 3) out vec3 _dir;
layout(location = 4) out vec3 _modelDir;

// helpers
vec3 safeNormalize(vec3 v, vec3 fallback) {
    float l = length(v);
    return (l > 1e-6) ? (v / l) : fallback;
}
vec3 safePerp(vec3 a, vec3 b) {
    vec3 r = cross(a, b);
    if (length(r) < 1e-6) {
        // choose any vector not parallel to a
        vec3 alt = abs(a.x) < 0.9 ? vec3(1,0,0) : vec3(0,1,0);
        r = cross(a, alt);
        if (length(r) < 1e-6) r = vec3(1,0,0);
    }
    return normalize(r);
}

void main() {
    uint segmentId = gl_InstanceIndex;
    uint meshId    = nonuniformEXT(uint(constants.id));
    uint base      = segmentId * 2u;

    // bounds-safety: avoid reading out-of-range if segmentId too large
    // (assume caller ensures instanceCount = numSegments)
    uint idx0 = indexBuffer[meshId].index[base];
    uint idx1 = indexBuffer[meshId].index[base + 1u];
    uint idx2 = indexBuffer[meshId].index[min(base + 2u, base + 1u)];

    vec3 p0 = (object.model * vertexBuffer[meshId].pos[idx0]).xyz;
    vec3 p1 = (object.model * vertexBuffer[meshId].pos[idx1]).xyz;
    vec3 p2 = (object.model * vertexBuffer[meshId].pos[idx2]).xyz;

    vec3 view0 = safeNormalize(camera.position.xyz - p0, vec3(0.0,0.0,1.0));
    vec3 view1 = safeNormalize(camera.position.xyz - p1, vec3(0.0,0.0,1.0));

    vec3 rawDir0 = p1 - p0;
    vec3 rawDir1 = p2 - p1;

    vec3 dir0 = safeNormalize(rawDir0, vec3(0.0,0.0,1.0));
    // if dir1 is degenerate fallback to dir0
    vec3 dir1 = safeNormalize(rawDir1, dir0);

    // compute billboard right vectors (stable)
    vec3 right0 = safePerp(dir0, view0);
    vec3 right1 = safePerp(dir1, view1);

    // choose vertex within the quad (0..3)
    int v = int(gl_VertexIndex & 3);
    bool isStart = (v < 2);
    bool isRight = (v == 1 || v == 3);

    if (isStart) {
        _modelPos = p0 + right0 * (isRight ? constants.thickness : -constants.thickness);
        _modelDir = dir0;
    } else {
        _modelPos = p1 + right1 * (isRight ? constants.thickness : -constants.thickness);
        _modelDir = dir1; 
    }

    // transform dir into view space safely
    _dir = safeNormalize((camera.view * vec4(_modelDir, 0.0)).xyz, vec3(0.0,0.0,1.0));

    vec4 viewPos = camera.view * vec4(_modelPos, 1.0);
    _pos = viewPos.xyz;
    _uv  = vec2(float(isRight), float(!isStart));

    gl_Position = camera.viewProj * vec4(_modelPos, 1.0);
}


#shader fragment
#version 460 core
#include light.glsl
#include scene.glsl
#include camera.glsl
#include object.glsl
#include utils.glsl
#include shadow_mapping.glsl
#include reindhart.glsl
#include sh.glsl
#include BRDFs/epic_hair_BSDF.glsl

// Input
layout(location = 0) in vec3 g_pos;
layout(location = 1) in vec3 g_modelPos;
layout(location = 2) in vec2 g_uv;
layout(location = 3) in vec3 g_dir;
layout(location = 4) in vec3 g_modelDir;

// Uniforms
layout(set = 0, binding = 2) uniform sampler2DArray shadowMap;
layout(set = 0, binding = 4) uniform samplerCube irradianceMap;

layout(set = 0, binding = 9) uniform sampler3D hairVoxelsSh;
layout(set = 0, binding = 13) uniform sampler3D hairVoxelsDensity;
layout(set = 0, binding = 12) uniform sampler3D hairLUT;



layout(push_constant) uniform Data {
    float id;
    float numSegments;
    float thickness;
    float avgFiberLength;
} data;

layout(set = 1, binding = 1) uniform MaterialUniforms {
    vec3  baseColor;
    float thickness;

    float roughness;
    float metallic;
    float specular;
    float shift;

    float ior;
    float Rpower;
    float TTpower;
    float TRTpower;

    float opaqueVisibility;
    float useLegacyAbsorption;
    float useSeparableR;
    float useBacklit;

    float clampBSDFValue;
    float r;
    float tt;
    float trt;

    float scatter;
    float densityBoost;
    float advShadows;
}
material;

EpicHairBSDF bsdf;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 outBrightColor;
layout(location = 2) out vec4 outNormals;
layout(location = 3) out vec4 outAlbedoMask;
layout(location = 4) out vec4 outDiffuseIrr;
layout(location = 5) out vec4 outBackIrr;
layout(location = 6) out vec4 outLinearDepth;

vec3 computeAmbient(vec3 n) {

    vec3 ambient;
    if (scene.useIBL)
    {
        float rad           = radians(scene.envRotation);
        float c             = cos(rad);
        float s             = sin(rad);
        mat3  rotationY     = mat3(c, 0.0, -s, 0.0, 1.0, 0.0, s, 0.0, c);
        vec3  rotatedNormal = normalize(rotationY * n);

    } else
    {
        ambient = (scene.ambientIntensity * scene.ambientColor);
    }
    return ambient;
}

// Anysotropic. Decoding from a L1 SH
float getOpticalDensity(vec3 worldPos, vec3 lightWorldPos) {
    vec3 dir = normalize(lightWorldPos - worldPos);

    // Compute voxel UVW coords in object space
    vec3 uvw = (worldPos - object.minCoord.xyz) / (object.maxCoord.xyz - object.minCoord.xyz);
    uvw      = clamp(uvw, 0.0, 0.9999);

    // Fetch SH L1 and decode
    // ivec3 coord = ivec3(uvw * vec3(textureSize(hairVoxelsSh, 0)));
    // vec4 SHL1 = texelFetch(hairVoxelsSh, coord, 0);
    vec4 SHL1 = texture(hairVoxelsSh, uvw, 0);

    return decodeScalarFromSHL1(SHL1, dir);
}

//////////////////////////////////////////////////////////////////////////
// Special shadow mapping for hair for controlling density
//////////////////////////////////////////////////////////////////////////

float computeHairShadowCone(vec3 worldPos, vec3 lightDir) {
    vec3 boundsMin = object.minCoord.xyz;
    vec3 boundsMax = object.maxCoord.xyz;
    vec3 boxSize   = boundsMax - boundsMin;

    vec3 texPos = (worldPos - boundsMin) / boxSize;

    float t         = 0.001;
    float tMax      = 1.0;
    float sigma     = 0.7;
    float coneAngle = 0.02;
    float trans     = 1.0;

    const int MAX_STEPS = 64;          // drastically fewer now
    float     step      = 1.0 / 256.0; // start near one voxel

    for (int i = 0; i < MAX_STEPS && trans > 0.001; i++)
    {
        float radius = coneAngle * t;
        float mip    = clamp(log2(max(radius * 256.0, 1e-4)), 0.0, 3.0);

        vec3  samplePos = texPos + lightDir * t;
        float dens      = textureLod(hairVoxelsDensity, samplePos, mip).r;

        // exponential attenuation
        trans *= exp(-dens * sigma * step * 256.0);

        // exponential step growth
        t += step;
        step *= 1.05;

        if (t > tMax)
            break;
    }

    return trans;
}

float computeHairShadowDDA(vec3 worldPos, vec3 lightDir) {
    vec3 boundsMin = object.minCoord.xyz;
    vec3 boundsMax = object.maxCoord.xyz;

    ivec3 dim       = textureSize(hairVoxelsDensity, 0);
    vec3  gridSize  = vec3(dim);
    vec3  invBounds = 1.0 / (boundsMax - boundsMin);

    // Convert world → voxel coords
    vec3 startV  = (worldPos - boundsMin) * invBounds * gridSize;
    vec3 rayDirV = normalize(lightDir) * gridSize * 0.5; // scaled voxel ray step

    // Compute DDA parameters
    ivec3 voxel = ivec3(floor(startV));
    ivec3 step  = ivec3(sign(rayDirV));

    vec3 tMax;
    vec3 tDelta = abs(1.0 / rayDirV);

    for (int axis = 0; axis < 3; axis++)
    {
        float nextBoundary = (step[axis] > 0) ? (float(voxel[axis] + 1) - startV[axis]) : (startV[axis] - float(voxel[axis]));

        tMax[axis] = nextBoundary * tDelta[axis];
    }

    float accum    = 0.0;
    int   maxSteps = 256; // cheap! this is shadow, not SH baking

    for (int i = 0; i < maxSteps; i++)
    {
        if (voxel.x < 0 || voxel.y < 0 || voxel.z < 0 || voxel.x >= dim.x || voxel.y >= dim.y || voxel.z >= dim.z)
            break;

        // float d = texelFetch(hairVoxelsDensity, voxel, 0).r;

        vec3  worldV = (vec3(voxel) + 0.5) / gridSize;
        float d      = textureLod(hairVoxelsDensity, worldV, 0.0).r;
        // if(i>0)
        accum += d;

        // Step voxel
        if (tMax.x < tMax.y)
        {
            if (tMax.x < tMax.z)
            {
                voxel.x += step.x;
                tMax.x += tDelta.x;
            } else
            {
                voxel.z += step.z;
                tMax.z += tDelta.z;
            }
        } else
        {
            if (tMax.y < tMax.z)
            {
                voxel.y += step.y;
                tMax.y += tDelta.y;
            } else
            {
                voxel.z += step.z;
                tMax.z += tDelta.z;
            }
        }
    }

    return accum;
}

float bilinear(float v[4], vec2 f) {
    return mix(mix(v[0], v[1], f.x), mix(v[2], v[3], f.x), f.y);
}

vec3 bilinear(vec3 v[4], vec2 f) {
    return mix(mix(v[0], v[1], f.x), mix(v[2], v[3], f.x), f.y);
}
vec3 hairShadow(out vec3 spread, out float directF, vec3 pShad, sampler2DArray shadowMap, int lightId, float density) {
    ivec2 size = textureSize(shadowMap, 0).xy;
    vec2  t    = pShad.xy * vec2(size) + 0.5;
    vec2  f    = t - floor(t);
    vec2  s    = 0.5 / vec2(size);

    vec2 tcp[4];
    tcp[0] = pShad.xy + vec2(-s.x, -s.y);
    tcp[1] = pShad.xy + vec2(s.x, -s.y);
    tcp[2] = pShad.xy + vec2(-s.x, s.y);
    tcp[3] = pShad.xy + vec2(s.x, s.y);

    const float coverage = 0.05;
    const vec3  a_f      = vec3(0.507475266, 0.465571405, 0.394347166);
    const vec3  w_f      = vec3(0.028135575, 0.027669785, 0.027669785);
    float       dir[4];
    vec3        spr[4], t_d[4];
    for (int i = 0; i < 4; ++i)
    {
        float z = texture(shadowMap, vec3(tcp[i], lightId)).r;
        float h = max(0.0, pShad.z - z);
        float n = h * density * 10000.0;
        dir[i]  = pow(1.0 - coverage, n);
        t_d[i]  = pow(1.0 - coverage * (1.0 - a_f), vec3(n, n, n));
        spr[i]  = n * coverage * w_f;
    }

    directF = bilinear(dir, f);
    spread  = bilinear(spr, f);
    return bilinear(t_d, f);
}

vec3 computeHairShadow(LightUniform light, int lightId, sampler2DArray shadowMap, float density, vec3 pos, out vec3 spread, out float directF) {
    vec4 posLightSpace = light.viewProj * vec4(pos, 1.0);
    vec3 projCoords    = posLightSpace.xyz / posLightSpace.w;
    projCoords.xy      = projCoords.xy * 0.5 + 0.5;

    vec3 transDirect = hairShadow(spread, directF, projCoords, shadowMap, lightId, density);
    directF *= 0.5;
    return transDirect * 0.5;
}

void main() {

    // BSDF setup ............................................................
    //  bsdf.baseColor = material.baseColor;
    bsdf.baseColor = material.baseColor;

    bsdf.roughness = material.roughness;
    bsdf.metallic  = material.metallic;
    bsdf.specular  = material.specular;

    bsdf.shift = material.shift;
    bsdf.ior   = material.ior;

    bsdf.Rpower   = material.Rpower;
    bsdf.TTpower  = material.TTpower;
    bsdf.TRTpower = material.TRTpower;

    bsdf.useLegacyAbsorption = (material.useLegacyAbsorption > 0.5);
    bsdf.useSeparableR       = (material.useSeparableR > 0.5);
    bsdf.useBacklit          = (material.useBacklit > 0.5);

    bsdf.clampBSDFValue = (material.clampBSDFValue > 0.5);

    bsdf.opaqueVisibility = material.opaqueVisibility;

    bsdf.localScattering  = vec3(0.0);
    bsdf.globalScattering = vec3(1.0);

    // bsdf.scatteringComponentEnabled = uint(material.scatteringComponentEnabled);

    // DIRECT LIGHTING .......................................................
    vec3 color = vec3(0.0);
    for (int i = 0; i < scene.numLights; i++)
    {
        // If inside liught area influence
        if (isInAreaOfInfluence(scene.lights[i], g_pos))
        {

            vec3  shadow         = vec3(1.0);
            vec3  spread         = vec3(0.0);
            float directFraction = 1.0;
            if (int(object.otherParams.y) == 1 && scene.lights[i].shadowCast == 1)
            {
                if (scene.lights[i].shadowType == 0) // Classic
                    shadow = computeHairShadow(scene.lights[i], i, shadowMap, 0.7, g_modelPos, spread, directFraction);
                if (scene.lights[i].shadowType == 1) // VSM
                    shadow = computeHairShadow(scene.lights[i], i, shadowMap, 0.7, g_modelPos, spread, directFraction);
            }

            vec3  L         = normalize(scene.lights[i].position.xyz - g_pos);
            vec3  V         = normalize(-g_pos);
            vec3  T         = normalize(g_dir);
            float inBacklit = saturate(dot(-L, V));

            // Number of traversed strands
            HairTransmittanceMask transMask;
            float rawCount = getOpticalDensity(g_modelPos, (camera.invView * vec4(scene.lights[i].position, 1.0)).xyz) / max(data.avgFiberLength, 1e-9);
            rawCount *= material.densityBoost;
#define USE_AMANATIDES_WOO_DDA 1
#if USE_AMANATIDES_WOO_DDA
            // Much weaker perceptual curve now
            float k       = 0.6; // instead of 2.0
            float hLog    = log(1.0 + k * rawCount) / log(1.0 + k);
            float hSmooth = pow(hLog, 0.9); // more linear, less softening
#else
            // This is basically a perceptual remap + contrast recovery
            float k       = 2.0;
            float hLog    = log(1.0 + k * rawCount) / log(1.0 + k);
            float hSmooth = pow(hLog, 0.8); // 0.7–0.9 = softer
#endif
            transMask.hairCount = hSmooth;

            transMask.visibility = directFraction;
            transMask.visibility = 1.0;

            if (material.advShadows > 0.0)
            {
                float sigma = 0.5; // tweak ~0.3–1.2 depending on density scale
                // transMask.visibility = exp(-sigma * computeHairShadowDDA(g_modelPos, normalize((camera.invView * vec4(scene.lights[i].position, 1.0)).xyz
                // -g_modelPos)));
                transMask.visibility = computeHairShadowCone(g_modelPos, normalize((camera.invView * vec4(scene.lights[i].position, 1.0)).xyz - g_modelPos));
            }

            bsdf          = evalHairMultipleScattering(V, L, T, transMask, hairLUT, bsdf);
            vec3 lighting = evalEpicHairBSDF(L,
                                             V,
                                             T,
                                             directFraction,
                                             bsdf,
                                             inBacklit,
                                             scene.lights[i].area,
                                             material.r > 0.5,
                                             material.tt > 0.5,
                                             material.trt > 0.5,
                                             material.scatter > 0.5) *
                            scene.lights[i].color * scene.lights[i].intensity;

            color += lighting;
            // if(transMask.hairCount < 1000000.0)
            // color = vec3( transMask.visibility);
        }
    }

    // vec3 n1 = cross(g_modelDir, cross(camera.position.xyz, g_modelDir));
    vec3 fakeNormal = normalize(g_modelPos - object.volumeCenter);
    // vec3 fakeNormal = mix(n1,n2,0.5);

    // AMBIENT COMPONENT ..........................................................

    vec3 ambient = computeAmbient(fakeNormal);
    color += ambient;

    if (int(object.otherParams.x) == 1 && scene.enableFog)
    {
        float f = computeFog(gl_FragCoord.z);
        color   = f * color + (1 - f) * scene.fogColor.rgb;
    }

    //    vec3 color = vec3(41.0,0.0,0.0);

    fragColor = vec4(color, 1.0);
    // check whether result is higher than some threshold, if so, output as bloom threshold color
    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
    if (brightness > 1.0)
        outBrightColor = vec4(color, 1.0);
    else
        outBrightColor = vec4(0.0, 0.0, 0.0, 1.0);

    outNormals     = vec4(0.0);
    outAlbedoMask  = vec4(0.0, 0.0, 0.0, 0.0);
    outDiffuseIrr  = vec4(0.0);
    outBackIrr     = vec4(0.0);
    outLinearDepth = vec4(gl_FragCoord.z, 0.0, 0.0, 0.0);
}