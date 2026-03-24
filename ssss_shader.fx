precision highp float;

#define MAX_SAMPLES 128
#define MAX_DISTANCE 5.0
#define PI 3.1415926535897932384626433832795
#define EPSILON 1e-6

// Texture from previous frame
uniform sampler2D texturePrevious;

// Textures from the G-Buffer
uniform sampler2D textureAlbedo;
uniform sampler2D texturePosition;
uniform sampler2D textureDiffuseLight;
uniform sampler2D textureMask;

// Textures for subsurface scattering computation
uniform sampler2D textureScatteringDistance;
uniform sampler2D textureDiffuseBackLight;
uniform sampler2D textureAOThickness;
uniform sampler2D textureDepth;

uniform int sampleCount;
uniform float maxScatter;
uniform float frameCounter;
uniform float testScene;
uniform vec2 samples[MAX_SAMPLES];
uniform vec2 screenSize;
uniform mat4 projection, invProjection;

// For single scattering
const float extintionCoeff = 3.0;      // The parameter to fit for single scatter
const float Fdr = 0.028;               // Diffuse Fresnel reflectance (≈0.028 for skin)

const float blushScale = 0.0;
const float ambientPower = 0.5;

float smoothThickness(vec2 uv, float thickness) 
{
  float sharpenFactor = 10.0;
  float sum = 0.0;
  float wsum = 0.0;

  for (int x = -1; x <= 1; ++x) {
    for (int y = -1; y <= 1; ++y) {
      vec2 offset = vec2(x, y) / screenSize;
      float sampleT = texture(textureAOThickness, uv + offset).g;
      float weight = exp(-abs(sampleT - thickness) * sharpenFactor);
      sum += sampleT * weight;
      wsum += weight;
    }
  }
  return sum / wsum;
}

float random(vec2 st) 
{
  return fract(sin(dot(st, vec2(12.9898, 78.233))) * 43758.5453123);
}

vec3 Rr(vec3 d, float r) // Burley's Normalized Diffusion Model
{ 
  return (exp(-r / d) + exp(-r / (d * 3.0))) / (8.0 * PI * d * r);
}

vec3 computeSSSS(vec2 uv) 
{
  // Load from textures
  vec3 A = texture(textureAlbedo, uv).rgb;
  vec3 diffuse = A * texture(textureDiffuseLight, uv).rgb;
  vec3 scatteringDistance = texture(textureScatteringDistance, uv).rgb;
  vec2 aoThickness = texture(textureAOThickness, uv).rg;
  float thickness = smoothThickness(uv, aoThickness.g);
  float scatterMask = texture(textureMask, uv).r;

  // Compute AO to integrate it in the diffusion equation
  float ao = pow(aoThickness.r, ambientPower);
  ao = (!bool(testScene) && ao > EPSILON) ? ao : 1.0;

  /*** MULTIPLE SCATTERING CONTRIBUTION ***/
  float camDist = length(texture(texturePosition, uv).xyz) * MAX_DISTANCE;
  vec3 d = clamp(mix(scatteringDistance / camDist, scatteringDistance, testScene), EPSILON, 1.0) * maxScatter;
  float maxRadius = max(d.r, max(d.g, d.b));

  // View fragment position computation
  float z = texture(textureDepth, uv).r * 2.0 - 1.0;
  vec2 fragCoords = uv * 2.0 - vec2(1.0);
  vec4 viewSpacePos = invProjection * vec4(fragCoords, -z, 1.0);
  vec3 fragViewPos = viewSpacePos.xyz / viewSpacePos.w;

  float jitter = 2.0 * PI * random(mod(uv * screenSize, float(sampleCount)));

  vec3 sssMultiple = vec3(0.0);
  vec3 weight = vec3(0.0);

  for (int i = 0; i < sampleCount; i++) 
  {
    float theta = samples[i].x + jitter;
    float r = samples[i].y * maxRadius;

    vec2 sampleOffset = vec2(cos(theta), sin(theta)) * r;
    vec4 sampleProjected = projection * vec4(fragViewPos + vec3(sampleOffset, 0.0), 1.0);
    vec2 sampleCoords = sampleProjected.xy / sampleProjected.w;
    vec2 sampleUV = (sampleCoords + 1.0) * 0.5;

    float sampleZ = texture(textureDepth, sampleUV).r * 2.0 - 1.0;
    viewSpacePos = invProjection * vec4(sampleCoords, -sampleZ, 1.0);
    vec3 sampleViewPos = viewSpacePos.xyz / viewSpacePos.w;

    float radialDistance = max(distance(sampleViewPos, fragViewPos), EPSILON);

    vec3 rRr = radialDistance * Rr(d, radialDistance);   // Use r * R(r) to integrate over the disk
    vec3 pr = r * Rr(vec3(maxRadius), r);                // PDF
    vec3 diffusion = rRr / pr;
    weight += diffusion;

    vec3 diffuseLight = texture(textureDiffuseLight, sampleUV).rgb * ao;
    sssMultiple += diffusion * diffuseLight;
  }

  sssMultiple = A * (sssMultiple / max(weight, vec3(EPSILON)));
  /******/

  /*** SINGLE SCATTERING CONTRIBUTION ***/
  vec3 backLight = texture(textureDiffuseBackLight, uv).rgb;
  float transmittance = exp(-extintionCoeff * thickness); // Beer-Lambert law
  float falloff = transmittance / (thickness * thickness + EPSILON);
  vec3 sssSingle = Fdr * pow(scatteringDistance, vec3(2.2)) * falloff * backLight;
  /******/

  // Blush (Hacky method)
  float maxCoeff = max(max(diffuse.r, diffuse.g), diffuse.b);
  vec3 maxChannel = step(maxCoeff, diffuse);
  vec3 blush = mix(maxCoeff * maxChannel * (1.0 - thickness) * blushScale, vec3(0.0), testScene);

  return mix(diffuse, sssMultiple + sssSingle + blush, scatterMask);
}

void main(void) 
{
  vec2 uv = gl_FragCoord.xy / screenSize;
  vec3 currentPixel = computeSSSS(uv);
  vec3 previousPixel = texture(texturePrevious, uv).rgb;

  gl_FragColor = vec4((previousPixel * frameCounter + currentPixel) / vec3(frameCounter + 1.0), 1.0);
}
