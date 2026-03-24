precision highp float;

#define MAX_SAMPLES 64
#define EPSILON 1e-5

uniform sampler2D texturePosition;
uniform sampler2D textureNormal;
uniform sampler2D textureNoise;

uniform float kernelSize;
uniform vec3 samples[MAX_SAMPLES];

uniform vec2 screenSize;
uniform mat4 projection;

const float radius = 0.03;
const float bias = 0.003;

float occlusion(vec3 normal)
{
    float factor = 0.0;

    vec2 uv = gl_FragCoord.xy / screenSize;
    vec3 fragPos = texture(texturePosition, uv).xyz;
    vec2 noiseScale = screenSize / vec2(textureSize(textureNoise, 0));
    vec3 randomVec = texture(textureNoise, uv * noiseScale).xyz;
    randomVec = randomVec * 2.0 - vec3(1.0); 

    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);
            
    for(int i = 0; i < int(kernelSize); i++)
    {
        vec3 samplePos = TBN * samples[i];
        samplePos = fragPos + samplePos * radius; 
        vec4 offset = projection * vec4(samplePos, 1.0);
        offset.xyz /= offset.w;
        offset.xyz = offset.xyz * 0.5 + 0.5;
        float sampleDepth = texture(texturePosition, offset.xy).z;
        float rangeCheck = smoothstep(0.0, 1.0, radius / abs(fragPos.z - sampleDepth));
        factor += (sampleDepth <= samplePos.z + bias ? rangeCheck : 0.0);
    }

    return clamp(factor / kernelSize, EPSILON, 1.0);
}

void main()
{
    vec2 uv = gl_FragCoord.xy / screenSize;
    vec3 pos = texture(texturePosition, uv).xyz;
    vec3 normal = texture(textureNormal, uv).rgb;

    float ao = 1.0 - occlusion(normal);
    float thickness = occlusion(-normal);
    
    gl_FragColor.r = ao;
    gl_FragColor.g = thickness;
    gl_FragColor.a = 1.0;
}