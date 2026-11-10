//
// Created by micha on 10/11/2026.
//

#include "StorageBuffer.h"

namespace core::gl {
    StorageBuffer::StorageBuffer(StorageBuffer&& other) noexcept
        : id(other.id), binding(other.binding), sizeBytes(other.sizeBytes) {
        other.id = 0; // other no longer owns it, so its destructor won't delete it
        other.sizeBytes = 0;
    }
    StorageBuffer& StorageBuffer::operator=(StorageBuffer&& other) noexcept {
        if (this != &other) {
            Release();               // delete the existing buffer
            id = other.id;
            binding = other.binding;
            sizeBytes = other.sizeBytes;
            other.id = 0;
            other.sizeBytes = 0;
        }
        return *this;
    }

    StorageBuffer::~StorageBuffer() { Release(); }
    void StorageBuffer::Release() {
        if (id) glDeleteBuffers(1, &id);
        id = 0;
        sizeBytes = 0;
    }

    void StorageBuffer::EnsureCreated() {
        if (id == 0) glGenBuffers(1, &id);
    }

    void StorageBuffer::Allocate(size_t bytes) {
        EnsureCreated();
        sizeBytes = bytes;
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, id);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeBytes, nullptr, GL_DYNAMIC_DRAW);
    }

    void StorageBuffer::Bind() const {
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, id);
    }
} // core