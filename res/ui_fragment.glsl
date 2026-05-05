#version 450 core
in vec2 TexCoords;
out vec4 FragColor;

void main() {
    float dist = distance(TexCoords, vec2(0.5));
    
    // Solid circle with slight antialiasing
    float alpha = smoothstep(0.5, 0.48, dist);
    
    if (alpha <= 0.0) discard;
    
    FragColor = vec4(1.0, 1.0, 1.0, alpha); 
}
