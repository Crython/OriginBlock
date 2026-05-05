#version 450 core
in vec2 TexCoords;
out vec4 FragColor;

uniform sampler2D uText;
uniform vec3 uTextColor;

void main() {    
    float a = texture(uText, TexCoords).r;
    if (a < 0.01) discard;
    FragColor = vec4(uTextColor, a);
}
