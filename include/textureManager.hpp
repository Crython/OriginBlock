#ifndef TEXTURE_MANAGER_HPP
#define TEXTURE_MANAGER_HPP
#include "texture.hpp"

class TextureManager {
public:
    void loadBlockTextures();
    void bindBlockTextures() const;

    uint32_t blockFaceTexture(uint8_t blockType, uint8_t face) const;

private:
    GLuint blockTextureArray = 0;
};


#endif // TEXTURE_MANAGER_HPP