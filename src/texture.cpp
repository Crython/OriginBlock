#include "pch.h"
#include "texture.hpp"
#include <stb/stb_image.h>


Texture::Texture(const std::string& path) {
    int channels;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path.c_str(), &m_width, &m_height, &channels, 4);
    if (!data)
    {
        std::cout << "CWD: " << std::filesystem::current_path() << "\n";
		std::cerr << "Failed to load texture: " << path << std::endl;
		std::cerr << "stb_image error: " << stbi_failure_reason() << std::endl;
		std::abort();
        throw std::runtime_error("Failed to load texture: " + path);
    }

    glGenTextures(1, &m_id);
    glBindTexture(GL_TEXTURE_2D, m_id);

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glTexImage2D(
        GL_TEXTURE_2D, 0, GL_RGBA8,
        m_width, m_height, 0,
        GL_RGBA, GL_UNSIGNED_BYTE, data
    );

    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);
}

Texture::~Texture() {
    if (m_id)
        glDeleteTextures(1, &m_id);
}

void Texture::bind(uint32_t slot) const {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, m_id);
}
