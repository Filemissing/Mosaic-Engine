//
// Common interface over every raymarcher version, so they can be swapped at runtime and benchmarked identically.
// The adapters only forward calls; each version's own rendering/baking code is left as it was at its commit.
//

#ifndef RAWENGINE_RAYMARCHBACKEND_H
#define RAWENGINE_RAYMARCHBACKEND_H

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <glad/glad.h>
#include <glm/glm.hpp>

namespace core {
    class Camera;
    class Primitive;
    class Raymarcher;

    class RaymarchBackend {
    public:
        virtual ~RaymarchBackend() = default;

        virtual const char* Id() const = 0;

        // false for versions that march the primitives directly and have nothing to bake
        virtual bool HasBake() const { return true; }
        virtual bool SupportsTerrain() const { return false; }

        virtual void SetVolume(glm::ivec3 resolution, glm::vec3 worldMin, glm::vec3 worldMax) {}
        // brick pool size in bricks per axis, for versions with a sparse brick pool (v3 and later); takes effect on
        // the next SetVolume. Returns false for versions without a pool.
        virtual bool SetBrickPoolDim(glm::ivec3 dim) { return false; }
        virtual void SetMarchParams(int maxSteps, float maxDist, float surfDist) = 0;

        // anything the version needs before Bake that is not part of the bake itself (e.g. noise textures)
        virtual void PrepareBake() {}
        virtual void Bake(const std::vector<Primitive*>& primitives) {}
        // called every frame before Render, outside the timed raymarch pass (v6 rebakes changed primitives here)
        virtual void Update(const std::vector<Primitive*>& primitives, glm::vec3 cameraPos) {}
        virtual void Render(GLuint targetFbo, GLuint sceneColorTex, GLuint sceneDepthTex, Camera* cam) = 0;

        // the current-version raymarcher, for the existing Ray-marcher settings window; nullptr for older versions
        virtual Raymarcher* GetCurrentRaymarcher() { return nullptr; }
    };

    struct RaymarchVersionInfo {
        std::string id;
        std::string description;
        std::string sourceCommit;
        bool supportsTerrain;
        std::function<std::unique_ptr<RaymarchBackend>()> create;
    };

    const std::vector<RaymarchVersionInfo>& GetRaymarchVersions();
    const RaymarchVersionInfo* FindRaymarchVersion(const std::string& id);
} // core

#endif //RAWENGINE_RAYMARCHBACKEND_H
