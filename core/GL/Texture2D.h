//
// Created by micha on 10/11/2026.
//

#ifndef RAWENGINE_TEXTURE2D_H
#define RAWENGINE_TEXTURE2D_H
#include <glm/vec2.hpp>
#include <glad/glad.h>
#include "TextureFormat.h"

namespace core::gl {
    class Texture2D {
    public:
        Texture2D() = default; // empty, Id() == 0
        Texture2D(glm::ivec2 size, TextureFormat tf, GLenum filter, const void* data = nullptr);
        ~Texture2D();

        Texture2D(const Texture2D&) = delete;
        Texture2D& operator=(const Texture2D&) = delete;
        Texture2D(Texture2D&& other) noexcept;
        Texture2D& operator=(Texture2D&& other) noexcept;

        void BindImage(GLuint unit, GLenum access) const;

        GLuint Id() const { return id; }
        glm::ivec2 Size() const { return size; }

    private:
        void Release();

        GLuint id = 0;
        glm::ivec2 size{0};
        TextureFormat format{};
    };
} // core

#endif //RAWENGINE_TEXTURE2D_H