#shader vertex
#version 460 core
#include object.glsl

//Input
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 uv;
layout(location = 3) in vec3 tangent;
layout(location = 4) in vec3 color;

//Output
layout(location = 0) out vec3 v_color;
layout(location = 1) out vec3 v_tangent;

void main() {

    gl_Position = object.model * vec4(position, 1.0);

    v_tangent = normalize(mat3(transpose(inverse(object.model))) * tangent);
    v_color = color;

}

#shader geometry
#version 460 core
#include camera.glsl

//Setup
layout(lines) in;
layout(triangle_strip, max_vertices = 4) out;

//Input
layout(location = 0) in vec3 v_color[];
layout(location = 1) in vec3 v_tangent[];

//Uniforms
layout(set = 1, binding = 1) uniform MaterialUniforms {
    vec3 baseColor;
    float thickness;
} material;

//Output
layout(location = 0) out vec3 g_pos;
layout(location = 1) out vec3 g_modelPos;
layout(location = 2) out vec3 g_normal;
layout(location = 3) out vec3 g_modelNormal;
layout(location = 4) out vec2 g_uv;
layout(location = 5) out vec3 g_dir;
layout(location = 6) out vec3 g_modelDir;
layout(location = 7) out vec3 g_color;
layout(location = 8) out vec3 g_origin;


void emitQuadPoint(
    vec4 origin,
    vec4 right,
    float offset,
    vec3 forward,
    vec3 normal,
    vec2 uv,
    int id
) {

    vec4 newPos = origin + right * offset; //Model space
    gl_Position = camera.viewProj * newPos;
    g_dir = normalize(mat3(transpose(inverse(camera.view))) * v_tangent[id]);
    g_modelDir = v_tangent[id];
    g_color = v_color[id];
    g_pos = (camera.view * newPos).xyz;
    g_modelPos = newPos.xyz;
    g_uv = uv;
    g_normal = normalize(mat3(transpose(inverse(camera.view))) * normal);
    g_modelNormal = normal;
    g_origin = (camera.view * origin).xyz;

    EmitVertex();
}

void main() {

        //Model space --->>>

    vec4 startPoint = gl_in[0].gl_Position;
    vec4 endPoint = gl_in[1].gl_Position;

    vec4 view0 = vec4(camera.position.xyz, 1.0) - startPoint;
    vec4 view1 = vec4(camera.position.xyz, 1.0) - endPoint;

    vec3 dir0 = v_tangent[0];
    vec3 dir1 = v_tangent[1];

    vec4 right0 = normalize(vec4(cross(dir0.xyz, view0.xyz), 0.0));
    vec4 right1 = normalize(vec4(cross(dir1.xyz, view1.xyz), 0.0));

    vec3 normal0 = normalize(cross(right0.xyz, dir0.xyz));
    vec3 normal1 = normalize(cross(right1.xyz, dir1.xyz));

        //<<<----

    float halfLength = material.thickness * 0.5;

    emitQuadPoint(startPoint, right0, halfLength, dir0, normal0, vec2(1.0, 0.0), 0);
    emitQuadPoint(endPoint, right1, halfLength, dir1, normal1, vec2(1.0, 1.0), 1);
    emitQuadPoint(startPoint, -right0, halfLength, dir0, normal0, vec2(0.0, 0.0), 0);
    emitQuadPoint(endPoint, -right1, halfLength, dir1, normal1, vec2(0.0, 1.0), 1);

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
#include BRDFs/hair_BSDF.glsl

//Input
layout(location = 0) in vec3 g_pos;
layout(location = 1) in vec3 g_modelPos;
layout(location = 2) in vec3 g_normal;
layout(location = 3) in vec3 g_modelNormal;
layout(location = 4) in vec2 g_uv;
layout(location = 5) in vec3 g_dir;
layout(location = 6) in vec3 g_modelDir;
layout(location = 7) in vec3 g_color;
layout(location = 8) in vec3 g_origin;

//Uniforms
layout(set = 0, binding = 2) uniform sampler2DArray shadowMap;
layout(set = 0, binding = 4) uniform samplerCube irradianceMap;


layout(set = 1, binding = 1) uniform MaterialUniforms {
    vec3 sigma_a;
    float thickness;

    float beta;
    float shift;
    float ior;
    float density;

    float Rpower;
    float TTpower;
    float TRTpower;
    float scatter ;

    float azBeta;
    bool r;
    bool tt;
    bool trt;

} material;

layout(set = 2, binding = 0) uniform sampler3D DpTex;
layout(set = 2, binding = 1) uniform sampler2D attTexFront;
layout(set = 2, binding = 2) uniform sampler2D attTexBack;
layout(set = 2, binding = 3) uniform sampler2D Ng;

HairBSDF bsdf;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 outBrightColor;

vec3 computeAmbient(vec3 n) {

    vec3 ambient;
    if(scene.useIBL){
        float rad = radians(scene.envRotation);
        float c = cos(rad);
        float s = sin(rad);
        mat3 rotationY = mat3(c, 0.0, -s,
                0.0, 1.0, 0.0,
                s, 0.0, c);
        vec3 rotatedNormal = normalize(rotationY * n);

        // ambient = evalMarschnerLookupBSDF(
        //         rotatedNormal, 
        //         normalize(-g_pos),
        //         texture(irradianceMap, rotatedNormal).rgb*scene.ambientIntensity,
        //         bsdf, 
        //         mTex,
        //         nTex1,
        //         nTex2,
        //         GITex,
        //         mGITex,
        //         nGITex1,
        //         nGITex2,
        //         vec3(0.5),
        //         vec3(0.5),
        //         0.1,
        //         material.r, 
        //         false, 
        //         material.trt,
        //         material.useScatter);


        // ambient = (scene.ambientIntensity * scene.ambientColor) ;
    }else{
        ambient = (scene.ambientIntensity * scene.ambientColor) ;
    }
    return ambient;
}

//////////////////////////////////////////////////////////////////////////
// Special shadow mapping for hair for controlling density
//////////////////////////////////////////////////////////////////////////
float bilinear(float v[4], vec2 f) {
    return mix(mix(v[0], v[1], f.x), mix(v[2], v[3], f.x), f.y);
}

vec3 bilinear(vec3 v[4], vec2 f) {
    return mix(mix(v[0], v[1], f.x), mix(v[2], v[3], f.x), f.y);
}
vec3 hairShadow(out vec3 spread, out float directF, vec3 pShad, sampler2DArray shadowMap, int lightId, float density) {
    ivec2 size = textureSize(shadowMap, 0).xy;
    vec2 t = pShad.xy * vec2(size) + 0.5;
    vec2 f = t - floor(t);
    vec2 s = 0.5 / vec2(size);

    vec2 tcp[4];
    tcp[0] = pShad.xy + vec2(-s.x, -s.y);
    tcp[1] = pShad.xy + vec2(s.x, -s.y);
    tcp[2] = pShad.xy + vec2(-s.x, s.y);
    tcp[3] = pShad.xy + vec2(s.x, s.y);

    const float coverage = 0.05;
    const vec3 a_f = vec3(0.507475266, 0.465571405, 0.394347166);
    const vec3 w_f = vec3(0.028135575, 0.027669785, 0.027669785);
    float dir[4];
    vec3 spr[4], t_d[4];
    for(int i = 0; i < 4; ++i) {
        float z = texture(shadowMap, vec3(tcp[i], lightId)).r;
        float h = max(0.0, pShad.z - z);
        float n = h * density * 0.7;
        dir[i] = pow(1.0 - coverage, n);
        t_d[i] = pow(1.0 - coverage * (1.0 - a_f), vec3(n, n, n));
        spr[i] = n * coverage * w_f;
    }

    directF = bilinear(dir, f);
    spread = bilinear(spr, f);
    return bilinear(t_d, f);
}

vec3 computeHairShadow(LightUniform light, int lightId, sampler2DArray shadowMap, float density, vec3 pos, out vec3 spread, out float directF) {
    vec4 posLightSpace = light.viewProj * vec4(pos, 1.0);
    vec3 projCoords = posLightSpace.xyz / posLightSpace.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    vec3 transDirect = hairShadow(spread, directF, projCoords, shadowMap, lightId, density);
    directF *= 0.5;
    return transDirect * 0.5;
}

void main() {

    //BSDF setup ............................................................
    bsdf.tangent =  normalize(g_dir);

    bsdf.beta = material.beta;
    bsdf.azBeta = material.azBeta;
    bsdf.shift = material.shift;
    bsdf.sigma_a = material.sigma_a;
    bsdf.ior =  material.ior;
    bsdf.density = material.density;

    bsdf.Rpower = material.Rpower;
    bsdf.TTpower = material.TTpower;
    bsdf.TRTpower = material.TRTpower;


    //DIRECT LIGHTING .......................................................
    vec3 color = vec3(0.0);
    for(int i = 0; i < scene.numLights; i++) {
        //If inside liught area influence
        if(isInAreaOfInfluence(scene.lights[i], g_pos)) {

            vec3    shadow = vec3(1.0);
            vec3    spread = vec3(0.0);
            float   directFraction = 1.0;
            if(int(object.otherParams.y) == 1 && scene.lights[i].shadowCast == 1) {
                if(scene.lights[i].shadowType == 0) //Classic
                    shadow = computeHairShadow(scene.lights[i], i,shadowMap, bsdf.density, g_modelPos,spread, directFraction);
                if(scene.lights[i].shadowType == 1) //VSM   
                    shadow = computeHairShadow(scene.lights[i], i,shadowMap, bsdf.density, g_modelPos,spread, directFraction);
            }
            vec3 lighting = evalHairBSDF(
                normalize(scene.lights[i].position.xyz - g_pos), 
                normalize(-g_pos),
                scene.lights[i].color * scene.lights[i].intensity,
                bsdf, 
                shadow,
                spread,
                directFraction,
                //TBD
                DpTex,
                attTexFront,
                attTexFront,
                //
                material.r, 
                material.tt, 
                material.trt);

            color += lighting;
        }
    }


    // vec3 n1 = cross(g_modelDir, cross(camera.position.xyz, g_modelDir));
    vec3 fakeNormal = normalize(g_modelPos-object.volumeCenter);
    // vec3 fakeNormal = mix(n1,n2,0.5);

    //AMBIENT COMPONENT ..........................................................

    vec3 ambient = computeAmbient(fakeNormal);
    color += ambient;

    if(int(object.otherParams.x) == 1 && scene.enableFog) {
        float f = computeFog(gl_FragCoord.z);
        color = f * color + (1 - f) * scene.fogColor.rgb;
    }


    fragColor = vec4(color, 1.0);
     // check whether result is higher than some threshold, if so, output as bloom threshold color
    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
    if(brightness > 1.0)
        outBrightColor = vec4(color, 1.0);
    else
        outBrightColor = vec4(0.0, 0.0, 0.0, 1.0);
    

}