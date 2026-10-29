//
// Runs the same benchmark (scene, camera poses, render size, volume and march settings, frame counts)
// against every raymarcher version and writes the results to Assets/Data/comparison/.
//

#ifndef RAWENGINE_VERSIONBENCHMARK_H
#define RAWENGINE_VERSIONBENCHMARK_H

#include <string>
#include <vector>
#include <glm/glm.hpp>

#include "BenchmarkTool.h"
#include "EditorWindow.h"
#include "../../core/RaymarchVersions/GpuTimer.h"

namespace editor::editorWindows {
    class VersionBenchmark : public EditorWindow {
        using EditorWindow::EditorWindow;

    public:
        struct Pose {
            glm::vec3 position;
            glm::vec3 rotation; // degrees, as in Transform
        };

        void OnGUI() override;

        // advances a running benchmark by one frame; called at the start of every frame, before the viewport renders
        static void Tick();
        static bool IsRunning() { return s_phase != Phase::Idle; }

        // starts a run with every version enabled; quick = few frames, for checking that everything works.
        // The engine closes when the run ends.
        static void RunFromCommandLine(const std::string& sceneName, bool quick);

        // stops a running benchmark, writing what was measured so far
        static void Stop(const std::string& reason) { if (IsRunning()) Abort(reason); }

    private:
        enum class Phase { Idle, StartVersion, Settle, Baking, Warmup, Measure, FinishVersion, Done };

        // frames to wait after freeing a version before the VRAM baseline, since drivers release memory lazily
        static constexpr int SETTLE_FRAMES = 30;

        // ── Configuration ─────────────────────────────────────────────────────
        static char        s_sceneName[128];
        static std::vector<bool> s_versionEnabled;
        static glm::ivec2  s_renderSize;
        static glm::ivec3  s_volumeResolution;
        static glm::ivec3  s_brickPoolDim; // applied to every version with a brick pool, so their memory compares fairly
        static glm::vec3   s_worldMin, s_worldMax;
        static int         s_maxSteps;
        static float       s_maxDist, s_surfDist;
        static int         s_warmupFrames, s_measuredFrames, s_bakeRepeats;
        static bool        s_screenshots;
        static bool        s_exitWhenDone;
        static std::vector<Pose> s_poses;

        // ── Run state ─────────────────────────────────────────────────────────
        static Phase s_phase;
        static std::vector<std::string> s_runList;
        static std::vector<std::string> s_skipped;
        static int s_versionCursor, s_poseIndex, s_frameCounter, s_bakesDone;
        static uint64_t s_lastRenderedFrame;
        static int s_ticksWithoutRender;
        static double s_vramBaseline;
        static std::string s_previousVersion;
        static std::string s_outputDir;
        static std::string s_status;
        static std::vector<BenchmarkEntry> s_results;
        static core::GpuTimer* s_bakeTimer;

        static std::string PosesPath();
        static bool LoadPoses();
        static void SavePoses();

        static void StartRun();
        static void Abort(const std::string& reason);
        static void StartPose();
        // frames the viewport actually rendered since the last call
        static int NewRenderedFrames();
        static void Push(const std::string& name, double value);
        static void WriteScreenshot(const std::string& path);
        static void WriteOutputs();
        static double UsedVramMiB();
        static const std::string& CurrentId() { return s_runList[s_versionCursor]; }
    };
} // editor::editorWindows

#endif //RAWENGINE_VERSIONBENCHMARK_H
