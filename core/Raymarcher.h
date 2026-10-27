//
// Created by micha on 09/09/2026.
//

#ifndef RAWENGINE_RAYMARCHER_H
#define RAWENGINE_RAYMARCHER_H
#include <unordered_map>
#include <vector>

#include "Assets/ComputeShader.h"
#include "Assets/Material.h"
#include "Assets/model.h"
#include "Components/Camera.h"
#include "Components/Primitive.h"

namespace core {
    class Raymarcher {
        // GL_TIMESTAMP pairs (start, end): unlike GL_TIME_ELAPSED they can run inside the Renderer's frame query
        GLuint m_timeQuery[2] = {0, 0};
        bool m_queryInFlight = false;
        GLuint m_partialQuery[2] = {0, 0};
        bool m_partialQueryInFlight = false;

        // general settings
        glm::ivec3 resolution{};
        glm::vec3 worldMin{}, worldMax{};
        bool dirty = true; // a full rebake is needed (volume settings, noise or terrain changed)

        // noise generation
        GLuint noise2DTex = 0;
        GLuint noise3DTex = 0;

        ComputeShader* noise2DShader;
        ComputeShader* noise3DShader;

        // ---- primitives, stored by stable slot so baked ids survive adding/removing other primitives ----
        struct PrimitiveSlot {
            bool used = false;
            bool baked = false;        // bakedGpu/aabb describe what's currently in the volume
            GPUPrimitive bakedGpu{};
            glm::vec3 aabbMin{}, aabbMax{};
            bool unbounded = false;    // plane/terrain: changes need a full rebake
        };
        GLuint primitivesBuffer = 0;     // SSBO binding 0, one GPUPrimitive per slot (free slots have type -1)
        GLuint primitiveIndexBuffer = 0; // SSBO binding 2, slots classify has to evaluate for the current dispatch
        std::unordered_map<Primitive*, uint32_t> slotOf;
        std::vector<PrimitiveSlot> slots;
        std::vector<uint32_t> freePrimitiveSlots;
        std::vector<GPUPrimitive> gpuSlots;

        void SyncPrimitiveSlots(const std::vector<Primitive*>& primitives);
        void UploadPrimitiveSlots();
        void UploadPrimitiveIndices(const std::vector<uint32_t>& indices);
        bool PrimitiveWorldAABB(const GPUPrimitive& p, glm::vec3& outMin, glm::vec3& outMax) const; // false = unbounded

        // ---- brick pool (persistent) ----
        static constexpr int BRICK_CORE = 8;
        static constexpr int BRICK_STORE = BRICK_CORE + 1; // 9 — includes the apron sample
        static constexpr int HALO = 2 * BRICK_CORE;       // voxels of context around a chunk; also the distance truncation

        glm::ivec3 brickGridResolution = glm::ivec3(0); // number of bricks per axis
        glm::ivec3 brickPoolDim = glm::ivec3(64); // the dimensions of the brick pool

        GLuint freeListBuffer = 0; // SSBO binding 1: int freeCount, int droppedBricks, uint freeSlots[capacity]

        GLuint brickPoolTex = 0;   // R16F, holds baked distances, laid out as brickPoolDim * BRICK_STORE texels
        GLuint brickIdTex = 0;     // R16UI, same layout as brickPoolTex, primitive slot of the nearest primitive (for color)
        GLuint brickSignTex = 0;   // R8, brick grid, sign of the brick's origin, used to determine solid/empty when inactive
        GLuint lookupTex = 0;      // RGBA32F, brick grid, xyz = this brick's origin in pool space, w = 1 active / 0 empty / -1 solid
        GLuint dirtyTex = 0;       // R8, brick grid, 1 = brick still has to be rebaked

        void EnsureBrickResourcesSized();
        void DestroyBrickResources();
        void ResetFreeList();

        // ---- chunked bake (scratch, sized from the VRAM budget) ----
        ComputeShader* classifyShader = nullptr;
        ComputeShader* jfaStepShader = nullptr;
        ComputeShader* jfaFinalizeShader = nullptr;
        ComputeShader* fillShader = nullptr;
        ComputeShader* releaseShader = nullptr;
        ComputeShader* allocateShader = nullptr;
        ComputeShader* settleShader = nullptr;
        ComputeShader* seedFixShader = nullptr;

        int chunkBricks = 1;                  // C: bricks per chunk axis
        glm::ivec3 chunkGridDim{1};
        glm::ivec3 scratchDim{0};             // allocated scratch size in voxels
        GLuint seedTexA = 0, seedTexB = 0;    // RGBA16F seed ping-pong, chunk-local coordinates
        GLuint finalSeedTex = 0;
        GLuint signTex = 0;                   // R8
        GLuint idTex = 0;                     // R16UI
        GLuint distTex = 0;                   // R16F, raw SDF value, used by seedFix to place seeds on missed sign changes
        GLuint brickFlagsTex = 0;             // R8, (C + 4)^3 bricks: seed per brick for the core and a 2 brick halo
        GLuint chunkFlagsBuffer = 0;          // SSBO binding 3, one uint per chunk, written by the coarse pass

        // dirty bookkeeping on the CPU, one box (in bricks) per chunk; mirrors which bits of dirtyTex are set
        struct BrickBox {
            glm::ivec3 min{0}, max{-1};
            bool Empty() const { return max.x < min.x; }
        };
        std::vector<BrickBox> chunkDirty;
        bool fullRebakeRequested = false;

        void EnsureScratchSized();
        void DestroyScratchResources();
        void Fill(GLuint r8Tex, glm::ivec3 origin, glm::ivec3 dim, float value);
        void StampDirty(glm::ivec3 brickMin, glm::ivec3 brickMax);
        void StampDirtyWorld(glm::vec3 min, glm::vec3 max);
        void BakeChunkBox(const BrickBox& box);
        int ProcessDirtyChunks(int maxChunks, glm::vec3 cameraPos);
        void SetCommonClassifyUniforms();
        void ReadBackFreeList();

        // final output
        Material* marchMaterial;
        Material* debugSliceMaterial;
        Material* debugTextureMaterial;
        Model* quadModel;

    public:
        // general settings
        int maxSteps = 256;
        float maxDist = 200.0f;
        float surfDist = 0.001f;
        float colorBlend = 1.0f; // 0 = hard color cutoff between primitives, 1 = trilinear fade over one voxel

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
        void DestroyNoiseResources();
        void BakeNoise();

        // terrain settings
        int octaves = 8;
        float warpStrength = 1.0f;
        float lacunarity = 2.0f;
        float persistence = 0.3f;
        float heightScale = 8.0f;

        // debug settings
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
        void ApplyBakeBudget(); // recompute the chunk size from bakeBudgetMiB, keeps pending dirty marks
        void Bake(const std::vector<Primitive*>& primitives); // full bake, synchronous
        void UpdateDynamic(const std::vector<Primitive*>& primitives, glm::vec3 cameraPos); // per frame, before Render
        void Render(GLuint targetFbo, GLuint sceneColorTex, GLuint sceneDepthTex, Camera* cam);
        void RenderDebugSlice(GLuint targetFbo, GLuint textureToView);
        void RenderDebugTexture(GLuint targetFbo, GLuint textureToView);

        void SetWorldBounds(glm::vec3 min, glm::vec3 max);
        void MarkDirty() {dirty = true;}
        bool IsDirty() const { return dirty; }

        glm::ivec3 GetResolution() const { return resolution; }
        glm::vec3 GetWorldMin() const { return worldMin; }
        glm::vec3 GetWorldMax() const { return worldMax; }
        int GetChunkBricks() const { return chunkBricks; }
        int GetPoolCapacity() const { return brickPoolDim.x * brickPoolDim.y * brickPoolDim.z; }

        glm::ivec3 GetBrickPoolDim() const { return brickPoolDim; }
        void SetBrickPoolDim(const glm::ivec3 dim) { brickPoolDim = dim; }
    };
} // core

#endif //RAWENGINE_RAYMARCHER_H
