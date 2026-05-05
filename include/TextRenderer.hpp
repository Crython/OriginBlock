#ifndef TEXT_RENDERER_HPP
#define TEXT_RENDERER_HPP

#include <string>
#include <vector>
#include <map>
#include <glad/glad.h>
#include <glm/glm/glm.hpp>
#include "shader.hpp"
#include "stb/stb_truetype.h"

struct Character {
    GLuint textureID;   // ID handle of the glyph texture
    glm::ivec2 size;    // Size of glyph
    glm::ivec2 bearing; // Offset from baseline to left/top of glyph
    unsigned int advance;    // Offset to advance to next glyph
};

class TextRenderer {
public:
    TextRenderer(Shader& shader);
    ~TextRenderer();

    // Load font from a TTF file
    bool loadFont(const std::string& fontPath, int fontSize);

    // Render a string at (x, y) with scale and color
    void renderText(std::string text, float x, float y, float scale, glm::vec3 color);

    // Update screen dimensions
    void setScreenSize(int width, int height);

private:
    Shader& shader;
    GLuint vao, vbo;
    int screenWidth, screenHeight;

    // Map to store pre-rendered characters
    std::map<char, Character> characters;

    void initRenderData();
};

#endif
