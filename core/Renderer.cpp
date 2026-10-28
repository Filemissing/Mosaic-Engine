//
// Created by micha on 02/12/2025.
//

#include "Renderer.h"

#include <iostream>

#include "../editor/Editor.h"
#include "../editor/EditorWindows/BenchmarkTool.h"

namespace core {
    Renderer::Renderer() {
        glGenQueries(1, &m_timeQuery);

        // auto* blur_horizontal = new PostProcessPass(new Material(
        //                 "Assets/shaders/PostProcessing/viewSpace.vert",
        //                 "Assets/shaders/PostProcessing/blur_horizontal.frag"));
        // auto* blur_vertical = new PostProcessPass(new Material(
        //                 "Assets/shaders/PostProcessing/viewSpace.vert",
        //                 "Assets/shaders/PostProcessing/blur_vertical.frag"));
        // postProcessingEffects.push_back(
        //     new PostProcessEffect("Bloom",
        //         {
        //             new PostProcessPass(new Material(
        //                 "Assets/shaders/PostProcessing/viewSpace.vert",
        //                 "Assets/shaders/PostProcessing/bloom_threshold.frag"),
        //                 {PostProcessingParameter("_Threshold", typeid(float), 0.5f)}),
        //             blur_horizontal,
        //             blur_vertical,
        //             blur_horizontal,
        //             blur_vertical,
        //             blur_horizontal,
        //             blur_vertical,
        //             blur_horizontal,
        //             blur_vertical,
        //             blur_horizontal,
        //             blur_vertical,
        //             blur_horizontal,
        //             blur_vertical,
        //             blur_horizontal,
        //             blur_vertical,
        //             blur_horizontal,
        //             blur_vertical,
        //             blur_horizontal,
        //             blur_vertical,
        //             blur_horizontal,
        //             blur_vertical,
        //             blur_horizontal,
        //             blur_vertical,
        //             blur_horizontal,
        //             blur_vertical,
        //             blur_horizontal,
        //             blur_vertical,
        //             blur_horizontal,
        //             blur_vertical,
        //             blur_horizontal,
        //             blur_vertical,
        //             blur_horizontal,
        //             blur_vertical,
        //             new PostProcessPass(new Material(
        //                 "Assets/shaders/PostProcessing/viewSpace.vert",
        //                 "Assets/shaders/PostProcessing/bloom_combine.frag"),
        //                 {PostProcessingParameter("_Intensity", typeid(float), 1.0f)})
        //         }
        //     )
        // );

        // postProcessingEffects.push_back(
        //     new PostProcessEffect("Hue Shift",
        //         {
        //             new PostProcessPass(
        //                 new Material(
        //                     "Assets/shaders/PostProcessing/viewSpace.vert",
        //                     "Assets/shaders/PostProcessing/Hue shift.frag"),
        //                     {PostProcessingParameter("_Degrees", typeid(float), 0.0f)}
        //             )
        //         }
        //     )
        // );

        // postProcessingEffects.push_back(
        //     new PostProcessEffect("Invert",
        //         {
        //             new PostProcessPass(
        //                 new Material(
        //                     "Assets/shaders/PostProcessing/viewSpace.vert",
        //                     "Assets/shaders/PostProcessing/invert.frag"
        //                 )
        //             )
        //         }
        //     )
        // );

        // postProcessingEffects.push_back(
        //     new PostProcessEffect("Test",
        //         {
        //             new PostProcessPass(
        //                 new Material(
        //                     "Assets/shaders/PostProcecssion/viewspace.vert",
        //                     "Assets/shaders/raymarching/bake.frag"
        //                 )
        //             )
        //         }
        //     )
        // );

        std::vector<Mesh> meshes = std::vector<Mesh>();
        meshes.push_back(Mesh::generateQuad());
        quadModel = new Model(std::move(meshes));

        ppFbo[0] = ppFbo[1] = 0;
        ppTex[0] = ppTex[1] = 0;
        ppDepth[0] = ppDepth[1] = 0;

        SetRaymarchVersion("v7-materials");
    }
    Renderer::~Renderer() {
        DestroyFbo();
        m_backend.reset();
        for (auto& pass : postProcessingEffects) {
            delete pass;
        }
    }

    bool Renderer::SetRaymarchVersion(const std::string& id) {
        const RaymarchVersionInfo* info = id.empty() ? nullptr : FindRaymarchVersion(id);
        if (!id.empty() && !info) {
            std::cerr << "[Renderer] Unknown raymarcher version: " << id << std::endl;
            return false;
        }

        // destroy the previous version before creating the next, so their GPU memory never overlaps
        m_backend.reset();
        m_backendId.clear();
        glFinish();

        if (info) {
            m_backend = info->create();
            m_backendId = info->id;
        }
        return true;
    }

    void Renderer::RenderScene(Scene*& scene,const GLuint& finalFbo, const GLuint& finalTexture, int width, int height) {
        renderedFrames++;

        if (m_queryInFlight) {
            GLint available = 0;
            glGetQueryObjectiv(m_timeQuery, GL_QUERY_RESULT_AVAILABLE, &available);
            if (available) {
                GLuint64 elapsedNs = 0;
                glGetQueryObjectui64v(m_timeQuery, GL_QUERY_RESULT, &elapsedNs);
                editor::editorWindows::BenchmarkTool::PushSample("Raymarch Frame",
                    static_cast<double>(elapsedNs) * 1e-6);
                m_queryInFlight = false;
            }
        }

        // per-frame work of versions that rebake changed primitives; outside both the frame query and the
        // raymarch timer, so those stay render-only
        if (m_backend && scene && Camera::GetMainCamera())
            m_backend->Update(scene->primitives, Camera::GetMainCamera()->gameObject->transform.position);

        if (!m_queryInFlight) {
            glBeginQuery(GL_TIME_ELAPSED, m_timeQuery);
        }

        // 1. Render scene into pingFbo[0]
        glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneColor, 0);
        glViewport(0, 0, width, height);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        editor::Editor::activeScene->Render();

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // blit scene to ppFbo[0]
        glBindFramebuffer(GL_READ_FRAMEBUFFER, sceneFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ppFbo[0]);

        glBlitFramebuffer(
            0, 0, width, height,
            0, 0, width, height,
            GL_COLOR_BUFFER_BIT,
            GL_NEAREST
        );

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDisable(GL_DEPTH_TEST); // disable depth test for post-processing

        // render Ray-marched content
        if (m_backend) {
            int timer = raymarchTimerTag.empty() ? -1 : raymarchTimer.Begin(raymarchTimerTag);
            m_backend->Render(ppFbo[1], ppTex[0], sceneDepth, Camera::GetMainCamera());
            raymarchTimer.End(timer);
        } else {
            // no raymarcher selected: pass the scene through unchanged
            glBindFramebuffer(GL_READ_FRAMEBUFFER, ppFbo[0]);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ppFbo[1]);
            glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        GLuint input = 0;
        GLuint depthTex = 0;
        GLuint output = 0;

        bool ping = false;
        // 2. Execute post-processing passes
        for (auto& effect : postProcessingEffects) {
            if (!effect->enabled)
                continue;

            for (auto pass : effect->passes)
            {
                output = ping ? ppFbo[1] : ppFbo[0];

                glBindFramebuffer(GL_FRAMEBUFFER, output);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

                // set input textures
                input = ping ? ppTex[0] : ppTex[1];
                pass->material->SetTexture2D("_MainTex", input);

                depthTex = ping ? ppDepth[0] : ppDepth[1];
                pass->material->SetTexture2D("_DepthTex", depthTex);


                pass->material->SetTexture2D("_SceneColor", sceneColor);
                pass->material->SetTexture2D("_SceneDepth", sceneDepth);

                pass->material->SetVec4("_TexelSize", glm::vec4(1.0f/float(width), 1.0f/float(height), 0, 0));

                // set additional camera parameters
                Camera* cam = Camera::GetMainCamera();
                pass->material->SetMat4("_InvView", glm::inverse(cam->GetView()));
                pass->material->SetMat4("_InvProj", glm::inverse(cam->GetProjection()));
                pass->material->SetFloat("_Near",cam->nearPlane);
                pass->material->SetFloat("_Far",cam->farPlane);

                for (PostProcessingParameter& param : pass->parameters) {
                    if (param.typeName == "float") {
                        pass->material->SetFloat(param.name, param.floatValue);
                    }
                    else if (param.typeName == "int") {
                        pass->material->SetInt(param.name, param.intValue);
                    }
                    else if (param.typeName == "vec4") {
                        pass->material->SetVec4(param.name, param.vec4Value);
                    }
                    else if (param.typeName == "mat4") {
                        pass->material->SetMat4(param.name, param.mat4Value);
                    }
                }

                pass->material->Bind();
                quadModel->render();

                ping = !ping;

                glBindFramebuffer(GL_FRAMEBUFFER, 0); //unbind
            }
        }

        // 3. Blit to final output
        glBindFramebuffer(GL_READ_FRAMEBUFFER, ping ? ppFbo[0] : ppFbo[1]);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, finalFbo);

        glBlitFramebuffer(
            0, 0, width, height,
            0, 0, width, height,
            GL_COLOR_BUFFER_BIT,
            GL_NEAREST
        );

        glEnable(GL_DEPTH_TEST); // re-enable depth test
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        if (!m_queryInFlight) {
            glEndQuery(GL_TIME_ELAPSED);
            m_queryInFlight = true;
        }
    }

    void Renderer::EnsureFboSized(int width, int height) {
        if (width <= 0 || height <= 0) return;

        // Save previous bindings so we restore them and don't disturb other code
        GLint prevFbo = 0, prevTex = 0, prevRbo = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
        glGetIntegerv(GL_RENDERBUFFER_BINDING, &prevRbo);

        // Destroy previous resources (if any)
        DestroyFbo();

        // Create Scene buffers
        glGenFramebuffers(1, &sceneFbo);
        glGenTextures(1, &sceneColor);
        glGenTextures(1, &sceneDepth);

        // Create color texture storage
        glBindTexture(GL_TEXTURE_2D, sceneColor);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // Bind FBO and attach the color texture
        glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneColor, 0);

        // Create + attach a dedicated depth+stencil texture for this FBO
        glBindTexture(GL_TEXTURE_2D, sceneDepth);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8,
                     width, height, 0,
                     GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                               GL_TEXTURE_2D, sceneDepth, 0);

        // Tell GL which color attachments we'll draw into (required on some drivers)
        GLenum drawbuf = GL_COLOR_ATTACHMENT0;
        glDrawBuffers(1, &drawbuf);

        // Check completeness
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "[Renderer] sceneFBO " << " incomplete! status=0x" << std::hex << status << std::dec << std::endl;
        }

        // Create ping-pong FBOs + color textures + depth textures
        glGenFramebuffers(2, ppFbo);
        glGenTextures(2, ppTex);
        glGenTextures(2, ppDepth);

        for (int i = 0; i < 2; ++i) {
            // Create color texture storage
            glBindTexture(GL_TEXTURE_2D, ppTex[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            // Optional wrap
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            // Bind FBO and attach the color texture
            glBindFramebuffer(GL_FRAMEBUFFER, ppFbo[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ppTex[i], 0);

            // Create + attach a dedicated depth+stencil texture for this FBO
            glBindTexture(GL_TEXTURE_2D, ppDepth[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8,
                         width, height, 0,
                         GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                   GL_TEXTURE_2D, ppDepth[i], 0);

            // Tell GL which color attachments we'll draw into (required on some drivers)
            GLenum drawbuf = GL_COLOR_ATTACHMENT0;
            glDrawBuffers(1, &drawbuf);

            // Check completeness
            GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (status != GL_FRAMEBUFFER_COMPLETE) {
                std::cerr << "[Renderer] FBO " << i << " incomplete! status=0x" << std::hex << status << std::dec << std::endl;
            }
        }

        // Unbind everything we touched and restore previous bindings
        glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
        glBindTexture(GL_TEXTURE_2D, prevTex);
        glBindRenderbuffer(GL_RENDERBUFFER, prevRbo);

        // Final error check
        while (GLenum e = glGetError()) {
            std::cerr << "GL error after EnsureFboSized(): 0x" << std::hex << e << std::dec << std::endl;
        }
    }
    void Renderer::DestroyFbo() {
        // Delete previous resources if they exist
        if (sceneFbo) glDeleteFramebuffers(1, &sceneFbo);
        if (sceneColor) glDeleteTextures(1, &sceneColor);
        if (sceneDepth) glDeleteTextures(1, &sceneDepth);

        if (ppFbo[0]) glDeleteFramebuffers(2, ppFbo);
        if (ppTex[0]) glDeleteTextures(2, ppTex);
        if (ppDepth[0]) glDeleteTextures(2, ppDepth);
    }
} // core