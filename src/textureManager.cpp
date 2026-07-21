#include "pch.h"
#include "textureManager.hpp"
#include "stb/stb_image.h"


static const std::vector<std::string> blockTexturePaths = {
    "assets/textures/blocks/debug.png",      // layer 0
    "assets/textures/blocks/side+X.png",     // 1
    "assets/textures/blocks/side-X.png",     // 2
    "assets/textures/blocks/side+Y.png",     // 3
    "assets/textures/blocks/side-Y.png",     // 4
	"assets/textures/blocks/side+Z.png",     // 5
    "assets/textures/blocks/side-Z.png",     // 6
    "assets/textures/blocks/grass_top.png",  // 7
    "assets/textures/blocks/grass_side.png", // 8
    "assets/textures/blocks/dirt.png",       // 9
    "assets/textures/blocks/stone.png",      // 10
    "assets/textures/blocks/sand.png",       // 11
    "assets/textures/blocks/bedrock.png",    // 12
	"assets/textures/blocks/wood.png",		 // 13
    "assets/textures/blocks/wood_top.png",   // 14
	"assets/textures/blocks/leaves.png",     // 15
    "assets/textures/blocks/white.png"      // 
};

void TextureManager::loadBlockTextures()
{
    int width, height, channels = 0;
    std::vector<unsigned char*> images;

    for (auto& path : blockTexturePaths)
    {
        unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (!data)
        {
			std::cerr << "Failed to load texture: " << path << std::endl;
            throw std::runtime_error("Failed to load texture: " + path);
        }
        images.push_back(data);
    }

    float maxAniso = 0.0f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &maxAniso);

    glGenTextures(1, &blockTextureArray);
    glBindTexture(GL_TEXTURE_2D_ARRAY, blockTextureArray);
    glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY, maxAniso);

    glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_LOD_BIAS, 0.75f);


    glTexImage3D(
        GL_TEXTURE_2D_ARRAY,
        0,
        GL_RGBA8,
        width,
        height,
        images.size(),
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        nullptr
    );

    for (int i = 0; i < images.size(); ++i)
    {
        glTexSubImage3D(
            GL_TEXTURE_2D_ARRAY,
            0,
            0, 0, i,
            width, height, 1,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            images[i]
        );
    }

    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);

    for (auto img : images)
        stbi_image_free(img);
}

void TextureManager::bindBlockTextures() const
{
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, blockTextureArray);
}

