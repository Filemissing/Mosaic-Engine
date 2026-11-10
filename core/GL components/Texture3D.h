// core/GL/Texture3D.h
#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>

#include "TextureFormat.h"

namespace core::gl {
    class Texture3D {
    public:
        Texture3D() = default; // empty, Id() == 0
        Texture3D(glm::ivec3 size, TextureFormat tf, GLenum filter, const void* data = nullptr);
        ~Texture3D();

        Texture3D(const Texture3D&) = delete;
        Texture3D& operator=(const Texture3D&) = delete;
        Texture3D(Texture3D&& other) noexcept;
        Texture3D& operator=(Texture3D&& other) noexcept;

        void BindImage(GLuint unit, GLenum access) const;

        GLuint Id() const { return id; }
        glm::ivec3 Size() const { return size; }

    private:
        void Release();

        GLuint id = 0;
        glm::ivec3 size{0};
        TextureFormat format{};
    };
}
