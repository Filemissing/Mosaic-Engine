//
// Created by micha on 10/11/2026.
//

#include "./Texture3D.h"

namespace core::gl {
    Texture3D::Texture3D(Texture3D&& other) noexcept
        : id(other.id), size(other.size), format(other.format) {
        other.id = 0; // other no longer owns it, so its destructor won't delete it
    }
    Texture3D& Texture3D::operator=(Texture3D&& other) noexcept {
        if (this != &other) {
            Release();               // delete the existing texture
            id = other.id;
            size = other.size;
            format = other.format;
            other.id = 0;
        }
        return *this;
    }

    Texture3D::Texture3D(glm::ivec3 size, TextureFormat tf, GLenum filter, const void* data, GLenum wrap) {
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_3D, id);
        glTexImage3D(GL_TEXTURE_3D, 0, tf.internalFormat, size.x, size.y, size.z, 0, tf.format, tf.type, data);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, filter);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, wrap);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, wrap);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, wrap);

        this->size = size;
        this->format = tf;
    }

    Texture3D::~Texture3D() { Release(); }
    void Texture3D::Release() {
        if (id) glDeleteTextures(1, &id);
        id = 0;
    }

    void Texture3D::BindImage(GLuint unit, GLenum access) const {
        glBindImageTexture(unit, id, 0, GL_TRUE, 0, access, format.internalFormat);
    }
} // core