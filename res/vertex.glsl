#version 410 core

layout (location = 0) in uint aPacked1;
layout (location = 1) in uint aPacked2;

uniform mat4 uViewProj;
uniform mat4 uModel;

out vec3 vFaceNormal;
out vec2 vUV;
out float vAO;
flat out int vTexLayer;

vec3 decodeNormal(uint normalIndex) {
    if (normalIndex == 0u) return vec3(1.0, 0.0, 0.0);
    if (normalIndex == 1u) return vec3(-1.0, 0.0, 0.0);
    if (normalIndex == 2u) return vec3(0.0, 1.0, 0.0);
    if (normalIndex == 3u) return vec3(0.0, -1.0, 0.0);
    if (normalIndex == 4u) return vec3(0.0, 0.0, 1.0);
    if (normalIndex == 5u) return vec3(0.0, 0.0, -1.0);
    return vec3(0.0, 0.0, 1.0);
}

out float vDist;

void main()
{
    // Extract data from aPacked1: X(5), Y(5), Z(5), U(5), V(5), Normal(3), AO_low(4)
    uint x = aPacked1 & 0x1Fu;
    uint y = (aPacked1 >> 5) & 0x1Fu;
    uint z = (aPacked1 >> 10) & 0x1Fu;
    uint u = (aPacked1 >> 15) & 0x1Fu;
    uint v = (aPacked1 >> 20) & 0x1Fu;
    uint normalIndex = (aPacked1 >> 25) & 0x7u;
    uint aoLow = (aPacked1 >> 28) & 0xFu;

    // Extract data from aPacked2: texLayer(15), AO_high(1)
    uint texLayer = aPacked2 & 0x7FFFu;
    uint aoHigh = (aPacked2 >> 15) & 0x1u;
    uint ao = aoLow | (aoHigh << 4);

    vFaceNormal = decodeNormal(normalIndex);
    vUV = vec2(float(u), float(v));
    vAO = float(ao) / 31.0; 
    vTexLayer = int(texLayer);

    vec3 pos = vec3(float(x), float(y), float(z));
    vec4 worldPos = uModel * vec4(pos, 1.0);
    gl_Position = uViewProj * worldPos;

    vDist = gl_Position.w;
}
