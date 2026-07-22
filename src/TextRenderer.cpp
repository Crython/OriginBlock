#include "pch.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "TextRenderer.hpp"
#include <glm/glm/gtc/matrix_transform.hpp>


TextRenderer::TextRenderer(Shader& shader) : shader(shader), screenWidth(1920), screenHeight(1080) {
    initRenderData();
}

TextRenderer::~TextRenderer() {
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    for (auto const& [key, val] : characters) {
        glDeleteTextures(1, &val.textureID);
    }
}

bool TextRenderer::loadFont(const std::string& fontPath, int fontSize) {
    // Clear previous font data
    for (auto const& [key, val] : characters) {
        glDeleteTextures(1, &val.textureID);
    }
    characters.clear();

    // Load font file into buffer
    std::ifstream file(fontPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "ERROR::TEXTRENDERER: Failed to load font file: " << fontPath << std::endl;
        return false;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<unsigned char> fontBuffer(size);
    if (!file.read((char*)fontBuffer.data(), size)) {
        return false;
    }

    stbtt_fontinfo fontInfo;
    if (!stbtt_InitFont(&fontInfo, fontBuffer.data(), 0)) {
        std::cerr << "ERROR::TEXTRENDERER: Failed to initialize stb_truetype font info" << std::endl;
        return false;
    }

    float scale = stbtt_ScaleForPixelHeight(&fontInfo, (float)fontSize);

    // Disable byte-alignment restriction
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // Load first 128 characters of ASCII set
    for (unsigned char c = 0; c < 128; c++) {
        int w, h, xoff, yoff;
        unsigned char* bitmap = stbtt_GetCodepointBitmap(&fontInfo, 0, scale, c, &w, &h, &xoff, &yoff);

        if (!bitmap) {
            // Some characters might not have a bitmap (like space)
            int advance, lsb;
            stbtt_GetCodepointHMetrics(&fontInfo, c, &advance, &lsb);
            Character character = {
                0,
                glm::ivec2(0, 0),
                glm::ivec2(0, 0),
                (unsigned int)(advance * scale)
            };
            characters.insert(std::pair<char, Character>(c, character));
            continue;
        }

        // Generate texture
        GLuint texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, bitmap);

        // Set texture options
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        // Get metrics
        int advance, lsb;
        stbtt_GetCodepointHMetrics(&fontInfo, c, &advance, &lsb);

        // Store character
        Character character = {
            texture,
            glm::ivec2(w, h),
            glm::ivec2(xoff, yoff),
            (unsigned int)(advance * scale)
        };
        characters.insert(std::pair<char, Character>(c, character));

        stbtt_FreeBitmap(bitmap, nullptr);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void TextRenderer::initRenderData() {
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void TextRenderer::setScreenSize(int width, int height) {
    screenWidth = width;
    screenHeight = height;
}

void TextRenderer::renderText(std::string text, float x, float y, float scale, glm::vec3 color, bool useEscapeSequences ) {
    shader.bind();
    shader.setVec3("uTextColor", color);
    shader.setInt("uText", 0);
    
    // Orthographic projection: (0,0) is top-left, (screenW, screenH) is bottom-right
    glm::mat4 projection = glm::ortho(0.0f, (float)screenWidth, (float)screenHeight, 0.0f);
    shader.setMat4("uProjection", projection);

    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(vao);
    glDisable(GL_CULL_FACE);

    float currentX = x;
    // We treat 'y' as the baseline for the text
    float baselineY = y;

    float maxH = 0.0f;
    float lineHeight = 30.0f * scale;
    if (!characters.empty()) {
        for (const auto& pair : characters) {
            if (pair.second.size.y > maxH) maxH = pair.second.size.y;
        }
        lineHeight = maxH * 1.2f * scale;
    }

    // Pre-calculate number of lines to adjust for screen overflow
    int numLines = 1;
    bool escCount = false;
    for (char c : text) {
        if (useEscapeSequences && !escCount && c == '\\') {
            escCount = true;
            continue;
        }
        if (useEscapeSequences && escCount) {
            escCount = false;
            if (c == 'n') numLines++;
        } else if (c == '\n') {
            numLines++;
        }
    }

    float expectedBottom = baselineY + (numLines - 1) * lineHeight + maxH * scale;
    if (expectedBottom > screenHeight) {
        baselineY -= (expectedBottom - screenHeight);
    }

    std::vector<float> xPosHistory;
    xPosHistory.push_back(currentX);

    bool escaped = false;

    for (char c : text) {
        if (useEscapeSequences && !escaped && c == '\\') {
            escaped = true;
            continue;
        }

        if (useEscapeSequences && escaped) {
            escaped = false;
			if (c == 'n') c = '\n';       // newline
			else if (c == 'r') c = '\r';  // carriage return
			else if (c == 't') c = '\t';  // tab
			else if (c == 'b') c = '\b';  // backspace
			else if (c == '\\') c = '\\'; // write a literal backslash
			else currentX -= 0;           // No change to currentX for unrecognized escape sequences
        }

        if (c == '\n') {
            baselineY += lineHeight;
            float spaceAdvance = characters.count(' ') ? characters[' '].advance * scale : 20.0f * scale;
            currentX = x + spaceAdvance * 2.0f;
            xPosHistory.push_back(currentX);
            continue;
        }
        if (c == '\r') {
            currentX = x;
            xPosHistory.push_back(currentX);
            continue;
        }
        if (c == '\t') {
            float spaceAdvance = characters.count(' ') ? characters[' '].advance * scale : 20.0f * scale;
            currentX += spaceAdvance * 4.0f;
            xPosHistory.push_back(currentX);
            continue;
        }
        if (c == '\b') {
            if (xPosHistory.size() > 1) {
                xPosHistory.pop_back();
                currentX = xPosHistory.back();
            }
            continue;
        }

        if (characters.find(c) == characters.end()) {
            continue;
        }

        Character ch = characters[c];

        if (ch.textureID == 0) { // Skip characters with no bitmap (like space)
            currentX += ch.advance * scale;
            xPosHistory.push_back(currentX);
            continue;
        }

        float xpos = currentX + ch.bearing.x * scale;
        float ypos = baselineY + ch.bearing.y * scale;

        float w = ch.size.x * scale;
        float h = ch.size.y * scale;

        // Quad vertices for standard top-left UVs
        float vertices[6][4] = {
            { xpos,     ypos + h,   0.0f, 1.0f },
            { xpos,     ypos,       0.0f, 0.0f },
            { xpos + w, ypos,       1.0f, 0.0f },

            { xpos,     ypos + h,   0.0f, 1.0f },
            { xpos + w, ypos,       1.0f, 0.0f },
            { xpos + w, ypos + h,   1.0f, 1.0f }
        };

        glBindTexture(GL_TEXTURE_2D, ch.textureID);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices); 
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        
        glDrawArrays(GL_TRIANGLES, 0, 6);
        
        currentX += ch.advance * scale;
        xPosHistory.push_back(currentX);
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glEnable(GL_CULL_FACE);
}

float TextRenderer::getTextWidth(const std::string& text, float scale) {
    float width = 0.0f;
    for (char c : text) {
        auto it = characters.find(c);
        if (it != characters.end()) {
            width += it->second.advance * scale;
        }
    }
    return width;
}
