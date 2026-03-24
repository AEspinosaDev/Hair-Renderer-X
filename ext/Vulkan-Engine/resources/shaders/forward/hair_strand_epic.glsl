#shader vertex
#version 460 core
#include object.glsl

// Input
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;
layout(location = 3) in vec3 tangent;
layout(location = 4) in vec3 color;

// Output
layout(location = 0) out vec3 v_color;
layout(location = 1) out vec3 v_tangent;
layout(location = 2) out vec2 v_uv;

void main() {

    gl_Position = object.model * vec4(position, 1.0);

    v_tangent = normalize(mat3(transpose(inverse(object.model))) * tangent);
    v_color   = color;
    v_uv      = uv;
}

#shader geometry
#version 460 core
#include camera.glsl

// Setup
layout(lines) in;
layout(triangle_strip, max_vertices = 4) out;

// Input
layout(location = 0) in vec3 v_color[];
layout(location = 1) in vec3 v_tangent[];
layout(location = 2) in vec2 v_uv[];

// Uniforms
layout(set = 1, binding = 1) uniform MaterialUniforms {
    vec3  baseColor;
    float thickness;
}
material;

// Output
layout(location = 0) out vec3 g_pos;
layout(location = 1) out vec3 g_modelPos;
layout(location = 2) out vec3 g_normal;
layout(location = 3) out vec3 g_modelNormal;
layout(location = 4) out vec2 g_uv;
layout(location = 5) out vec3 g_dir;
layout(location = 6) out vec3 g_modelDir;
layout(location = 7) out vec3 g_color;
layout(location = 8) out vec3 g_origin;
layout(location = 9) out float g_coverage;

void emitQuadPoint(vec4 origin, vec4 right, float offset, vec3 forward, vec3 normal, vec2 uv, int id, float coverage) {

    vec4 newPos   = origin + right * offset; // Model space
    gl_Position   = camera.viewProj * newPos;
    g_dir         = normalize(mat3(transpose(inverse(camera.view))) * v_tangent[id]);
    g_modelDir    = v_tangent[id];
    g_color       = v_color[id];
    g_pos         = (camera.view * newPos).xyz;
    g_modelPos    = newPos.xyz;
    g_uv          = uv;
    g_normal      = normalize(mat3(transpose(inverse(camera.view))) * normal);
    g_modelNormal = normal;
    g_origin      = (camera.view * origin).xyz;
    g_coverage    = coverage;

    EmitVertex();
}

void main() {

    const vec2 VIEWPORT_SIZE = vec2(camera.screenExtent);

    // Model space --->>>

    vec4 startPoint = gl_in[0].gl_Position;
    vec4 endPoint   = gl_in[1].gl_Position;

    vec4 view0 = vec4(camera.position.xyz, 1.0) - startPoint;
    vec4 view1 = vec4(camera.position.xyz, 1.0) - endPoint;

    vec3 dir0 = v_tangent[0];
    vec3 dir1 = v_tangent[1];

    vec4 right0 = normalize(vec4(cross(dir0.xyz, view0.xyz), 0.0));
    vec4 right1 = normalize(vec4(cross(dir1.xyz, view1.xyz), 0.0));

    vec3 normal0 = normalize(cross(right0.xyz, dir0.xyz));
    vec3 normal1 = normalize(cross(right1.xyz, dir1.xyz));

    //<<<----

    float finalAlpha = 1.0;
    // vec4 p0_clip = camera.viewProj * gl_in[0].gl_Position;
    // vec4 p1_clip = camera.viewProj * gl_in[1].gl_Position;

    // // Un punto desplazado por el grosor real (en el startPoint)
    // vec4 p0_offset_clip = camera.viewProj * (gl_in[0].gl_Position + right0 * material.thickness);

    // vec2 p0_screen = (p0_clip.xy / p0_clip.w) * VIEWPORT_SIZE * 0.5;
    // vec2 p0_offset_screen = (p0_offset_clip.xy / p0_offset_clip.w) * VIEWPORT_SIZE * 0.5;
    // float projectedWidthPx = length(p0_screen - p0_offset_screen);

    // float minPixels = 1.0;
    // float scaleFactor = 1.0;

    // if (projectedWidthPx < minPixels && projectedWidthPx > 0.001) {
    //     scaleFactor = minPixels / projectedWidthPx;

    //     // Compensamos con transparencia (Conservación de energía)
    //     finalAlpha = 1.0 / scaleFactor;
    // }

    // // Aplicamos el escalado al grosor físico
    // float correctedHalfLength = (material.thickness * 0.5) * scaleFactor;

    //<<<----

    float halfLength = material.thickness * 0.5;

    emitQuadPoint(startPoint, right0, halfLength, dir0, normal0, v_uv[0], 0, finalAlpha);
    emitQuadPoint(endPoint, right1, halfLength, dir1, normal1, v_uv[1], 1, finalAlpha);
    emitQuadPoint(startPoint, -right0, halfLength, dir0, normal0, v_uv[0], 0, finalAlpha);
    emitQuadPoint(endPoint, -right1, halfLength, dir1, normal1, v_uv[1], 1, finalAlpha);
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
layout(location = 2) in vec3 g_normal;
layout(location = 3) in vec3 g_modelNormal;
layout(location = 4) in vec2 g_uv;
layout(location = 5) in vec3 g_dir;
layout(location = 6) in vec3 g_modelDir;
layout(location = 7) in vec3 g_color;
layout(location = 8) in vec3 g_origin;
layout(location = 9) in float g_coverage;

// Uniforms
layout(set = 0, binding = 2) uniform sampler2DArray shadowMap;
layout(set = 0, binding = 4) uniform samplerCube irradianceMap;

layout(set = 0, binding = 7) uniform sampler3D NpTex;
layout(set = 0, binding = 9) uniform sampler3D hairVoxelsSh;
layout(set = 0, binding = 13) uniform sampler3D hairVoxelsDensity;
layout(set = 0, binding = 12) uniform sampler3D hairLUT;

layout(push_constant) uniform Data {
    float id;
    float avgFiberLength;
}
data;

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
    float scatterKnob;
    float advShadows;
    float shadowKnob;

    float useGlints;
    float rootDarkening;
    float tipBleaching;
    float tipFalloff;

    float variability;
    vec3 tintColor;
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

// Construye una base ortonormal basada en la Tangente y la Vista
// T = Tangente del pelo (g_dir)
// V = Vector Vista (hacia la cámara)
void buildBasis(vec3 T, vec3 V, out vec3 N, out vec3 B) {
    // 1. Binormal (B): Es el vector que va "a lo ancho" de la cinta en pantalla.
    // Es perpendicular a la hebra (T) y a la mirada (V).
    B = normalize(cross(T, V));

    // 2. Normal (N): Es el vector que apunta "hacia fuera" del cilindro.
    // Es perpendicular a la Binormal y a la Tangente.
    N = normalize(cross(B, T));
}
float hash31(vec3 p3) {
    p3 = fract(p3 * .1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
vec3 computeMicroTangent(vec3 T, vec3 V, vec3 worldPos) {

    // --- 1. RECONSTRUIR BASE VIRTUAL ---
    vec3 N, B;
    buildBasis(T, V, N, B);

    // --- 2. CONFIGURACIÓN ---
    // float glintStrength = material.TTpower;    // Fuerza (3)
    // // float glintScale    = 50.0; // Frecuencia (Escamas muy pequeñas)
    // float glintScale    = material.TRTpower; // Frecuencia 150 (Escamas muy pequeñas)
    // float anisotropy    = material.Rpower;   // Estiramiento (0.01 = muy estirado a lo largo)
    float glintStrength = 0.2;  // Fuerza (3)
    float glintScale    = 50.0; // Frecuencia 150 (Escamas muy pequeñas)
    float anisotropy    = 0.01; // Estiramiento (0.01 = muy estirado a lo largo)

    // // --- 3. LOD (Anti-Aliasing) ---
    // // Derivada de posición: ¿Cuánto terreno cubrimos por pixel?
    // float pixelSize = length(fwidth(worldPos));
    // // Si el pixel es más grande que el ruido, apagamos el efecto suavemente
    // float lodFade = 1.0 - smoothstep(0.0, 1.0, pixelSize * glintScale * 0.5);

    // if (lodFade < 0.01) return T;

    // --- 4. COORDENADAS DE RUIDO ---
    // Proyectamos la posición del mundo sobre nuestro cilindro virtual
    float u = dot(worldPos, T); // Largo de la hebra
    float v = dot(worldPos, B); // Ancho de la hebra

    // Estiramos para crear anisotropía (escamas)
    vec3 noiseCoord = vec3(u * anisotropy, v, 0.0) * glintScale;

    // --- 5. RUIDO ---
    float n = hash31(floor(noiseCoord));

    // Mapear [0,1] -> [-1, 1]
    float perturbation = (n * 2.0 - 1.0);

    // --- 6. APLICAR ---
    // Movemos la tangente hacia la Binormal (ancho) y un poco hacia la Normal (profundidad)
    // para dar sensación 3D.
    vec3 T_micro = normalize(T + (B * perturbation) * glintStrength);

    return T_micro;
}

float computeHairShadowCone(vec3 worldPos, vec3 lightDir, vec3 physicalAbsorption, float densityScale) {

    vec3  boundsMin = object.minCoord.xyz;
    vec3  boundsMax = object.maxCoord.xyz;
    vec3  boxSize   = boundsMax - boundsMin;
    float maxBoxDim = max(boxSize.x, max(boxSize.y, boxSize.z));

    ivec3 texDim            = textureSize(hairVoxelsDensity, 0);
    float maxTexDim         = float(max(texDim.x, max(texDim.y, texDim.z)));
    float voxelSizeTexSpace = 1.0 / maxTexDim;

    vec3 startTexPos = (worldPos - boundsMin) / boxSize;

    // Vinculacion con la fisica del pelo
    float materialSigma = max(physicalAbsorption.r, max(physicalAbsorption.g, physicalAbsorption.b));

    // Factor de corrección empírico (porque voxel density != path distance pura)
    // Este número ya no variará tanto entre rubio/moreno, será más estable.
    float dynamicSigma      = materialSigma ;

    float coneAngle    = 0.035;
    float minStepWorld = length(boxSize) * voxelSizeTexSpace * 0.5;

    // 3. INICIO RAYO
    float t     = minStepWorld * 2.0; // No autosombra
    float trans = 1.0;
    float tMax  = 1.75;

    // Nunca avanzar más de un 5% del volumen en un solo paso, aunque el cono sea enorme.
    float maxStepWorld = maxBoxDim * 0.1;

    const int MAX_STEPS = 64;
    for (int i = 0; i < MAX_STEPS &&  trans > 0.01; i++)
    {
        // 1. Calcular posición en textura [0..1]
        vec3 samplePos = startTexPos + ((lightDir * t) / boxSize);

        // 2. CHECK DE SALIDA: Si salimos del volumen, no hay más pelo que ocluya.
        if (any(lessThan(samplePos, vec3(0.0))) || any(greaterThan(samplePos, vec3(1.0))))
        {
            break; 
        }

        // 3. RADIOS SEPARADOS (Mundo vs Textura)
        float coneRadiusWorld = t * tan(coneAngle);            // Radio físico
        float coneRadiusTex   = coneRadiusWorld / maxBoxDim;   // Radio normalizado para el MIP

        // 4. MIP LEVEL
        float mipLevel = log2(max(coneRadiusTex, 1e-6) / voxelSizeTexSpace);
        mipLevel = clamp(mipLevel, 0.0, 3.0);

        // 5. SAMPLEO
        float density = textureLod(hairVoxelsDensity, samplePos, mipLevel).r;

        // 6. TAMAÑO DEL PASO EN UNIDADES DE MUNDO
        // Avanzamos el diámetro del cono (Radius * 2), pero limitándolo por los min/max.
        float stepSize = clamp(coneRadiusWorld * 2.0, minStepWorld, maxStepWorld);

        // 7. BEER-LAMBERT
        if (density > 0.001)
        {
            trans *= exp(-density * dynamicSigma * stepSize * densityScale);
        }

        // 8. AVANZAR
        t += stepSize;
    }

    return trans;
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

void applyNaturalVariation(inout float m, inout float r, float uv_length, float variation) {

    float bleachFactor = pow(uv_length, material.tipFalloff);
    float darkenactor = pow(1.0 - uv_length, material.tipFalloff);

    m += material.rootDarkening *darkenactor; 
    m -= material.tipBleaching * bleachFactor;       

    float strandVariationStrength = material.variability; 

    float randomOffset = (variation * 2.0 - 1.0) * strandVariationStrength;
    m += randomOffset;
    r += randomOffset;

    m = clamp(m, 0.0, 1.0);
    r = clamp(r, 0.0, 1.0);
}

void main() {

    // BSDF setup ............................................................
    float melanin = material.baseColor.x; 
    float redness = material.baseColor.y;
    applyNaturalVariation(melanin, redness, g_uv.x, g_uv.y);
    vec3 physicalSigma = getAbsorptionFromMelanin(melanin, redness, material.baseColor.z);
    vec3 epicBaseColor = hairAbsorptionToColor(physicalSigma);

    bsdf.baseColor = epicBaseColor;

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

    vec3 V = normalize(-g_pos);
    vec3 T = normalize(g_dir);
    if (material.useGlints > 0.5)
        T = computeMicroTangent(T, V, g_modelPos);

    // T = computeMicroTangent(T, N_orig, g_modelPos);

    // vec3  T         = computeGlintTangent();

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
            float inBacklit = saturate(dot(-L, V));

            HairTransmittanceMask transMask;
            if (material.advShadows > 0.0)
            {
                transMask.visibility = computeHairShadowCone(
                    g_modelPos, normalize((camera.invView * vec4(scene.lights[i].position, 1.0)).xyz - g_modelPos), physicalSigma, material.shadowKnob );
            }
            float derivedHairCount = -log(max(transMask.visibility, 0.001));
            // Aplicas un factor para convertir "Densidad Óptica" a "Número de Capas" aproximado
            // Epic suele considerar que 1 unidad de HairCount es una capa de pelo visible.
            // Ajusta este 1.0/0.7 según tu densidad de voxelización original.
            // float densityScale  = 1.0 / 0.8;
            transMask.hairCount = derivedHairCount * material.scatterKnob * 1000.0;

            bsdf          = evalHairMultipleScattering(V, L, T, transMask, hairLUT, bsdf);
            vec3 lighting = evalEpicHairBSDF(L,
                                             V,
                                             T,
                                             directFraction,
                                             NpTex,
                                             bsdf,
                                             inBacklit,
                                             scene.lights[i].area,
                                             material.r > 0.5,
                                             material.tt > 0.5,
                                             material.trt > 0.5,
                                             material.scatter > 0.5) *
                            scene.lights[i].color * scene.lights[i].intensity * HAIR_GLOBAL_SCALE;

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
    // fragColor = vec4(g_uv.x, 0.0,0.0, 1.0);
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