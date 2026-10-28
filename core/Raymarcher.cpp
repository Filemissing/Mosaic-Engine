//
// Created by micha on 09/09/2026.
//

#include "Raymarcher.h"

#include <algorithm>
#include <cmath>
#include <iostream>

#include "../editor/Editor.h"
#include "../editor/EditorWindows/BenchmarkTool.h"

#define GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX 0x9048
#define GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_MEMORY_NVX 0x9049

namespace core {
    namespace {
        void CreateTex3D(GLuint& tex, GLenum internalFormat, glm::ivec3 dim, GLenum format, GLenum type,
                         GLenum filter, const void* data = nullptr) {
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_3D, tex);
            glTexImage3D(GL_TEXTURE_3D, 0, internalFormat, dim.x, dim.y, dim.z, 0, format, type, data);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, filter);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, filter);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        }
        void DeleteTex(GLuint& tex) {
            if (tex) glDeleteTextures(1, &tex);
            tex = 0;
        }
        glm::ivec3 Groups(glm::ivec3 dim) { return (dim + glm::ivec3(7)) / 8; }

        // same matrix as eulerToMat3 in classify.comp (glm's mat3 constructor is column-major, like GLSL's)
        glm::mat3 EulerToMat3(glm::vec3 euler) {
            glm::vec3 rad = glm::radians(euler);
            float cx = std::cos(rad.x), sx = std::sin(rad.x);
            float cy = std::cos(rad.y), sy = std::sin(rad.y);
            float cz = std::cos(rad.z), sz = std::sin(rad.z);
            glm::mat3 rx(1, 0, 0, 0, cx, -sx, 0, sx, cx);
            glm::mat3 ry(cy, 0, sy, 0, 1, 0, -sy, 0, cy);
            glm::mat3 rz(cz, -sz, 0, sz, cz, 0, 0, 0, 1);
            return rz * ry * rx;
        }

        bool GeometryDiffers(const GPUPrimitive& a, const GPUPrimitive& b) {
            return a.position != b.position || a.rotation != b.rotation || a.scale != b.scale || a.data != b.data;
        }

        constexpr int SCRATCH_BYTES_PER_VOXEL = 8 + 8 + 1 + 2 + 2; // seed A + seed B (RGBA16F), sign (R8), id (R16UI), dist (R16F)
    }

    Raymarcher::Raymarcher() {
        glGenQueries(2, m_timeQuery);
        glGenQueries(2, m_partialQuery);

        glGenBuffers(1, &primitivesBuffer);
        glGenBuffers(1, &primitiveIndexBuffer);
        glGenBuffers(1, &freeListBuffer);
        glGenBuffers(1, &chunkFlagsBuffer);
        gpuSlots.resize(1);
        gpuSlots[0].position.w = -1; // never bind an empty SSBO

        marchMaterial = new Material(
            "Assets/shaders/PostProcessing/viewSpace.vert",
            "Assets/shaders/raymarching/march.frag");
        debugSliceMaterial = new Material(
            "Assets/shaders/PostProcessing/viewSpace.vert",
            "Assets/shaders/debug/slice_view.frag");
        debugTextureMaterial = new Material(
            "Assets/shaders/PostProcessing/viewSpace.vert",
            "Assets/shaders/debug/texture_view.frag");

        noise2DShader = new ComputeShader("Assets/shaders/raymarching/noise2D.comp");
        noise3DShader = new ComputeShader("Assets/shaders/raymarching/noise3D.comp");

        EnsureNoiseResourcesSized();

        classifyShader = new ComputeShader("Assets/shaders/raymarching/classify.comp");
        jfaStepShader = new ComputeShader("Assets/shaders/raymarching/jfaStep.comp");
        jfaFinalizeShader = new ComputeShader("Assets/shaders/raymarching/jfaFinalize.comp");
        fillShader = new ComputeShader("Assets/shaders/raymarching/clear.comp");
        releaseShader = new ComputeShader("Assets/shaders/raymarching/release.comp");
        allocateShader = new ComputeShader("Assets/shaders/raymarching/allocate.comp");
        settleShader = new ComputeShader("Assets/shaders/raymarching/settle.comp");
        seedFixShader = new ComputeShader("Assets/shaders/raymarching/seedFix.comp");

        std::vector<Mesh> meshes = std::vector<Mesh>();
        meshes.push_back(Mesh::generateQuad());
        quadModel = new Model(std::move(meshes));

        SetWorldBounds(glm::vec3(-100), glm::vec3(100));
        EnsureVolumeSized(glm::ivec3(1024));
    }
    Raymarcher::~Raymarcher() {
        glDeleteQueries(2, m_timeQuery);
        glDeleteQueries(2, m_partialQuery);
        glDeleteBuffers(1, &primitivesBuffer);
        glDeleteBuffers(1, &primitiveIndexBuffer);
        glDeleteBuffers(1, &freeListBuffer);
        glDeleteBuffers(1, &chunkFlagsBuffer);
        DestroyScratchResources();
        DestroyBrickResources();
        DestroyNoiseResources();

        delete marchMaterial;
        delete debugSliceMaterial;
        delete debugTextureMaterial;
        delete quadModel;

        delete noise2DShader;
        delete noise3DShader;

        delete classifyShader;
        delete jfaStepShader;
        delete jfaFinalizeShader;
        delete fillShader;
        delete releaseShader;
        delete allocateShader;
        delete settleShader;
        delete seedFixShader;
    }

    // ---------------------------------------------------------------------------------------------------------
    // resources
    // ---------------------------------------------------------------------------------------------------------
    void Raymarcher::EnsureVolumeSized(glm::ivec3 newResolution) {
        resolution = newResolution;
        EnsureBrickResourcesSized();

        chunkDirty.clear(); // the old grid is gone, everything is rebaked anyway
        ApplyBakeBudget();

        for (auto& slot : slots) slot.baked = false;
        dirty = true;

        // Final error check
        while (GLenum e = glGetError()) {
            std::cerr << "GL error after EnsureVolumeSized(): 0x" << std::hex << e << std::dec << std::endl;
        }
    }
    void Raymarcher::EnsureBrickResourcesSized() {
        DestroyBrickResources();

        brickGridResolution = (resolution + glm::ivec3(BRICK_CORE - 1)) / BRICK_CORE; // round up
        glm::ivec3 poolTexelSize = brickPoolDim * BRICK_STORE;
        size_t brickCount = static_cast<size_t>(brickGridResolution.x) * brickGridResolution.y * brickGridResolution.z;

        CreateTex3D(brickPoolTex, GL_R16F, poolTexelSize, GL_RED, GL_HALF_FLOAT, GL_LINEAR);
        CreateTex3D(brickIdTex, GL_R16UI, poolTexelSize, GL_RED_INTEGER, GL_UNSIGNED_SHORT, GL_NEAREST); // NEAREST — integer ids can't be filtered

        // brick grid textures start zeroed: everything empty, nothing dirty, until the first bake
        std::vector<float> zeroLookup(brickCount * 4, 0.0f);
        std::vector<unsigned char> zeroBytes(brickCount, 0);
        CreateTex3D(lookupTex, GL_RGBA32F, brickGridResolution, GL_RGBA, GL_FLOAT, GL_NEAREST, zeroLookup.data());
        CreateTex3D(brickSignTex, GL_R8, brickGridResolution, GL_RED, GL_UNSIGNED_BYTE, GL_NEAREST, zeroBytes.data());
        CreateTex3D(dirtyTex, GL_R8, brickGridResolution, GL_RED, GL_UNSIGNED_BYTE, GL_NEAREST, zeroBytes.data());

        ResetFreeList();
    }
    void Raymarcher::DestroyBrickResources() {
        DeleteTex(brickPoolTex);
        DeleteTex(brickIdTex);
        DeleteTex(lookupTex);
        DeleteTex(brickSignTex);
        DeleteTex(dirtyTex);
    }
    void Raymarcher::ResetFreeList() {
        const int capacity = GetPoolCapacity();
        std::vector<GLuint> data(2 + capacity);
        data[0] = static_cast<GLuint>(capacity); // freeCount
        data[1] = 0;                             // droppedBricks
        for (int i = 0; i < capacity; i++) data[2 + i] = static_cast<GLuint>(capacity - 1 - i); // pops hand out slot 0 first

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, freeListBuffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER, data.size() * sizeof(GLuint), data.data(), GL_DYNAMIC_DRAW);
        poolSlotsUsed = 0;
        droppedBricks = 0;
    }

    void Raymarcher::ApplyBakeBudget() {
        // keep the pending dirty marks: dirtyTex keeps its bits, only the CPU per-chunk boxes are re-split
        std::vector<BrickBox> pending;
        for (const auto& box : chunkDirty) if (!box.Empty()) pending.push_back(box);

        double budgetBytes = static_cast<double>(std::max(bakeBudgetMiB, 1)) * 1024.0 * 1024.0;
        GLint availKb = 0;
        glGetIntegerv(GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_MEMORY_NVX, &availKb);
        while (glGetError() != GL_NO_ERROR) {} // the NVX query fails on other vendors, the budget alone is used then
        if (availKb > 0) {
            double currentScratch = static_cast<double>(scratchDim.x) * scratchDim.y * scratchDim.z * SCRATCH_BYTES_PER_VOXEL;
            budgetBytes = std::min(budgetBytes, 0.5 * (availKb * 1024.0 + currentScratch)); // never take more than half the free VRAM
        }

        int side = static_cast<int>(std::cbrt(budgetBytes / SCRATCH_BYTES_PER_VOXEL));
        int maxGrid = std::max({brickGridResolution.x, brickGridResolution.y, brickGridResolution.z, 1});
        chunkBricks = std::clamp((side - 2 * HALO) / BRICK_CORE, 1, maxGrid);
        chunkGridDim = (brickGridResolution + glm::ivec3(chunkBricks - 1)) / chunkBricks;

        chunkDirty.assign(static_cast<size_t>(chunkGridDim.x) * chunkGridDim.y * chunkGridDim.z, BrickBox{});
        for (const auto& box : pending) {
            // CPU side only, the GPU bits are already set
            glm::ivec3 c0 = box.min / chunkBricks, c1 = box.max / chunkBricks;
            for (int z = c0.z; z <= c1.z; z++)
            for (int y = c0.y; y <= c1.y; y++)
            for (int x = c0.x; x <= c1.x; x++) {
                glm::ivec3 c(x, y, z);
                glm::ivec3 lo = glm::max(box.min, c * chunkBricks);
                glm::ivec3 hi = glm::min(box.max, glm::min(c * chunkBricks + chunkBricks - 1, brickGridResolution - 1));
                BrickBox& d = chunkDirty[x + y * chunkGridDim.x + z * chunkGridDim.x * chunkGridDim.y];
                if (d.Empty()) { d.min = lo; d.max = hi; }
                else { d.min = glm::min(d.min, lo); d.max = glm::max(d.max, hi); }
            }
        }

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunkFlagsBuffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER, chunkDirty.size() * sizeof(GLuint), nullptr, GL_DYNAMIC_DRAW);

        EnsureScratchSized();
    }
    void Raymarcher::EnsureScratchSized() {
        glm::ivec3 dim = glm::min(glm::ivec3(chunkBricks * BRICK_CORE + 2 * HALO), resolution);
        if (dim == scratchDim && seedTexA) return;
        DestroyScratchResources();
        scratchDim = dim;

        CreateTex3D(seedTexA, GL_RGBA16F, scratchDim, GL_RGBA, GL_HALF_FLOAT, GL_NEAREST); // NEAREST — these are positions, not colors to blend
        CreateTex3D(seedTexB, GL_RGBA16F, scratchDim, GL_RGBA, GL_HALF_FLOAT, GL_NEAREST);
        CreateTex3D(signTex, GL_R8, scratchDim, GL_RED, GL_UNSIGNED_BYTE, GL_NEAREST);
        CreateTex3D(idTex, GL_R16UI, scratchDim, GL_RED_INTEGER, GL_UNSIGNED_SHORT, GL_NEAREST);
        CreateTex3D(distTex, GL_R16F, scratchDim, GL_RED, GL_HALF_FLOAT, GL_NEAREST);
        CreateTex3D(brickFlagsTex, GL_R8, glm::ivec3(chunkBricks + 4), GL_RED, GL_UNSIGNED_BYTE, GL_NEAREST);
        finalSeedTex = seedTexA;
    }
    void Raymarcher::DestroyScratchResources() {
        DeleteTex(seedTexA);
        DeleteTex(seedTexB);
        DeleteTex(signTex);
        DeleteTex(idTex);
        DeleteTex(distTex);
        DeleteTex(brickFlagsTex);
        finalSeedTex = 0;
        scratchDim = glm::ivec3(0);
    }

    // ---------------------------------------------------------------------------------------------------------
    // primitives
    // ---------------------------------------------------------------------------------------------------------
    void Raymarcher::SyncPrimitiveSlots(const std::vector<Primitive*>& primitives) {
        // assert offsets
        static_assert(offsetof(GPUPrimitive, position) == 0);
        static_assert(offsetof(GPUPrimitive, rotation) == 16);
        static_assert(offsetof(GPUPrimitive, scale)    == 32);
        static_assert(offsetof(GPUPrimitive, data)     == 48);
        static_assert(offsetof(GPUPrimitive, color)    == 64);
        static_assert(sizeof(GPUPrimitive) == 80);

        std::unordered_map<Primitive*, bool> present;
        for (Primitive* p : primitives) {
            present[p] = true;
            if (slotOf.find(p) != slotOf.end()) continue;

            uint32_t s;
            if (!freePrimitiveSlots.empty()) { s = freePrimitiveSlots.back(); freePrimitiveSlots.pop_back(); }
            else { s = static_cast<uint32_t>(slots.size()); slots.emplace_back(); }
            slots[s] = PrimitiveSlot{};
            slots[s].used = true;
            slotOf[p] = s;
        }

        // removed primitives: their baked surface has to be cleared from the volume before the slot is reused
        for (auto it = slotOf.begin(); it != slotOf.end();) {
            if (present.count(it->first)) { ++it; continue; }
            PrimitiveSlot& slot = slots[it->second];
            if (slot.baked) {
                if (slot.unbounded) fullRebakeRequested = true;
                else if (autoRebake) StampDirtyWorld(slot.aabbMin, slot.aabbMax);
                else dirty = true;
            }
            slot = PrimitiveSlot{};
            freePrimitiveSlots.push_back(it->second);
            it = slotOf.erase(it);
        }

        gpuSlots.assign(std::max<size_t>(slots.size(), 1), GPUPrimitive{});
        for (auto& g : gpuSlots) g.position.w = -1; // free slot
        for (const auto& [p, s] : slotOf) gpuSlots[s] = p->GetGPUPrimitive();
    }
    void Raymarcher::UploadPrimitiveSlots() {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, primitivesBuffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER, gpuSlots.size() * sizeof(GPUPrimitive), gpuSlots.data(), GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, primitivesBuffer);
    }
    void Raymarcher::UploadPrimitiveIndices(const std::vector<uint32_t>& indices) {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, primitiveIndexBuffer);
        GLuint dummy = 0;
        if (indices.empty()) glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(GLuint), &dummy, GL_DYNAMIC_DRAW); // never bind an empty SSBO
        else glBufferData(GL_SHADER_STORAGE_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, primitiveIndexBuffer);
        classifyShader->SetInt("primIndexCount", static_cast<int>(indices.size()));
    }

    bool Raymarcher::PrimitiveWorldAABB(const GPUPrimitive& p, glm::vec3& outMin, glm::vec3& outMax) const {
        // local bounds per type, read off the SDFs in classify.comp (before scaling)
        glm::vec3 d = glm::abs(glm::vec3(p.data));
        glm::vec3 lo, hi;
        switch (static_cast<int>(p.position.w)) {
            case Primitive::Sphere:   hi = glm::vec3(d.x); lo = -hi; break;
            case Primitive::Box:      hi = d; lo = -hi; break;
            case Primitive::Pyramid:  lo = glm::vec3(-d.x, 0.0f, -d.x); hi = glm::vec3(d.x, d.y, d.x); break; // data.x = base half size, data.y = height
            case Primitive::Capsule:  hi = glm::vec3(d.x, d.y + d.x, d.x); lo = -hi; break;
            case Primitive::Cylinder: hi = glm::vec3(d.x, d.y, d.x); lo = -hi; break;
            case Primitive::Torus:    hi = glm::vec3(d.x + d.y, d.y, d.x + d.y); lo = -hi; break;
            case Primitive::Cone:     hi = glm::vec3(d.x, d.y, d.x); lo = -hi; break; // conservative: height in both directions
            default: return false; // plane, terrain: unbounded
        }

        // classify maps world -> local as R * (p - pos) / scale, so local -> world is pos + R^T * (local * scale)
        glm::mat3 toWorld = glm::transpose(EulerToMat3(glm::vec3(p.rotation)));
        glm::vec3 scale = glm::vec3(p.scale);
        outMin = glm::vec3(1e30f);
        outMax = glm::vec3(-1e30f);
        for (int i = 0; i < 8; i++) {
            glm::vec3 corner((i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z);
            glm::vec3 w = glm::vec3(p.position) + toWorld * (corner * scale);
            outMin = glm::min(outMin, w);
            outMax = glm::max(outMax, w);
        }
        return true;
    }

    // ---------------------------------------------------------------------------------------------------------
    // dirty tracking
    // ---------------------------------------------------------------------------------------------------------
    void Raymarcher::Fill(GLuint r8Tex, glm::ivec3 origin, glm::ivec3 dim, float value) {
        fillShader->SetVec3("_Origin", glm::vec3(origin));
        fillShader->SetVec3("_Dim", glm::vec3(dim));
        fillShader->SetFloat("_Value", value);
        fillShader->Bind();
        fillShader->BindImage(0, r8Tex, GL_WRITE_ONLY, GL_R8);
        glm::ivec3 g = Groups(dim);
        fillShader->Dispatch(g.x, g.y, g.z);
    }
    void Raymarcher::StampDirty(glm::ivec3 brickMin, glm::ivec3 brickMax) {
        brickMin = glm::max(brickMin, glm::ivec3(0));
        brickMax = glm::min(brickMax, brickGridResolution - 1);
        if (glm::any(glm::lessThan(brickMax, brickMin))) return; // outside the volume

        Fill(dirtyTex, brickMin, brickMax - brickMin + 1, 1.0f);

        // mirror on the CPU, per chunk, so finding work never needs a readback
        glm::ivec3 c0 = brickMin / chunkBricks, c1 = brickMax / chunkBricks;
        for (int z = c0.z; z <= c1.z; z++)
        for (int y = c0.y; y <= c1.y; y++)
        for (int x = c0.x; x <= c1.x; x++) {
            glm::ivec3 c(x, y, z);
            glm::ivec3 lo = glm::max(brickMin, c * chunkBricks);
            glm::ivec3 hi = glm::min(brickMax, c * chunkBricks + chunkBricks - 1);
            BrickBox& d = chunkDirty[x + y * chunkGridDim.x + z * chunkGridDim.x * chunkGridDim.y];
            if (d.Empty()) { d.min = lo; d.max = hi; }
            else { d.min = glm::min(d.min, lo); d.max = glm::max(d.max, hi); }
        }
    }
    void Raymarcher::StampDirtyWorld(glm::vec3 min, glm::vec3 max) {
        // distances change up to the truncation distance (HALO) from the surface, and one more brick can change
        // activity through the neighbour dilation; +1 voxel for the apron
        glm::vec3 voxelSize = (worldMax - worldMin) / glm::vec3(resolution);
        glm::vec3 margin = voxelSize * static_cast<float>(HALO + BRICK_CORE + 1);
        glm::vec3 v0 = (min - margin - worldMin) / voxelSize;
        glm::vec3 v1 = (max + margin - worldMin) / voxelSize;
        StampDirty(glm::ivec3(glm::floor(v0 / static_cast<float>(BRICK_CORE))),
                   glm::ivec3(glm::floor(v1 / static_cast<float>(BRICK_CORE))));
    }

    // ---------------------------------------------------------------------------------------------------------
    // baking
    // ---------------------------------------------------------------------------------------------------------
    void Raymarcher::SetCommonClassifyUniforms() {
        classifyShader->SetVec3("_WorldMin", worldMin);
        classifyShader->SetVec3("_WorldMax", worldMax);
        classifyShader->SetVec3("_Resolution", resolution);
        classifyShader->SetInt("_Octaves", octaves);
        classifyShader->SetFloat("_WarpStrength", warpStrength);
        classifyShader->SetFloat("_Lacunarity", lacunarity);
        classifyShader->SetFloat("_Persistence", persistence);
        classifyShader->SetFloat("_HeightScale", heightScale);
        classifyShader->SetFloat("_TerrainScale", terrainScale);
        classifyShader->SetFloat("_TerraceCount", terraceCount);
        classifyShader->SetFloat("_CliffSharpness", cliffSharpness);
        classifyShader->SetFloat("_DetailAmount", detailAmount);
        classifyShader->SetFloat("_OverhangStrength", overhangStrength);
        classifyShader->SetFloat("_OverhangScale", overhangScale);
        classifyShader->SetFloat("_CliffRoughness", cliffRoughness);
        classifyShader->SetTexture2D("_Noise2D", noise2DTex);
        classifyShader->SetTexture3D("_Noise3D", noise3DTex);
        classifyShader->SetFloat("_Noise2DFrequency", noise2DFrequency);
        classifyShader->SetFloat("_Noise3DFrequency", noise3DFrequency);
        classifyShader->SetInt("BRICK_CORE", BRICK_CORE);
    }

    // Rebakes the dirty bricks inside one box (at most one chunk): classify the box plus a HALO of context,
    // jump-flood within it, give back the old pool slots, allocate new ones and write the pool.
    void Raymarcher::BakeChunkBox(const BrickBox& box) {
        const glm::vec3 voxelSize = (worldMax - worldMin) / glm::vec3(resolution);
        const float truncation = HALO * std::min({voxelSize.x, voxelSize.y, voxelSize.z});
        const glm::ivec3 boxDim = box.max - box.min + 1;

        // scratch region: the box's voxels (plus the apron) and HALO voxels around it, clipped to the volume
        glm::ivec3 vMin = glm::max(box.min * BRICK_CORE - HALO, glm::ivec3(0));
        glm::ivec3 vMax = glm::min((box.max + 1) * BRICK_CORE + HALO, resolution);
        glm::ivec3 dim = vMax - vMin;
        glm::ivec3 flagOrigin = box.min - 2;
        glm::ivec3 flagDim = boxDim + 4;

        // only primitives that can reach the scratch region matter here
        glm::vec3 regionMin = worldMin + glm::vec3(vMin) * voxelSize - voxelSize;
        glm::vec3 regionMax = worldMin + glm::vec3(vMax) * voxelSize + voxelSize;
        std::vector<uint32_t> indices;
        for (uint32_t s = 0; s < slots.size(); s++) {
            const PrimitiveSlot& slot = slots[s];
            if (!slot.used) continue;
            if (slot.unbounded || (glm::all(glm::lessThanEqual(slot.aabbMin, regionMax)) &&
                                   glm::all(glm::greaterThanEqual(slot.aabbMax, regionMin))))
                indices.push_back(s);
        }
        UploadPrimitiveIndices(indices);

        Fill(brickFlagsTex, glm::ivec3(0), glm::ivec3(chunkBricks + 4), 0.0f);

        // ---- classify ----
        SetCommonClassifyUniforms();
        classifyShader->SetInt("_CoarsePass", 0);
        classifyShader->SetVec3("_ChunkVoxelOrigin", glm::vec3(vMin));
        classifyShader->SetVec3("_ChunkDim", glm::vec3(dim));
        classifyShader->SetVec3("_FlagOrigin", glm::vec3(flagOrigin));
        classifyShader->SetVec3("_FlagDim", glm::vec3(flagDim));
        classifyShader->SetVec3("_CoreBrickMin", glm::vec3(box.min));
        classifyShader->SetVec3("_CoreBrickMax", glm::vec3(box.max));
        classifyShader->Bind();
        classifyShader->BindImage(0, seedTexA, GL_WRITE_ONLY, GL_RGBA16F);
        classifyShader->BindImage(1, signTex, GL_WRITE_ONLY, GL_R8);
        classifyShader->BindImage(2, brickFlagsTex, GL_WRITE_ONLY, GL_R8);
        classifyShader->BindImage(3, brickSignTex, GL_WRITE_ONLY, GL_R8);
        classifyShader->BindImage(4, idTex, GL_WRITE_ONLY, GL_R16UI);
        classifyShader->BindImage(5, distTex, GL_WRITE_ONLY, GL_R16F);
        glm::ivec3 g = Groups(dim);
        classifyShader->Dispatch(g.x, g.y, g.z);

        // ---- seed every sign change classify's |d| test missed (non-exact SDFs like terrain) ----
        seedFixShader->SetVec3("_ChunkDim", glm::vec3(dim));
        seedFixShader->SetVec3("_ChunkVoxelOrigin", glm::vec3(vMin));
        seedFixShader->SetVec3("_FlagOrigin", glm::vec3(flagOrigin));
        seedFixShader->SetVec3("_FlagDim", glm::vec3(flagDim));
        seedFixShader->SetInt("BRICK_CORE", BRICK_CORE);
        seedFixShader->Bind();
        seedFixShader->BindImage(0, seedTexA, GL_READ_WRITE, GL_RGBA16F);
        seedFixShader->BindImage(1, signTex, GL_READ_ONLY, GL_R8);
        seedFixShader->BindImage(2, distTex, GL_READ_ONLY, GL_R16F);
        seedFixShader->BindImage(3, brickFlagsTex, GL_WRITE_ONLY, GL_R8);
        seedFixShader->Dispatch(g.x, g.y, g.z);

        // ---- JFA: seeds further than HALO are never trusted, so the steps start at HALO / 2 ----
        bool pingIsA = true;
        auto jfaPass = [&](int step) {
            GLuint src = pingIsA ? seedTexA : seedTexB;
            GLuint dst = pingIsA ? seedTexB : seedTexA;
            jfaStepShader->SetInt("_Step", step);
            jfaStepShader->SetVec3("_Resolution", glm::vec3(dim)); // bounds of the scratch region
            jfaStepShader->Bind();
            jfaStepShader->BindImage(0, src, GL_READ_ONLY, GL_RGBA16F);
            jfaStepShader->BindImage(1, dst, GL_WRITE_ONLY, GL_RGBA16F);
            jfaStepShader->Dispatch(g.x, g.y, g.z);
            pingIsA = !pingIsA;
        };
        jfaPass(1); // 1+JFA extra pass
        for (int step = HALO / 2; step >= 1; step /= 2) jfaPass(step);
        finalSeedTex = pingIsA ? seedTexA : seedTexB;

        glm::ivec3 bg = Groups(boxDim);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, freeListBuffer);

        // ---- release: dirty bricks give their old pool slot back ----
        releaseShader->SetVec3("_BoxMin", glm::vec3(box.min));
        releaseShader->SetVec3("_BoxDim", glm::vec3(boxDim));
        releaseShader->SetVec3("_PoolGridDim", glm::vec3(brickPoolDim));
        releaseShader->SetInt("BRICK_STORE", BRICK_STORE);
        releaseShader->Bind();
        releaseShader->BindImage(0, dirtyTex, GL_READ_ONLY, GL_R8);
        releaseShader->BindImage(1, lookupTex, GL_READ_ONLY, GL_RGBA32F);
        releaseShader->Dispatch(bg.x, bg.y, bg.z);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT); // pushes must land before the pops

        // ---- allocate ----
        allocateShader->SetVec3("_BoxMin", glm::vec3(box.min));
        allocateShader->SetVec3("_BoxDim", glm::vec3(boxDim));
        allocateShader->SetVec3("_FlagOrigin", glm::vec3(flagOrigin));
        allocateShader->SetVec3("_PoolGridDim", glm::vec3(brickPoolDim));
        allocateShader->SetInt("BRICK_STORE", BRICK_STORE);
        allocateShader->Bind();
        allocateShader->BindImage(0, brickFlagsTex, GL_READ_ONLY, GL_R8);
        allocateShader->BindImage(1, lookupTex, GL_WRITE_ONLY, GL_RGBA32F);
        allocateShader->BindImage(2, dirtyTex, GL_READ_ONLY, GL_R8);
        allocateShader->BindImage(3, brickSignTex, GL_READ_ONLY, GL_R8);
        allocateShader->Dispatch(bg.x, bg.y, bg.z);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        // ---- finalize: one invocation per pool texel of the box's bricks ----
        jfaFinalizeShader->SetVec3("_WorldMin", worldMin);
        jfaFinalizeShader->SetVec3("_WorldMax", worldMax);
        jfaFinalizeShader->SetVec3("_Resolution", resolution);
        jfaFinalizeShader->SetVec3("_BoxMin", glm::vec3(box.min));
        jfaFinalizeShader->SetVec3("_BoxDim", glm::vec3(boxDim));
        jfaFinalizeShader->SetVec3("_ChunkVoxelOrigin", glm::vec3(vMin));
        jfaFinalizeShader->SetFloat("_Truncation", truncation);
        jfaFinalizeShader->SetInt("BRICK_CORE", BRICK_CORE);
        jfaFinalizeShader->SetInt("BRICK_STORE", BRICK_STORE);
        jfaFinalizeShader->Bind();
        jfaFinalizeShader->BindImage(0, finalSeedTex, GL_READ_ONLY, GL_RGBA16F);
        jfaFinalizeShader->BindImage(1, signTex, GL_READ_ONLY, GL_R8);
        jfaFinalizeShader->BindImage(2, brickPoolTex, GL_WRITE_ONLY, GL_R16F);
        jfaFinalizeShader->BindImage(3, lookupTex, GL_READ_ONLY, GL_RGBA32F);
        jfaFinalizeShader->BindImage(4, idTex, GL_READ_ONLY, GL_R16UI);
        jfaFinalizeShader->BindImage(5, brickIdTex, GL_WRITE_ONLY, GL_R16UI);
        jfaFinalizeShader->BindImage(6, dirtyTex, GL_READ_ONLY, GL_R8);
        glm::ivec3 fg = Groups(boxDim * BRICK_STORE);
        jfaFinalizeShader->Dispatch(fg.x, fg.y, fg.z);

        // these bricks are done
        Fill(dirtyTex, box.min, boxDim, 0.0f);
        glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    }

    int Raymarcher::ProcessDirtyChunks(int maxChunks, glm::vec3 cameraPos) {
        std::vector<int> work;
        for (int i = 0; i < static_cast<int>(chunkDirty.size()); i++)
            if (!chunkDirty[i].Empty()) work.push_back(i);

        if (maxChunks > 0 && static_cast<int>(work.size()) > maxChunks) {
            // nearest chunks first; the rest keep their marks (GPU bits and CPU boxes) for later frames
            glm::vec3 voxelSize = (worldMax - worldMin) / glm::vec3(resolution);
            auto distance2 = [&](int i) {
                glm::vec3 centerVoxel = glm::vec3(chunkDirty[i].min + chunkDirty[i].max + 1) * (0.5f * BRICK_CORE);
                glm::vec3 d = worldMin + centerVoxel * voxelSize - cameraPos;
                return glm::dot(d, d);
            };
            std::partial_sort(work.begin(), work.begin() + maxChunks, work.end(),
                              [&](int a, int b) { return distance2(a) < distance2(b); });
            work.resize(maxChunks);
        }

        for (int i : work) {
            BakeChunkBox(chunkDirty[i]);
            chunkDirty[i] = BrickBox{};
        }

        pendingDirtyChunks = 0;
        for (const auto& box : chunkDirty) if (!box.Empty()) pendingDirtyChunks++;
        lastChunksProcessed = static_cast<int>(work.size());
        if (!work.empty()) ReadBackFreeList();
        return lastChunksProcessed;
    }

    void Raymarcher::ReadBackFreeList() {
        GLint header[2] = {0, 0};
        glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, freeListBuffer);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(header), header);
        poolSlotsUsed = GetPoolCapacity() - header[0];
        droppedBricks = header[1];
    }

    void Raymarcher::Bake(const std::vector<Primitive*>& primitives) {
        if (!m_queryInFlight) {
            glQueryCounter(m_timeQuery[0], GL_TIMESTAMP);
        }

        // everything is rebuilt, so the current state of every primitive becomes the baked state
        SyncPrimitiveSlots(primitives);
        for (const auto& [p, s] : slotOf) {
            PrimitiveSlot& slot = slots[s];
            slot.bakedGpu = gpuSlots[s];
            slot.unbounded = !PrimitiveWorldAABB(slot.bakedGpu, slot.aabbMin, slot.aabbMax);
            slot.baked = true;
        }
        UploadPrimitiveSlots();

        ResetFreeList();
        Fill(dirtyTex, glm::ivec3(0), brickGridResolution, 1.0f);
        std::vector<GLuint> zeros(chunkDirty.size(), 0);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunkFlagsBuffer);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, zeros.size() * sizeof(GLuint), zeros.data());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, chunkFlagsBuffer);

        // ---- coarse pass: flag the chunks that may hold a surface, then settle the bricks of all other chunks ----
        const glm::vec3 voxelSize = (worldMax - worldMin) / glm::vec3(resolution);
        std::vector<uint32_t> all;
        for (const auto& [p, s] : slotOf) all.push_back(s);
        UploadPrimitiveIndices(all);

        SetCommonClassifyUniforms();
        classifyShader->SetInt("_CoarsePass", 1);
        classifyShader->SetVec3("_GridResolution", glm::vec3(brickGridResolution));
        classifyShader->SetInt("_ChunkBricks", chunkBricks);
        classifyShader->SetVec3("_ChunkGridDim", glm::vec3(chunkGridDim));
        classifyShader->SetFloat("_CoarseMargin", coarseMargin);
        classifyShader->SetFloat("_Truncation", HALO * std::min({voxelSize.x, voxelSize.y, voxelSize.z}));
        classifyShader->Bind();
        classifyShader->BindImage(3, brickSignTex, GL_WRITE_ONLY, GL_R8);
        glm::ivec3 g = Groups(brickGridResolution);
        classifyShader->Dispatch(g.x, g.y, g.z);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        settleShader->SetVec3("_GridResolution", glm::vec3(brickGridResolution));
        settleShader->SetInt("_ChunkBricks", chunkBricks);
        settleShader->SetVec3("_ChunkGridDim", glm::vec3(chunkGridDim));
        settleShader->Bind();
        settleShader->BindImage(0, brickSignTex, GL_READ_ONLY, GL_R8);
        settleShader->BindImage(1, lookupTex, GL_WRITE_ONLY, GL_RGBA32F);
        settleShader->BindImage(2, dirtyTex, GL_WRITE_ONLY, GL_R8);
        settleShader->Dispatch(g.x, g.y, g.z);
        classifyShader->SetInt("_CoarsePass", 0);
        glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);

        std::vector<GLuint> chunkFlags(chunkDirty.size(), 0);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, chunkFlagsBuffer);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, chunkFlags.size() * sizeof(GLuint), chunkFlags.data());

        for (int z = 0; z < chunkGridDim.z; z++)
        for (int y = 0; y < chunkGridDim.y; y++)
        for (int x = 0; x < chunkGridDim.x; x++) {
            int i = x + y * chunkGridDim.x + z * chunkGridDim.x * chunkGridDim.y;
            BrickBox box;
            if (chunkFlags[i]) {
                box.min = glm::ivec3(x, y, z) * chunkBricks;
                box.max = glm::min(box.min + chunkBricks - 1, brickGridResolution - 1);
            }
            chunkDirty[i] = box;
        }

        // ---- chunk pipeline for every flagged chunk, no per-frame limit ----
        ProcessDirtyChunks(0, glm::vec3(0));
        ReadBackFreeList();

        GLint totalMemKb = 0, availMemKb = 0;
        glGetIntegerv(GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX, &totalMemKb);
        glGetIntegerv(GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_MEMORY_NVX, &availMemKb);
        double usedMiB = (totalMemKb - availMemKb) / 1024.0;
        editor::editorWindows::BenchmarkTool::PushSample("VRAM used during bake (MiB)", usedMiB);

        dirty = false;
        fullRebakeRequested = false;

        if (!m_queryInFlight) {
            glQueryCounter(m_timeQuery[1], GL_TIMESTAMP);
            m_queryInFlight = true;
        }
    }

    void Raymarcher::UpdateDynamic(const std::vector<Primitive*>& primitives, glm::vec3 cameraPos) {
        SyncPrimitiveSlots(primitives); // also stamps the old bounds of removed primitives

        for (const auto& [p, s] : slotOf) {
            PrimitiveSlot& slot = slots[s];
            const GPUPrimitive& current = gpuSlots[s];
            if (slot.baked && !GeometryDiffers(current, slot.bakedGpu)) continue; // unchanged (color doesn't need a rebake)

            if (!autoRebake) { dirty = true; continue; } // leave the baked state alone, "Rebake Now" does a full bake

            glm::vec3 newMin, newMax;
            bool newBounded = PrimitiveWorldAABB(current, newMin, newMax);
            if (!newBounded || (slot.baked && slot.unbounded)) {
                fullRebakeRequested = true;
            } else {
                if (slot.baked) StampDirtyWorld(slot.aabbMin, slot.aabbMax); // where it was
                StampDirtyWorld(newMin, newMax);                             // where it is now
            }

            slot.bakedGpu = current;
            slot.aabbMin = newMin;
            slot.aabbMax = newMax;
            slot.unbounded = !newBounded;
            slot.baked = true;
        }

        UploadPrimitiveSlots();

        if (!autoRebake) return;
        if (dirty || fullRebakeRequested) {
            Bake(primitives);
            return;
        }

        bool anyPending = false;
        for (const auto& box : chunkDirty) if (!box.Empty()) { anyPending = true; break; }
        if (!anyPending) { lastChunksProcessed = 0; return; }

        bool timed = !m_partialQueryInFlight;
        if (timed) glQueryCounter(m_partialQuery[0], GL_TIMESTAMP);
        ProcessDirtyChunks(maxRebakeChunksPerFrame, cameraPos);
        if (timed) {
            glQueryCounter(m_partialQuery[1], GL_TIMESTAMP);
            m_partialQueryInFlight = true;
        }
    }



    // ---------------------------------------------------------------------------------------------------------
    // noise
    // ---------------------------------------------------------------------------------------------------------
    void Raymarcher::EnsureNoiseResourcesSized() {
        DestroyNoiseResources();

        // 2D noise
        glGenTextures(1, &noise2DTex);
        glBindTexture(GL_TEXTURE_2D, noise2DTex);

        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_R16F,
            noise2DResolution.x,
            noise2DResolution.y,
            0,
            GL_RED,
            GL_HALF_FLOAT,
            nullptr
        );

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);


        // 3D noise
        glGenTextures(1, &noise3DTex);
        glBindTexture(GL_TEXTURE_3D, noise3DTex);

        glTexImage3D(
            GL_TEXTURE_3D,
            0,
            GL_R16F,
            noise3DResolution.x,
            noise3DResolution.y,
            noise3DResolution.z,
            0,
            GL_RED,
            GL_HALF_FLOAT,
            nullptr
        );

        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);

        glBindTexture(GL_TEXTURE_2D, 0);
        glBindTexture(GL_TEXTURE_3D, 0);
    }
    void Raymarcher::DestroyNoiseResources()
    {
        if (noise2DTex) glDeleteTextures(1, &noise2DTex);
        if (noise3DTex) glDeleteTextures(1, &noise3DTex);
    }
    void Raymarcher::BakeNoise() {
        EnsureNoiseResourcesSized();

        // 2D noise
        noise2DShader->SetVec3("_Resolution", glm::vec3(noise2DResolution, 0));
        noise2DShader->SetInt("_Seed", noiseSeed);

        noise2DShader->Bind();

        noise2DShader->BindImage(
            0,
            noise2DTex,
            GL_WRITE_ONLY,
            GL_R16F
        );

        const int noise2DGX = (noise2DResolution.x + 7) / 8;
        const int noise2DGY = (noise2DResolution.y + 7) / 8;

        noise2DShader->Dispatch(noise2DGX,noise2DGY,1);

        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

        // 3D noise
        noise3DShader->SetVec3("_Resolution", glm::vec3(noise3DResolution));
        noise3DShader->SetInt("_Seed", noiseSeed);

        noise3DShader->Bind();

        noise3DShader->BindImage(
            0,
            noise3DTex,
            GL_WRITE_ONLY,
            GL_R16F
        );

        const int noise3DGX = (noise3DResolution.x + 7) / 8;
        const int noise3DGY = (noise3DResolution.y + 7) / 8;
        const int noise3DGZ = (noise3DResolution.z + 7) / 8;

        noise3DShader->Dispatch(noise3DGX,noise3DGY,noise3DGZ);

        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    }

    // ---------------------------------------------------------------------------------------------------------
    // rendering
    // ---------------------------------------------------------------------------------------------------------
    void Raymarcher::Render(GLuint targetFbo, GLuint sceneColorTex, GLuint sceneDepthTex, Camera* cam) {
        if (m_queryInFlight) {
            GLint available = 0;
            glGetQueryObjectiv(m_timeQuery[1], GL_QUERY_RESULT_AVAILABLE, &available);
            if (available) {
                GLuint64 startNs = 0, endNs = 0;
                glGetQueryObjectui64v(m_timeQuery[0], GL_QUERY_RESULT, &startNs);
                glGetQueryObjectui64v(m_timeQuery[1], GL_QUERY_RESULT, &endNs);
                GLuint64 elapsedNs = endNs - startNs;
                editor::editorWindows::BenchmarkTool::PushSample("Bake time",
                    static_cast<double>(elapsedNs) * 1e-6);
                m_queryInFlight = false;
            }
        }
        if (m_partialQueryInFlight) {
            GLint available = 0;
            glGetQueryObjectiv(m_partialQuery[1], GL_QUERY_RESULT_AVAILABLE, &available);
            if (available) {
                GLuint64 startNs = 0, endNs = 0;
                glGetQueryObjectui64v(m_partialQuery[0], GL_QUERY_RESULT, &startNs);
                glGetQueryObjectui64v(m_partialQuery[1], GL_QUERY_RESULT, &endNs);
                GLuint64 elapsedNs = endNs - startNs;
                editor::editorWindows::BenchmarkTool::PushSample("Partial rebake (ms)",
                    static_cast<double>(elapsedNs) * 1e-6);
                m_partialQueryInFlight = false;
            }
        }

        if (debug3D) {
            GLuint dTex = 0;
            if (debugTex3D == dSignTex) dTex = signTex;
            if (debugTex3D == dSeedTex) dTex = finalSeedTex;
            if (debugTex3D == dNoise3DTex) dTex = noise3DTex;
            if (debugTex3D == dBrickFlagsTex) dTex = brickFlagsTex;
            if (debugTex3D == dLookupTex) dTex = lookupTex;
            if (debugTex3D == dDirtyTex) dTex = dirtyTex;
            if (debugTex3D == dBrickPoolTex) dTex = brickPoolTex;
            RenderDebugSlice(targetFbo, dTex);
            return;
        }

        if (debug2D) {
            GLuint dTex = 0;
            if (debugTex2D == dNoise2DTex) dTex = noise2DTex;
            RenderDebugTexture(targetFbo, dTex);
            return;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        marchMaterial->SetTexture2D("_MainTex", sceneColorTex);
        marchMaterial->SetTexture2D("_SceneDepth", sceneDepthTex);
        marchMaterial->SetTexture3D("_LookupTex", lookupTex);
        marchMaterial->SetTexture3D("_BrickPoolTex", brickPoolTex);
        marchMaterial->SetTexture3D("_BrickIdTex", brickIdTex);

        marchMaterial->SetVec3("_WorldMin", worldMin);
        marchMaterial->SetVec3("_WorldMax", worldMax);

        marchMaterial->SetVec3("_Resolution", resolution);
        marchMaterial->SetVec3("_GridResolution", brickGridResolution);
        marchMaterial->SetVec3("_PoolResolution", glm::vec3(brickPoolDim * BRICK_STORE));
        marchMaterial->SetInt("BRICK_CORE", BRICK_CORE);
        marchMaterial->SetInt("BRICK_STORE", BRICK_STORE);

        marchMaterial->SetMat4("_InvView", glm::inverse(cam->GetView()));
        marchMaterial->SetMat4("_InvProj", glm::inverse(cam->GetProjection()));
        marchMaterial->SetVec3("_CameraPos", cam->gameObject->transform.position);
        marchMaterial->SetFloat("_Far", cam->farPlane);

        marchMaterial->SetInt("_MaxSteps", maxSteps);
        marchMaterial->SetFloat("_MaxDist", maxDist);
        marchMaterial->SetFloat("_SurfDist", surfDist);
        marchMaterial->SetFloat("_ColorBlend", colorBlend);

        // the baked ids index the primitive slots; UpdateDynamic re-uploads them every frame so color edits show without a rebake
        marchMaterial->SetInt("primitiveCount", static_cast<int>(gpuSlots.size()));
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, primitivesBuffer);

        marchMaterial->Bind();
        quadModel->render();

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        GLint totalMemKb = 0, availMemKb = 0;
        glGetIntegerv(GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX, &totalMemKb);
        glGetIntegerv(GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_MEMORY_NVX, &availMemKb);
        double usedMiB = (totalMemKb - availMemKb) / 1024.0;
        editor::editorWindows::BenchmarkTool::PushSample("VRAM used during render (MiB)", usedMiB);
    }
    void Raymarcher::RenderDebugSlice(GLuint targetFbo, GLuint textureToView) {
        glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDisable(GL_DEPTH_TEST);

        debugSliceMaterial->SetTexture3D("_Volume", textureToView);
        debugSliceMaterial->SetFloat("_SliceZ", static_cast<float>(sliceZ) / static_cast<float>(resolution.z));
        debugSliceMaterial->SetInt("_Mode", mode);
        debugSliceMaterial->SetFloat("_DisplayScale", displayScale);
        debugSliceMaterial->Bind();
        quadModel->render();

        glEnable(GL_DEPTH_TEST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    void Raymarcher::RenderDebugTexture(GLuint targetFbo, GLuint textureToView) {
        glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDisable(GL_DEPTH_TEST);

        debugTextureMaterial->SetTexture2D("_Texture", textureToView);
        debugTextureMaterial->SetInt("_Mode", mode);
        debugTextureMaterial->SetFloat("_DisplayScale", displayScale);
        debugTextureMaterial->Bind();
        quadModel->render();

        glEnable(GL_DEPTH_TEST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void Raymarcher::SetWorldBounds(glm::vec3 min, glm::vec3 max) {
        worldMin = min;
        worldMax = max;
        MarkDirty();
    }
} // core