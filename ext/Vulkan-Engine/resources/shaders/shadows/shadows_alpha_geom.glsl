#shader vertex
#version 460

layout(location = 0) in vec3 pos;
layout(location = 2) in vec2 uv;

layout(location = 0) out vec2 v_uv;

void main() {
    gl_Position = vec4(pos, 1.0);
    v_uv = vec2(uv.x, 1.0 - uv.y); // Match UV flip in forward hair_card shader
}

// ─────────────────────────────────────────────────────────────────────────────

#shader geometry
#version 460
#include light.glsl
#include scene.glsl
#include object.glsl

layout(triangles) in;
layout(triangle_strip, max_vertices = 150) out;

layout(location = 0) in  vec2 v_uv[];
layout(location = 0) out vec2 g_uv;

void main() {
    for (int i = 0; i < MAX_LIGHTS; i++) {
        if (i >= scene.numLights) break;

        gl_Layer = i;

        gl_Position = scene.lights[i].viewProj * object.model * gl_in[0].gl_Position;
        g_uv = v_uv[0];
        EmitVertex();

        gl_Position = scene.lights[i].viewProj * object.model * gl_in[1].gl_Position;
        g_uv = v_uv[1];
        EmitVertex();

        gl_Position = scene.lights[i].viewProj * object.model * gl_in[2].gl_Position;
        g_uv = v_uv[2];
        EmitVertex();

        EndPrimitive();
    }
}

// ─────────────────────────────────────────────────────────────────────────────

#shader fragment
#version 460

layout(location = 0) in vec2 g_uv;

// Only the first slot is needed (hairColor + alphaThreshold)
layout(set = 1, binding = 1) uniform MaterialUniforms {
    vec3  hairColor;
    float alphaThreshold;
} material;

layout(set = 2, binding = 0) uniform sampler2D hairDataTex; // R = alpha mask

void main() {
    float alpha = texture(hairDataTex, g_uv).r;
    if (alpha < material.alphaThreshold) discard;
    // No color output — depth attachment only; fragment runs for depth write.
}
