//
// Adapters that expose each raymarcher version through RaymarchBackend.
//

#include "RaymarchBackend.h"

#include <glm/gtc/type_ptr.hpp>

#include "../Raymarcher.h"
#include "../Assets/Material.h"
#include "../Assets/model.h"
#include "../Components/Camera.h"
#include "../Components/Primitive.h"
#include "../../editor/Editor.h"
#include "v1/Raymarcher.h"
#include "v2/Raymarcher.h"
#include "v4/Raymarcher.h"
#include "v5/Raymarcher.h"
#include "v6/Raymarcher.h"

namespace core {
    namespace {
        // ---- v0: the "Ray-marching" post-process effect from 0bdbf15 ----
        // At that commit the effect was a PostProcessPass: the Renderer set the camera/texture uniforms below,
        // and Material::Bind uploaded the primitives with this function (copied from 0bdbf15 Material.cpp,
        // only halfExtents -> data, the field's later name).
        void V0UploadPrimitives(GLuint shader, const std::vector<Primitive*>& primitives) {
            glUseProgram(shader);

            for (int i = 0; i < primitives.size(); i++) {
                const Primitive* primitive = primitives[i];

                std::string base = "primitives[" + std::to_string(i) + "].";

                glUniform1i(glGetUniformLocation(shader, (base + "type").c_str()),
                    primitive->type);

                glUniform3fv(glGetUniformLocation(shader, (base + "position").c_str()),
                    1, glm::value_ptr(primitive->gameObject->transform.position));

                glUniform3fv(glGetUniformLocation(shader, (base + "rotation").c_str()),
                    1, glm::value_ptr(primitive->gameObject->transform.rotation));

                glUniform3fv(glGetUniformLocation(shader, (base + "scale").c_str()),
                    1, glm::value_ptr(primitive->gameObject->transform.scale));

                glUniform3fv(glGetUniformLocation(shader, (base + "halfExtents").c_str()),
                    1, glm::value_ptr(primitive->data));

                glUniform3fv(glGetUniformLocation(shader, (base + "color").c_str()),
                    1, glm::value_ptr(primitive->color));
            }

            GLint loc = glGetUniformLocation(shader, "primitiveCount");
            if (loc == -1) return;
            glUniform1i(loc, primitives.size());
        }

        class V0Direct : public RaymarchBackend {
            Material* material;
            Model* quadModel;
            // defaults of the effect's PostProcessingParameters at 0bdbf15
            int maxSteps = 96;
            float maxDist = 200.0f;
            float surfDist = 0.001f;

        public:
            V0Direct() {
                material = new Material(
                    "Assets/shaders/PostProcessing/viewSpace.vert",
                    "Assets/shaders/versions/v0-direct/ray_marching.frag");

                std::vector<Mesh> meshes = std::vector<Mesh>();
                meshes.push_back(Mesh::generateQuad());
                quadModel = new Model(std::move(meshes));
            }
            ~V0Direct() override {
                delete material;
                delete quadModel;
            }

            const char* Id() const override { return "v0-direct"; }
            bool HasBake() const override { return false; }

            void SetMarchParams(int steps, float dist, float surf) override {
                maxSteps = steps;
                maxDist = dist;
                surfDist = surf;
            }

            void Render(GLuint targetFbo, GLuint sceneColorTex, GLuint sceneDepthTex, Camera* cam) override {
                glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

                material->SetTexture2D("_MainTex", sceneColorTex);
                material->SetTexture2D("_SceneDepth", sceneDepthTex);

                material->SetMat4("_InvView", glm::inverse(cam->GetView()));
                material->SetMat4("_InvProj", glm::inverse(cam->GetProjection()));
                material->SetFloat("_Near", cam->nearPlane);
                material->SetFloat("_Far", cam->farPlane);

                material->SetInt("_MaxSteps", maxSteps);
                material->SetFloat("_MaxDist", maxDist);
                material->SetFloat("_SurfDist", surfDist);

                material->Bind(); // also sets cameraPos, as it did at 0bdbf15
                V0UploadPrimitives(material->GetShaderProgram(), editor::Editor::activeScene->primitives);
                quadModel->render();

                glBindFramebuffer(GL_FRAMEBUFFER, 0);
            }
        };

        // ---- v1 to v7: thin forwarding adapters ----
        template <typename T>
        class VolumeAdapter : public RaymarchBackend {
        protected:
            T* raymarcher;

        public:
            template <typename... Args>
            explicit VolumeAdapter(Args... args) : raymarcher(new T(args...)) {}
            ~VolumeAdapter() override { delete raymarcher; }

            void SetVolume(glm::ivec3 resolution, glm::vec3 worldMin, glm::vec3 worldMax) override {
                raymarcher->SetWorldBounds(worldMin, worldMax);
                raymarcher->EnsureVolumeSized(resolution);
                raymarcher->MarkDirty();
            }
            void SetMarchParams(int steps, float dist, float surf) override {
                raymarcher->maxSteps = steps;
                raymarcher->maxDist = dist;
                raymarcher->surfDist = surf;
            }
            void Bake(const std::vector<Primitive*>& primitives) override {
                raymarcher->Bake(primitives);
            }
            void Render(GLuint targetFbo, GLuint sceneColorTex, GLuint sceneDepthTex, Camera* cam) override {
                raymarcher->Render(targetFbo, sceneColorTex, sceneDepthTex, cam);
            }
        };

        class V1DenseRaster : public VolumeAdapter<v1::Raymarcher> {
        public:
            const char* Id() const override { return "v1-dense-raster"; }
        };

        class V2DenseJfa : public VolumeAdapter<v2::Raymarcher> {
        public:
            const char* Id() const override { return "v2-dense-jfa"; }
            bool SupportsTerrain() const override { return true; }
            void PrepareBake() override { raymarcher->BakeNoise(); }
        };

        // v3 and v4 share one bake and brick pool (v4::Raymarcher); they differ only in their march shader
        class V3Sparse : public VolumeAdapter<v4::Raymarcher> {
        public:
            V3Sparse() : VolumeAdapter("Assets/shaders/versions/v3-sparse/march.frag") {}
            const char* Id() const override { return "v3-sparse"; }
            bool SupportsTerrain() const override { return true; }
            void PrepareBake() override { raymarcher->BakeNoise(); }
        };

        class V4Analytic : public VolumeAdapter<v4::Raymarcher> {
        public:
            const char* Id() const override { return "v4-analytic"; }
            bool SupportsTerrain() const override { return true; }
            void PrepareBake() override { raymarcher->BakeNoise(); }
        };

        // v3's sparse cache plus a per-texel primitive id in the brick pool, used to color hits
        class V5SparseColor : public VolumeAdapter<v5::Raymarcher> {
        public:
            const char* Id() const override { return "v5-sparse-color"; }
            bool SupportsTerrain() const override { return true; }
            void PrepareBake() override { raymarcher->BakeNoise(); }
        };

        // chunked sparse bake that rebakes only the bricks around changed primitives, every frame
        class V6DynamicSparse : public VolumeAdapter<v6::Raymarcher> {
        public:
            const char* Id() const override { return "v6-dynamic-sparse"; }
            bool SupportsTerrain() const override { return true; }
            void PrepareBake() override { raymarcher->BakeNoise(); }
            void Update(const std::vector<Primitive*>& primitives, glm::vec3 cameraPos) override {
                raymarcher->UpdateDynamic(primitives, cameraPos);
            }
        };

        // the current Raymarcher: v6 plus per-primitive materials (textures, surface shaders) and the scene's lights
        class V7Materials : public VolumeAdapter<Raymarcher> {
        public:
            const char* Id() const override { return "v7-materials"; }
            bool SupportsTerrain() const override { return true; }
            void PrepareBake() override { raymarcher->BakeNoise(); }
            void Update(const std::vector<Primitive*>& primitives, glm::vec3 cameraPos) override {
                raymarcher->UpdateDynamic(primitives, cameraPos); // also loads materials and rebuilds the march shader
            }
            Raymarcher* GetCurrentRaymarcher() override { return raymarcher; }
        };
    }

    const std::vector<RaymarchVersionInfo>& GetRaymarchVersions() {
        static const std::vector<RaymarchVersionInfo> versions = {
            {"v0-direct", "No cache: evaluates every primitive per pixel per step", "0bdbf15", false,
                [] { return std::unique_ptr<RaymarchBackend>(new V0Direct()); }},
            {"v1-dense-raster", "Dense 3D texture baked slice by slice in a fragment shader", "20aca78", false,
                [] { return std::unique_ptr<RaymarchBackend>(new V1DenseRaster()); }},
            {"v2-dense-jfa", "Dense 3D texture baked with jump flooding in compute shaders", "ef5bc68", true,
                [] { return std::unique_ptr<RaymarchBackend>(new V2DenseJfa()); }},
            {"v3-sparse", "Sparse brick pool, sphere tracing inside bricks", "ea5db90", true,
                [] { return std::unique_ptr<RaymarchBackend>(new V3Sparse()); }},
            {"v4-analytic", "Sparse brick pool, analytic cubic intersection per voxel cell", "db80f3d", true,
                [] { return std::unique_ptr<RaymarchBackend>(new V4Analytic()); }},
            {"v5-sparse-color", "v3-sparse plus a primitive-id color lookup in the brick pool", "8b11928", true,
                [] { return std::unique_ptr<RaymarchBackend>(new V5SparseColor()); }},
            {"v6-dynamic-sparse", "Chunked sparse bake with per-frame dynamic re-baking of changed primitives", "3ad342e", true,
                [] { return std::unique_ptr<RaymarchBackend>(new V6DynamicSparse()); }},
            {"v7-materials", "v6 plus per-primitive materials (textures, surface shaders) lit by the scene's lights (current)", "current", true,
                [] { return std::unique_ptr<RaymarchBackend>(new V7Materials()); }},
        };
        return versions;
    }

    const RaymarchVersionInfo* FindRaymarchVersion(const std::string& id) {
        for (const auto& v : GetRaymarchVersions())
            if (v.id == id) return &v;
        return nullptr;
    }
} // core
