//
// Owns the materials of ray-marched primitives and everything march.frag needs to shade them:
//  - a GL_TEXTURE_2D_ARRAY with every texture the materials use (GL 4.3 has no bindless textures)
//  - an SSBO with one GPUMaterial per material (binding 6)
//  - the march shader program, with each material's custom surface snippet stitched in
//

#ifndef RAWENGINE_RAYMARCHMATERIALLIBRARY_H
#define RAWENGINE_RAYMARCHMATERIALLIBRARY_H

#include <chrono>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>
#include <glad/glad.h>
#include <glm/glm.hpp>

#include "Assets/RaymarchMaterial.h"

namespace core {
    // std430 layout, mirrored by struct Material in march.frag
    struct GPUMaterial {
        glm::vec4 tint;        // rgb tint, a = normal strength
        glm::vec4 params;      // x texture scale, y blend sharpness, z smoothness, w metallic
        glm::ivec4 layers;     // texture array layers: x albedo, y normal, z roughness (-1 = none), w surface shader index
        glm::ivec4 layered;    // sub-material indices: x flat, y steep, z high (-1 = none), w flags (1 object space, 2 layered)
        glm::vec4 layerRanges; // x slope start, y slope end, z height start, w height end
        glm::vec4 extra;       // x layer noise, yz texture offset
    };

    class RaymarchMaterialLibrary {
    public:
        static constexpr int TEXTURE_SIZE = 1024;      // every texture array layer is resized to this
        static constexpr GLuint TEXTURE_UNIT = 15;     // above the units Material::Bind hands out
        static constexpr GLuint MATERIAL_BINDING = 6;  // SSBO binding

        RaymarchMaterialLibrary();
        ~RaymarchMaterialLibrary();

        static RaymarchMaterialLibrary* Instance() { return instance; }

        // Index of the material stored at path (loaded on first use); 0 = default material for "" or a missing file.
        int IndexOf(const std::string& path);
        RaymarchMaterial* Get(const std::string& path);
        void MarkDirty() { dirty = true; } // a material was edited

        // Once per frame: reload changed snippet files, rebuild the texture array / SSBO / shader if needed.
        void Update();
        // True once after the set of snippets (or a snippet file) changed: the march program must be rebuilt.
        bool ShaderChanged();
        // Stitches the snippets into the march shader template and links it. Returns 0 and prints the error (with
        // snippet file and line) if it doesn't compile, so the caller can keep its previous program.
        GLuint BuildMarchProgram(const std::string& vertexPath, const std::string& fragmentTemplatePath);
        // Binds the texture array, the material SSBO and their uniforms to the (current) march program.
        void Bind(GLuint program) const;

        static std::vector<std::string> ListMaterialFiles(); // Assets/Materials/*.json
        static std::vector<std::string> ListSurfaceShaders(); // Assets/shaders/raymarching/surfaces/*.glsl
        void DrawMaterialEditor(const std::string& path); // inspector UI for one material

    private:
        static RaymarchMaterialLibrary* instance;

        std::vector<RaymarchMaterial> materials; // [0] = default
        std::unordered_map<std::string, int> indexOf;
        bool dirty = true;

        GLuint textureArray = 0;
        std::vector<std::string> textureLayers; // file per layer
        GLuint materialBuffer = 0;

        std::vector<std::string> snippets;      // shader index n = snippets[n - 1]
        std::unordered_map<std::string, std::filesystem::file_time_type> snippetTimes;
        bool shaderChanged = false;
        std::chrono::steady_clock::time_point lastSnippetCheck{};

        int Load(const std::string& path);
        void RebuildTextureArray(const std::vector<std::string>& files);
        static int LayerOf(const std::vector<std::string>& files, const std::string& file);
    };
} // core

#endif //RAWENGINE_RAYMARCHMATERIALLIBRARY_H
