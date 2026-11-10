//
// Created by micha on 10/11/2026.
//

#ifndef RAWENGINE_STORAGEBUFFER_H
#define RAWENGINE_STORAGEBUFFER_H
#include <vector>
#include <glad/glad.h>

namespace core::gl {
    namespace ssbo {
        constexpr GLuint Primitives       = 0;
        constexpr GLuint FreeList         = 1;
        constexpr GLuint PrimitiveIndices = 2;
        constexpr GLuint ChunkFlags       = 3;
        constexpr GLuint Materials        = 6;
    }

    class StorageBuffer {
    public:
        explicit StorageBuffer(GLuint binding) : binding(binding) {}
        ~StorageBuffer();

        StorageBuffer(const StorageBuffer&) = delete;
        StorageBuffer& operator=(const StorageBuffer&) = delete;
        StorageBuffer(StorageBuffer&& other) noexcept;
        StorageBuffer& operator=(StorageBuffer&& other) noexcept;

        // (re)allocates the buffer to exactly fit data; an empty vector uploads one zeroed element instead because binding an empty SSBO isn't allowed
        template<typename T> void Upload(const std::vector<T>& data);
        // (re)allocates bytes of uninitialized storage
        void Allocate(size_t bytes);
        // overwrites part of the existing storage without reallocating
        template<typename T> void Write(const std::vector<T>& data, size_t offsetBytes = 0);
        // copies count elements back to the CPU
        template<typename T> std::vector<T> Read(size_t count, size_t offsetBytes = 0) const;

        void Bind() const;

        GLuint Id() const { return id; }
        size_t SizeBytes() const { return sizeBytes; }

    private:
        void EnsureCreated();
        void Release();

        GLuint id = 0;
        GLuint binding;
        size_t sizeBytes = 0;
    };

    // template definitions
    template<typename T>
    void StorageBuffer::Upload(const std::vector<T>& data) {
        EnsureCreated();
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, id);
        if (data.empty()) {
            const T zero{};
            sizeBytes = sizeof(T);
            glBufferData(GL_SHADER_STORAGE_BUFFER, sizeBytes, &zero, GL_DYNAMIC_DRAW);
        } else {
            sizeBytes = data.size() * sizeof(T);
            glBufferData(GL_SHADER_STORAGE_BUFFER, sizeBytes, data.data(), GL_DYNAMIC_DRAW);
        }
    }

    template<typename T>
    void StorageBuffer::Write(const std::vector<T>& data, size_t offsetBytes) {
        const size_t bytes = data.size() * sizeof(T);
        if (bytes == 0 || offsetBytes + bytes > sizeBytes) return; // nothing to write, or it doesn't fit
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, id);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, offsetBytes, bytes, data.data());
    }

    template<typename T>
    std::vector<T> StorageBuffer::Read(size_t count, size_t offsetBytes) const {
        std::vector<T> result(count);
        const size_t bytes = count * sizeof(T);
        if (bytes == 0 || offsetBytes + bytes > sizeBytes) return result;
        glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, id);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, offsetBytes, bytes, result.data());
        return result;
    }
} // core

#endif //RAWENGINE_STORAGEBUFFER_H