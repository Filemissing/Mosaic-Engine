#include "VersionBenchmark.h"

#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <glad/glad.h>

#include "imgui.h"
#include "../Editor.h"
#include "../Utils/SceneManager.h"
#include "../../core/Renderer.h"
#include "../../core/Components/PlayerController.h"
#include "../../core/Components/Primitive.h"

#ifndef RAWENGINE_GIT_HASH
#define RAWENGINE_GIT_HASH "unknown"
#endif

#define GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX 0x9048
#define GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_MEMORY_NVX 0x9049

namespace editor::editorWindows {

    // =========================================================================
    // Static member definitions
    // =========================================================================
    char              VersionBenchmark::s_sceneName[128] = "Benchmark_common";
    std::vector<bool> VersionBenchmark::s_versionEnabled;
    glm::ivec2        VersionBenchmark::s_renderSize       = glm::ivec2(1280, 720);
    glm::ivec3        VersionBenchmark::s_volumeResolution = glm::ivec3(512);
    glm::ivec3        VersionBenchmark::s_brickPoolDim     = glm::ivec3(32); // v3-v6's default; v7 defaults to 64
    glm::vec3         VersionBenchmark::s_worldMin         = glm::vec3(-30.0f);
    glm::vec3         VersionBenchmark::s_worldMax         = glm::vec3(30.0f);
    int               VersionBenchmark::s_maxSteps         = 256;
    float             VersionBenchmark::s_maxDist          = 200.0f;
    float             VersionBenchmark::s_surfDist         = 0.001f;
    int               VersionBenchmark::s_warmupFrames     = 120;
    int               VersionBenchmark::s_measuredFrames   = 300;
    int               VersionBenchmark::s_bakeRepeats      = 10;
    bool              VersionBenchmark::s_screenshots      = true;
    bool              VersionBenchmark::s_exitWhenDone     = false;
    std::vector<VersionBenchmark::Pose> VersionBenchmark::s_poses;

    VersionBenchmark::Phase  VersionBenchmark::s_phase = Phase::Idle;
    std::vector<std::string> VersionBenchmark::s_runList;
    std::vector<std::string> VersionBenchmark::s_skipped;
    int         VersionBenchmark::s_versionCursor = 0;
    int         VersionBenchmark::s_poseIndex     = 0;
    int         VersionBenchmark::s_frameCounter  = 0;
    int         VersionBenchmark::s_bakesDone     = 0;
    uint64_t    VersionBenchmark::s_lastRenderedFrame = 0;
    int         VersionBenchmark::s_ticksWithoutRender = 0;
    double      VersionBenchmark::s_vramBaseline  = -1.0;
    std::string VersionBenchmark::s_previousVersion;
    std::string VersionBenchmark::s_outputDir;
    std::string VersionBenchmark::s_status = "Idle";
    std::vector<BenchmarkEntry> VersionBenchmark::s_results;
    core::GpuTimer* VersionBenchmark::s_bakeTimer = nullptr;

    // =========================================================================
    // UI
    // =========================================================================

    void VersionBenchmark::OnGUI() {
        const auto& versions = core::GetRaymarchVersions();
        if (s_versionEnabled.size() != versions.size()) s_versionEnabled.assign(versions.size(), true);

        const bool running = IsRunning();

        // run controls first, so they stay visible however small the window is
        if (!running) {
            if (ImGui::Button("Run comparison")) StartRun();
        } else {
            if (ImGui::Button("Abort")) Abort("Aborted by user");
        }
        ImGui::SameLine();
        ImGui::TextWrapped("%s", s_status.c_str());
        if (running) {
            const char* phase = s_phase == Phase::Baking ? "baking" :
                                s_phase == Phase::Warmup ? "warming up" :
                                s_phase == Phase::Measure ? "measuring" : "preparing";
            ImGui::Text("Version %d/%zu, pose %d/%zu: %s", s_versionCursor + 1, s_runList.size(),
                        std::min(s_poseIndex + 1, static_cast<int>(s_poses.size())), s_poses.size(), phase);
        }
        if (running && s_ticksWithoutRender >= 60)
            ImGui::TextColored(ImVec4(1, 0.6f, 0, 1), "Paused: the ViewPort must stay visible while measuring.");

        ImGui::BeginDisabled(running);

        ImGui::SeparatorText("Scene and camera");
        if (ImGui::InputText("Scene", s_sceneName, sizeof(s_sceneName))) s_poses.clear();
        ImGui::Text("%zu camera pose(s) — %s", s_poses.size(), PosesPath().c_str());
        if (ImGui::Button("Load poses")) LoadPoses();
        ImGui::SameLine();
        if (ImGui::Button("Capture camera as pose") && Editor::activeScene && Editor::activeScene->mainCamera) {
            const auto& t = Editor::activeScene->mainCamera->gameObject->transform;
            s_poses.push_back({t.position, t.rotation});
        }
        ImGui::SameLine();
        if (ImGui::Button("Save poses")) SavePoses();
        ImGui::SameLine();
        if (ImGui::Button("Clear poses")) s_poses.clear();

        ImGui::SeparatorText("Versions");
        for (size_t i = 0; i < versions.size(); ++i) {
            bool enabled = s_versionEnabled[i];
            if (ImGui::Checkbox(versions[i].id.c_str(), &enabled)) s_versionEnabled[i] = enabled;
            ImGui::SameLine();
            ImGui::TextDisabled("%s (%s)", versions[i].description.c_str(), versions[i].sourceCommit.c_str());
        }

        ImGui::SeparatorText("Settings (identical for every version)");
        ImGui::InputInt2("Render size", &s_renderSize.x);
        ImGui::InputInt3("Volume resolution", &s_volumeResolution.x);
        ImGui::InputInt3("Brick pool size (bricks)", &s_brickPoolDim.x);
        ImGui::SetItemTooltip("Applied to every version with a sparse brick pool (v3 and later)");
        ImGui::InputFloat3("World min", &s_worldMin.x);
        ImGui::InputFloat3("World max", &s_worldMax.x);
        ImGui::InputInt("Max steps", &s_maxSteps);
        ImGui::InputFloat("Max distance", &s_maxDist);
        ImGui::InputFloat("Surface distance", &s_surfDist, 0.0f, 0.0f, "%.5f");
        ImGui::InputInt("Warmup frames per pose", &s_warmupFrames);
        ImGui::InputInt("Measured frames per pose", &s_measuredFrames);
        ImGui::InputInt("Bake repeats", &s_bakeRepeats);
        ImGui::Checkbox("Save a screenshot per version and pose", &s_screenshots);
        s_warmupFrames = std::max(s_warmupFrames, 0);
        s_measuredFrames = std::max(s_measuredFrames, 1);
        s_bakeRepeats = std::max(s_bakeRepeats, 1);

        ImGui::EndDisabled();

        ImGui::SeparatorText("Results");
        constexpr ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
        if (ImGui::BeginTable("##versionResults", 5, flags)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Samples", ImGuiTableColumnFlags_WidthFixed, 60.f);
            ImGui::TableSetupColumn("Mean", ImGuiTableColumnFlags_WidthFixed, 72.f);
            ImGui::TableSetupColumn("Median", ImGuiTableColumnFlags_WidthFixed, 72.f);
            ImGui::TableSetupColumn("StdDev", ImGuiTableColumnFlags_WidthFixed, 72.f);
            ImGui::TableHeadersRow();
            for (const auto& e : s_results) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(e.name.c_str());
                ImGui::TableSetColumnIndex(1); ImGui::Text("%zu", e.samples.size());
                ImGui::TableSetColumnIndex(2); ImGui::Text("%.4f", e.meanVal);
                ImGui::TableSetColumnIndex(3); ImGui::Text("%.4f", e.medianVal);
                ImGui::TableSetColumnIndex(4); ImGui::Text("%.4f", e.stdDev);
            }
            ImGui::EndTable();
        }
    }

    // =========================================================================
    // Poses
    // =========================================================================

    std::string VersionBenchmark::PosesPath() {
        return std::string("Assets/Benchmarks/") + s_sceneName + ".poses.json";
    }

    bool VersionBenchmark::LoadPoses() {
        std::ifstream in(PosesPath());
        if (!in.is_open()) return false;

        s_poses.clear();
        nlohmann::json json = nlohmann::json::parse(in);
        for (const auto& p : json["poses"]) {
            s_poses.push_back({
                glm::vec3(p["position"][0], p["position"][1], p["position"][2]),
                glm::vec3(p["rotation"][0], p["rotation"][1], p["rotation"][2])});
        }
        return true;
    }

    void VersionBenchmark::SavePoses() {
        nlohmann::json json;
        json["poses"] = nlohmann::json::array();
        for (const auto& p : s_poses) {
            json["poses"].push_back({
                {"position", {p.position.x, p.position.y, p.position.z}},
                {"rotation", {p.rotation.x, p.rotation.y, p.rotation.z}}});
        }
        std::filesystem::create_directories("Assets/Benchmarks");
        std::ofstream out(PosesPath());
        out << json.dump(4);
    }

    // =========================================================================
    // Running
    // =========================================================================

    void VersionBenchmark::RunFromCommandLine(const std::string& sceneName, bool quick) {
        std::snprintf(s_sceneName, sizeof(s_sceneName), "%s", sceneName.c_str());
        s_versionEnabled.assign(core::GetRaymarchVersions().size(), true);
        if (quick) {
            s_warmupFrames = 10;
            s_measuredFrames = 30;
            s_bakeRepeats = 2;
        }
        s_exitWhenDone = true;
        StartRun();
        if (!IsRunning()) {
            std::cerr << "[VersionBenchmark] " << s_status << std::endl;
            glfwSetWindowShouldClose(Editor::mainWindow, true);
        }
    }

    void VersionBenchmark::StartRun() {
        const auto& versions = core::GetRaymarchVersions();
        if (s_versionEnabled.size() != versions.size()) s_versionEnabled.assign(versions.size(), true);

        s_runList.clear();
        for (size_t i = 0; i < versions.size(); ++i)
            if (s_versionEnabled[i]) s_runList.push_back(versions[i].id);
        if (s_runList.empty()) { s_status = "No versions selected"; return; }

        // without a poses file, the first version's run falls back to the scene's own camera transform
        if (s_poses.empty()) LoadPoses();

        char stamp[32];
        std::time_t now = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
        std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);
        s_outputDir = std::string("Assets/Data/comparison/") + s_sceneName + "_" + stamp + "/";
        std::filesystem::create_directories(s_outputDir);

        if (!s_bakeTimer) s_bakeTimer = new core::GpuTimer(2);

        s_results.clear();
        s_skipped.clear();
        s_previousVersion = Editor::viewPort->GetRenderer()->GetRaymarchVersion();
        ViewPort::forcedRenderSize = s_renderSize;
        core::PlayerController::inputLocked = true;

        s_versionCursor = 0;
        s_phase = Phase::StartVersion;
    }

    void VersionBenchmark::Abort(const std::string& reason) {
        core::Renderer* renderer = Editor::viewPort->GetRenderer();
        renderer->raymarchTimerTag.clear();
        renderer->raymarchTimer.Collect([](const std::string&, double) {}, true); // discard partial samples
        if (!s_results.empty()) WriteOutputs();

        renderer->SetRaymarchVersion(s_previousVersion);
        ViewPort::forcedRenderSize = glm::ivec2(0);
        core::PlayerController::inputLocked = false;

        s_phase = Phase::Idle;
        s_status = reason;
        std::cout << "[VersionBenchmark] " << reason << std::endl;
        if (s_exitWhenDone) glfwSetWindowShouldClose(Editor::mainWindow, true);
    }

    void VersionBenchmark::StartPose() {
        const Pose& pose = s_poses[s_poseIndex];
        auto& transform = Editor::activeScene->mainCamera->gameObject->transform;
        transform.SetPosition(pose.position);
        transform.SetRotation(pose.rotation);

        s_frameCounter = 0;
        s_lastRenderedFrame = Editor::viewPort->GetRenderer()->renderedFrames;
        s_phase = Phase::Warmup;
    }

    int VersionBenchmark::NewRenderedFrames() {
        uint64_t now = Editor::viewPort->GetRenderer()->renderedFrames;
        int frames = static_cast<int>(now - s_lastRenderedFrame);
        s_lastRenderedFrame = now;

        // the viewport is not drawn while the window is minimized or its tab is hidden; nothing is measured then
        s_ticksWithoutRender = frames > 0 ? 0 : s_ticksWithoutRender + 1;
        if (s_ticksWithoutRender == 60)
            std::cout << "[VersionBenchmark] Paused: the ViewPort is not being drawn (window minimized or tab hidden)" << std::endl;
        return frames;
    }

    void VersionBenchmark::Push(const std::string& name, double value) {
        for (auto& e : s_results) {
            if (e.name == name) {
                e.samples.push_back(value);
                e.Recompute();
                return;
            }
        }
        s_results.push_back({name});
        s_results.back().samples.push_back(value);
        s_results.back().Recompute();
    }

    void VersionBenchmark::Tick() {
        if (s_phase == Phase::Idle) return;

        core::Renderer* renderer = Editor::viewPort->GetRenderer();
        renderer->raymarchTimer.Collect(Push);

        switch (s_phase) {
            case Phase::StartVersion: {
                const std::string& id = CurrentId();
                const core::RaymarchVersionInfo* info = core::FindRaymarchVersion(id);

                // free the previous version and reload the scene, so every version starts from the same state
                renderer->raymarchTimerTag.clear();
                renderer->SetRaymarchVersion("");

                core::Scene* scene = SceneManager::LoadScene(s_sceneName);
                if (!scene) { Abort(std::string("Scene not found: ") + s_sceneName); return; }
                Editor::selectedObject = nullptr;
                SceneManager::SetActiveScene(scene);
                if (!scene->mainCamera) { Abort("Scene has no camera"); return; }
                if (s_poses.empty()) {
                    const auto& t = scene->mainCamera->gameObject->transform;
                    s_poses.push_back({t.position, t.rotation});
                }

                bool hasTerrain = false;
                for (auto* p : scene->primitives)
                    if (p->type == core::Primitive::Terrain) hasTerrain = true;
                if (hasTerrain && !info->supportsTerrain) {
                    std::cout << "[VersionBenchmark] Skipping " << id << ": no terrain support" << std::endl;
                    s_skipped.push_back(id);
                    s_phase = Phase::FinishVersion;
                    return;
                }

                s_frameCounter = 0;
                s_phase = Phase::Settle;
                break;
            }
            case Phase::Settle: {
                if (++s_frameCounter < SETTLE_FRAMES) break;

                const std::string& id = CurrentId();
                glFinish();
                s_vramBaseline = UsedVramMiB();

                renderer->SetRaymarchVersion(id);
                core::RaymarchBackend* backend = renderer->GetBackend();
                backend->SetBrickPoolDim(s_brickPoolDim);
                backend->SetVolume(s_volumeResolution, s_worldMin, s_worldMax);
                backend->SetMarchParams(s_maxSteps, s_maxDist, s_surfDist);

                s_status = "Running " + id;
                std::cout << "[VersionBenchmark] " << s_status << std::endl;
                s_bakesDone = 0;
                s_poseIndex = 0;
                if (backend->HasBake()) {
                    backend->PrepareBake();
                    glFinish();
                    s_phase = Phase::Baking;
                } else {
                    StartPose();
                }
                break;
            }
            case Phase::Baking: {
                const std::string& id = CurrentId();
                core::RaymarchBackend* backend = renderer->GetBackend();

                // Every timed bake starts from freshly allocated volume resources, like the first one.
                // v3 deletes some bake textures after a bake without clearing their handles, so a second bake
                // on the same resources binds deleted textures; re-allocating (untimed) keeps every bake valid.
                backend->SetBrickPoolDim(s_brickPoolDim);
                backend->SetVolume(s_volumeResolution, s_worldMin, s_worldMax);
                glFinish();

                // one bake per frame, each waited on, so bakes never overlap
                int timer = s_bakeTimer->Begin(id + " | Bake (ms)");
                backend->Bake(Editor::activeScene->primitives);
                s_bakeTimer->End(timer);
                Push(id + " | Bake (ms)", s_bakeTimer->Wait(timer));

                if (++s_bakesDone >= s_bakeRepeats) {
                    glFinish();
                    double used = UsedVramMiB();
                    if (used >= 0 && s_vramBaseline >= 0) Push(id + " | VRAM after bake delta (MiB)", used - s_vramBaseline);
                    StartPose();
                }
                break;
            }
            case Phase::Warmup: {
                s_frameCounter += NewRenderedFrames();
                if (s_frameCounter >= s_warmupFrames) {
                    s_frameCounter = 0;
                    renderer->raymarchTimerTag = CurrentId() + " | Raymarch @pose" + std::to_string(s_poseIndex) + " (ms)";
                    s_phase = Phase::Measure;
                }
                break;
            }
            case Phase::Measure: {
                s_frameCounter += NewRenderedFrames();
                if (s_frameCounter < s_measuredFrames) break;

                const std::string& id = CurrentId();
                renderer->raymarchTimerTag.clear();

                if (s_poseIndex == 0) {
                    double used = UsedVramMiB();
                    if (used >= 0 && s_vramBaseline >= 0) Push(id + " | VRAM during render delta (MiB)", used - s_vramBaseline);
                }
                if (s_screenshots)
                    WriteScreenshot(s_outputDir + id + "_pose" + std::to_string(s_poseIndex) + ".ppm");

                if (++s_poseIndex < static_cast<int>(s_poses.size())) StartPose();
                else s_phase = Phase::FinishVersion;
                break;
            }
            case Phase::FinishVersion: {
                renderer->raymarchTimer.Collect(Push, true);
                if (++s_versionCursor < static_cast<int>(s_runList.size())) s_phase = Phase::StartVersion;
                else s_phase = Phase::Done;
                break;
            }
            case Phase::Done: {
                WriteOutputs();
                renderer->SetRaymarchVersion(s_previousVersion);
                ViewPort::forcedRenderSize = glm::ivec2(0);
                core::PlayerController::inputLocked = false;
                s_phase = Phase::Idle;
                s_status = "Done, results in " + s_outputDir;
                std::cout << "[VersionBenchmark] " << s_status << std::endl;
                if (s_exitWhenDone) glfwSetWindowShouldClose(Editor::mainWindow, true);
                break;
            }
            case Phase::Idle:
                break;
        }
    }

    // =========================================================================
    // Output
    // =========================================================================

    double VersionBenchmark::UsedVramMiB() {
        static int hasNvx = -1;
        if (hasNvx < 0) {
            hasNvx = 0;
            GLint n = 0;
            glGetIntegerv(GL_NUM_EXTENSIONS, &n);
            for (GLint i = 0; i < n; i++)
                if (strcmp(reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i)), "GL_NVX_gpu_memory_info") == 0) hasNvx = 1;
        }
        if (!hasNvx) return -1.0;

        GLint totalMemKb = 0, availMemKb = 0;
        glGetIntegerv(GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX, &totalMemKb);
        glGetIntegerv(GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_MEMORY_NVX, &availMemKb);
        return (totalMemKb - availMemKb) / 1024.0;
    }

    void VersionBenchmark::WriteScreenshot(const std::string& path) {
        const int w = Editor::viewPort->width();
        const int h = Editor::viewPort->height();
        if (w <= 0 || h <= 0) return;

        std::vector<unsigned char> pixels(static_cast<size_t>(w) * h * 3);
        glBindTexture(GL_TEXTURE_2D, Editor::viewPort->colorTexture());
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glBindTexture(GL_TEXTURE_2D, 0);

        std::ofstream out(path, std::ios::binary);
        out << "P6\n" << w << " " << h << "\n255\n";
        for (int y = h - 1; y >= 0; --y) // GL rows are bottom-up
            out.write(reinterpret_cast<const char*>(&pixels[static_cast<size_t>(y) * w * 3]), w * 3);
    }

    void VersionBenchmark::WriteOutputs() {
        BenchmarkTool::WriteCSV(s_outputDir + "results.csv", s_results);

        using nlohmann::json;
        json meta;
        meta["gitHash"] = RAWENGINE_GIT_HASH;
        meta["glRenderer"] = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        meta["glVersion"] = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        meta["scene"] = s_sceneName;
        meta["renderSize"] = {s_renderSize.x, s_renderSize.y};
        meta["volumeResolution"] = {s_volumeResolution.x, s_volumeResolution.y, s_volumeResolution.z};
        meta["brickPoolDim"] = {s_brickPoolDim.x, s_brickPoolDim.y, s_brickPoolDim.z};
        meta["worldMin"] = {s_worldMin.x, s_worldMin.y, s_worldMin.z};
        meta["worldMax"] = {s_worldMax.x, s_worldMax.y, s_worldMax.z};
        meta["maxSteps"] = s_maxSteps;
        meta["maxDist"] = s_maxDist;
        meta["surfDist"] = s_surfDist;
        meta["warmupFramesPerPose"] = s_warmupFrames;
        meta["measuredFramesPerPose"] = s_measuredFrames;
        meta["bakeRepeats"] = s_bakeRepeats;
        meta["timing"] = "GPU timestamps around the raymarch pass only (Render) and around Bake; VRAM is the GL_NVX_gpu_memory_info "
                         "used-memory delta against a reading taken with no raymarcher loaded";
        meta["poses"] = json::array();
        for (const auto& p : s_poses) {
            meta["poses"].push_back({
                {"position", {p.position.x, p.position.y, p.position.z}},
                {"rotation", {p.rotation.x, p.rotation.y, p.rotation.z}}});
        }
        meta["versions"] = json::array();
        for (const auto& id : s_runList) {
            const core::RaymarchVersionInfo* info = core::FindRaymarchVersion(id);
            bool skipped = std::find(s_skipped.begin(), s_skipped.end(), id) != s_skipped.end();
            meta["versions"].push_back({
                {"id", id},
                {"sourceCommit", info->sourceCommit},
                {"description", info->description},
                {"skipped", skipped ? "scene contains terrain, which this version does not support" : ""}});
        }

        std::ofstream out(s_outputDir + "meta.json");
        out << meta.dump(4);
    }
} // namespace editor::editorWindows
