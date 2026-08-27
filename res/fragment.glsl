#version 410 core

in vec3 vFaceNormal;
in vec2 vUV;
in float vAO;
in float vDist;
flat in int vTexLayer;

uniform sampler2DArray uBlockTextures;

// World lighting uniform block
layout (std140) uniform WorldLighting {
    vec3  sunDir;           // offset 0
    float sunIntensity;     // offset 16 (next 16-byte boundary)

    vec3  sunColor;         // offset 32 (16 + 16)
    float globalExposure;   // offset 48 (forced next 16-byte after vec3)

    vec3  ambientColor;     // offset 64
    float ambientStrength;  // offset 80 (next 16-byte boundary)

    vec3  fogColor;         // offset 96
    float fogDensity;       // offset 112 (next 16-byte boundary)

    // Total size: 128 bytes
} lighting;

out vec4 FragColor;

void main()
{
    vec4 texColor = texture(uBlockTextures, vec3(vUV, vTexLayer));
    if (texColor.a < 0.1) discard;

    vec3 normal = normalize(vFaceNormal);
    vec3 lightDir = normalize(lighting.sunDir);

    float diffuse = max(dot(normal, lightDir), 0.0);
    vec3 lightingResult = (lighting.sunColor * diffuse * lighting.sunIntensity) + (lighting.ambientColor * lighting.ambientStrength);
    lightingResult *= vAO;

    // Apply lighting to the texture first
    vec3 shadedColor = texColor.rgb * lightingResult * lighting.globalExposure;

    // Apply fog overlay as a final step
    float fogFactor = 1.0 - exp(-vDist * lighting.fogDensity);
    fogFactor = clamp(fogFactor, 0.0, 1.0);
    vec3 finalColor = mix(shadedColor, lighting.fogColor, fogFactor);

    FragColor = vec4(finalColor, texColor.a);
    //FragColor = texColor; // For debugging
}
