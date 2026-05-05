#pragma once
#include <string>

#include <glad/glad.h>

class Texture {
public:
    Texture(const std::string& path);
    ~Texture();

    void bind(uint32_t slot = 0) const;

    int width() const { return m_width; }
    int height() const { return m_height; }

private:
    GLuint m_id = 0;
    int m_width = 0;
    int m_height = 0;
};

