//
// Created by micha on 10/11/2026.
//

#include "Texture2D.h"

namespace core::gl {
    Texture2D::Texture2D(Texture2D&& other) noexcept
        : id(other.id), size(other.size), format(other.format) {
        other.id = 0; // other no longer owns it, so its destructor won't delete it
    }
    Texture2D& Texture2D::operator=(Texture2D&& other) noexcept {
        if (this != &other) {
            Release();               // delete the existing texture
            id = other.id;
            size = other.size;
            format = other.format;
            other.id = 0;
        }
        return *this;
    }

    Texture2D::Texture2D(glm::ivec2 size, TextureFormat tf, GLenum filter, const void* data) {
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexImage2D(GL_TEXTURE_2D, 0, tf.internalFormat, size.x, size.y, 0, tf.format, tf.type, data);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        this->size = size;
        this->format = tf;
    }

    Texture2D::~Texture2D() { Release(); }
    void Texture2D::Release() {
        if (id) glDeleteTextures(1, &id);
        id = 0;
    }

    void Texture2D::BindImage(GLuint unit, GLenum access) const {
        glBindImageTexture(unit, id, 0, GL_FALSE, 0, access, format.internalFormat);
    }
} // core