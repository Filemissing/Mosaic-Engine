//
// Created by micha on 09/09/2026.
//

#ifndef RAWENGINE_RAYMARCHER_H
#define RAWENGINE_RAYMARCHER_H
#include <unordered_map>
#include <vector>

#include "Assets/ComputeShader.h"
#include "Assets/Material.h"
#include "RaymarchMaterialLibrary.h"
#include "Assets/model.h"
#include "Components/Camera.h"
#include "Components/Primitive.h"
#include "GL/StorageBuffer.h"
#include "GL/Texture2D.h"
#include "GL/Texture3D.h"

namespace core {
    class Raymarcher {
        // GL_TIMESTAMP pairs (start, end): unlike GL_TIME_ELAPSED they can run inside the Renderer's frame query
        GLuint m_timeQuery[2] = {0, 0};
        bool m_queryInFlight = false;
        GLuint m_partialQuery[2] = {0, 0};
        bool m_partialQueryInFlight = false;

        // general settings
        bool dirty = true; // a full rebake is needed (volume settings, noise or terrain changed)

        // Octree
        glm::ivec3 resolution{};
        glm::vec3 worldMin{}, worldMax{};

        struct BrickBox {
            glm::ivec3 min{0}, max{-1};
            bool Empty() const { return max.x < min.x; }
        };
        struct OctreeLevel {
            float voxelSize = 0.0f;
            glm::vec3 gridOrigin{0.0f};
            int brickGridResolution = 0;
            gl::Texture3D brickSignTex;         // R8, brick grid, sign of the brick's origin, used to determine solid/empty when inactive
            gl::Texture3D lookupTex;            // RGBA32F, brick grid, xyz = this brick's origin in pool space, w = 1 active / 0 empty / -1 solid
            gl::Texture3D dirtyTex;             // R8, brick grid, 1 = brick still has to be rebaked
            glm::ivec3 chunkGridDim{1};   // chunks per axis
            std::vector<BrickBox> chunkDirty;   // mirrors dirtyTex

            float BrickSize() const { return voxelSize * BRICK_CORE; }
            float GridSize() const { return BrickSize() * static_cast<float>(brickGridResolution); }
        };
        int treeLevelCount = 5;
        std::vector<OctreeLevel> treeLevels; // index 0 = finest

        // noise generation
        gl::Texture2D noise2DTex;
        gl::Texture3D noise3DTex;

        ComputeShader* noise2DShader;
        ComputeShader* noise3DShader;

        // primitives, stored by stable slot so baked ids survive adding/removing other primitives
        struct PrimitiveSlot {
            bool used = false;
            bool baked = false;        // bakedGpu/aabb describe what's currently in the volume
            GPUPrimitive bakedGpu{};
            AABB aabb{};               // world bounds of the baked state (unused when unbounded)
            bool unbounded = false;    // plane/terrain: changes need a full rebake
        };
        gl::StorageBuffer primitivesBuffer{gl::ssbo::Primitives}; // SSBO binding 0, one GPUPrimitive per slot (free slots have type -1)
        gl::StorageBuffer primitiveIndexBuffer{gl::ssbo::PrimitiveIndices}; // SSBO binding 2, slots classify has to evaluate for the current dispatch
        std::unordered_map<Primitive*, uint32_t> slotOf;
        std::vector<PrimitiveSlot> slots;
        std::vector<uint32_t> freePrimitiveSlots;
        std::vector<GPUPrimitive> gpuSlots;

        void SyncPrimitiveSlots(const std::vector<Primitive*>& primitives);
        void UploadPrimitiveSlots();
        void UploadPrimitiveIndices(const std::vector<uint32_t>& indices);

        // brick pool (persistent)
        static constexpr int BRICK_CORE = 8;
        static constexpr int BRICK_STORE = BRICK_CORE + 1; // 9 — includes the apron sample
        static constexpr int HALO = 2 * BRICK_CORE;       // voxels of context around a chunk; also the distance truncation

        glm::ivec3 brickPoolDim = glm::ivec3(64); // the dimensions of the brick pool

        gl::StorageBuffer freeListBuffer{gl::ssbo::FreeList}; // SSBO binding 1: int freeCount, int droppedBricks, uint freeSlots[capacity]

        gl::Texture3D brickPoolTex;   // R16F, holds baked distances, laid out as brickPoolDim * BRICK_STORE texels
        gl::Texture3D brickIdTex;     // R16UI, same layout as brickPoolTex, primitive slot of the nearest primitive (for color)

        void EnsureBrickPoolSized();
        void EnsureLevelResourcesSized(OctreeLevel& level);
        void ResetFreeList();

        // chunked bake (scratch, sized from the VRAM budget)
        ComputeShader* classifyShader = nullptr;
        ComputeShader* jfaStepShader = nullptr;
        ComputeShader* jfaFinalizeShader = nullptr;
        ComputeShader* fillShader = nullptr;
        ComputeShader* releaseShader = nullptr;
        ComputeShader* allocateShader = nullptr;
        ComputeShader* settleShader = nullptr;
        ComputeShader* seedFixShader = nullptr;

        int chunkBricks = 1;                     // bricks per chunk axis
        glm::ivec3 scratchDim{0};          // allocated scratch size in voxels
        gl::Texture3D seedTexA, seedTexB;        // RGBA16F seed ping-pong, chunk-local coordinates
        gl::Texture3D* finalSeedTex = nullptr;
        gl::Texture3D signTex;                   // R8
        gl::Texture3D idTex;                     // R16UI
        gl::Texture3D distTex;                   // R16F, raw SDF value, used by seedFix to place seeds on missed sign changes
        gl::Texture3D brickFlagsTex;             // R8, (C + 4)^3 bricks: seed per brick for the core and a 2 brick halo
        gl::StorageBuffer chunkFlagsBuffer{gl::ssbo::ChunkFlags}; // SSBO binding 3, one uint per chunk, written by the coarse pass

        // dirty bookkeeping on the CPU, one box (in bricks) per chunk
        bool fullRebakeRequested = false;

        void EnsureScratchSized();
        void RechunkLevel(OctreeLevel& level); // split a level into chunks of chunkBricks, keeps its pending dirty boxes
        void Fill(gl::Texture3D& r8Tex, glm::ivec3 origin, glm::ivec3 dim, float value);
        void StampDirty(OctreeLevel& level, glm::ivec3 brickMin, glm::ivec3 brickMax);
        void StampDirtyWorld(const AABB& bounds);
        void BakeLevel(OctreeLevel& level); // full bake of one level: coarse pass, settle, then every flagged chunk
        void BakeChunkBox(OctreeLevel& level, const BrickBox& box);
        int ProcessDirtyChunks(int maxChunks, glm::vec3 cameraPos);
        void SetCommonClassifyUniforms();
        void SetLevelUniforms(ComputeShader* shader, const OctreeLevel& level);
        void ReadBackFreeList();

        // final output
        Material* marchMaterial;
        RaymarchMaterialLibrary* materialLibrary = nullptr; // materials of the primitives, textures, surface snippets
        void UpdateMaterials();
        Material* debugSliceMaterial;
        Material* debugTextureMaterial;
        Model* quadModel;

    public:
        // general settings
        int maxSteps = 1024;             // empty bricks cost a step each: a diagonal ray at 1024^3 crosses ~380 of them
        float maxDist = 600.0f;
        float surfDist = 0.001f;
        float colorBlend = 1.0f;         // 0 = hard color cutoff between primitives, 1 = trilinear fade over one voxel

        // bake settings
        int bakeBudgetMiB = 128;         // scratch VRAM a single chunk may use while baking
        float coarseMargin = 2.0f;       // >1 keeps the coarse skip safe for SDFs that aren't exact distances (terrain)
        bool autoRebake = true;          // rebake changed primitives (and full rebakes) automatically every frame
        int maxRebakeChunksPerFrame = 4; // 0 = unlimited; leftover dirty chunks stay marked for later frames

        // bake stats (read-only, for the UI)
        int lastChunksProcessed = 0;
        int pendingDirtyChunks = 0;
        int poolSlotsUsed = 0;
        int droppedBricks = 0;

        // noise settings
        glm::ivec2 noise2DResolution{512, 512};
        glm::ivec3 noise3DResolution{256, 256, 256};

        float noise2DFrequency = 1.0f;
        float noise3DFrequency = 1.0f;

        unsigned int noiseSeed = 12345;

        void EnsureNoiseResourcesSized();
        void BakeNoise();

        // terrain settings
        int octaves = 8;
        float warpStrength = 1.0f;
        float lacunarity = 2.0f;
        float persistence = 0.3f;
        float heightScale = 60.0f;      // world units from the lowest to the highest plateau
        float terrainScale = 0.12f;     // frequency of the large-scale elevation (where the mesas are)
        float terraceCount = 5.0f;      // plateau levels over the full height
        float cliffSharpness = 0.85f;   // 0 = smooth slopes between levels, 1 = sheer walls
        float detailAmount = 0.02f;     // small surface variation, fraction of the height scale
        float overhangStrength = 0.3f;  // how far cliff walls lean in and out
        float overhangScale = 1.5f;
        float cliffRoughness = 1.0f;    // world units of small-scale jitter on the cliff walls

        // debug settings
        int forceLevel = -1; // -1 = diabled/automatic
        bool showLevels = false;
        bool debug3D = false;
        bool debug2D = false;
        int sliceZ = 0;
        int mode = 0;
        float displayScale = 1.0f;
        enum DebugTexture3D {
            dSignTex,       // scratch, last chunk
            dSeedTex,       // scratch, last chunk
            dNoise3DTex,
            dBrickFlagsTex, // scratch, last chunk
            dLookupTex,
            dDirtyTex,
            dBrickPoolTex
        };
        DebugTexture3D debugTex3D = dSignTex;
        enum DebugTexture2D {
            dNoise2DTex
        };
        DebugTexture2D debugTex2D = dNoise2DTex;

        Raymarcher();
        ~Raymarcher();

        // general methods
        void EnsureVolumeSized(glm::ivec3 newResolution);
        void ApplyBakeBudget(); // recompute the chunk size from bakeBudgetMiB for all levels, keeps pending dirty marks
        void Bake(const std::vector<Primitive*>& primitives); // full bake of every level, synchronous
        void UpdateDynamic(const std::vector<Primitive*>& primitives, glm::vec3 cameraPos); // per frame, before Render
        void Render(GLuint targetFbo, GLuint sceneColorTex, GLuint sceneDepthTex, Camera* cam);
        void RenderDebugSlice(GLuint targetFbo, gl::Texture3D* textureToView);
        void RenderDebugTexture(GLuint targetFbo, gl::Texture2D* textureToView);

        void SetWorldBounds(glm::vec3 min, glm::vec3 max);
        int GetLevelCount() const { return treeLevelCount; };
        glm::vec3 GetWorldMin() {return worldMin; }
        glm::vec3 GetWorldMax() {return worldMax; }
        void MarkDirty() {dirty = true;}
        bool IsDirty() const { return dirty; }

        glm::ivec3 GetResolution() const { return resolution; }
        int GetChunkBricks() const { return chunkBricks; }
        int GetPoolCapacity() const { return brickPoolDim.x * brickPoolDim.y * brickPoolDim.z; }

        glm::ivec3 GetBrickPoolDim() const { return brickPoolDim; }
        void SetBrickPoolDim(const glm::ivec3 dim) { brickPoolDim = dim; }
    };
} // core

#endif //RAWENGINE_RAYMARCHER_H
