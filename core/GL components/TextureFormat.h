//
// Created by micha on 10/11/2026.
//

#ifndef RAWENGINE_TEXTUREFORMAT_H
#define RAWENGINE_TEXTUREFORMAT_H
#include <glad/glad.h>
#include "imgui_impl_opengl3_loader.h"

namespace core::gl {
    struct TextureFormat {
        GLenum internalFormat; // e.g. GL_R8, also used for glBindImageTexture
        GLenum format;         // e.g. GL_RED
        GLenum type;           // e.g. GL_UNSIGNED_BYTE
    };

    namespace formats {
        constexpr TextureFormat R8      {GL_R8,      GL_RED,         GL_UNSIGNED_BYTE};
        constexpr TextureFormat R16F    {GL_R16F,    GL_RED,         GL_HALF_FLOAT};
        constexpr TextureFormat R16UI   {GL_R16UI,   GL_RED_INTEGER, GL_UNSIGNED_SHORT};
        constexpr TextureFormat RGBA16F {GL_RGBA16F, GL_RGBA,        GL_HALF_FLOAT};
        constexpr TextureFormat RGBA32F {GL_RGBA32F, GL_RGBA,        GL_FLOAT};
    }
}

#endif //RAWENGINE_TEXTUREFORMAT_H