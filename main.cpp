#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <fstream>
#include <sstream>

#include "core/assimpLoader.h"
#include "core/ComponentFactory.h"
#include "core/GameObject.h"
#include "core/Assets/texture.h"
#include "core/Components/MeshRenderer.h"
#include "core/Assets/RenderSettings.h"
#include "core/Assets/Scene.h"
#include "core/Components/Primitive.h"
#include "editor/Editor.h"
#include "editor/Utils/SceneManager.h"
#include "editor/EditorWindows/VersionBenchmark.h"

using namespace core;
using namespace editor;

#define MAC_CLION
//#define VSTUDIO

#ifdef MAC_CLION
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#endif

#ifdef VSTUDIO
// Note: install imgui with:
//     ./vcpkg.exe install imgui[glfw-binding,opengl3-binding]
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#endif

#ifdef _WIN32
// ask hybrid-graphics laptops for the discrete GPU, so benchmarks never land on the integrated one
extern "C" {
    __declspec(dllexport) unsigned long NvOptimusEnablement = 1;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

bool HasNVXMemoryInfo() {
    GLint n = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; i < n; i++) {
        if (strcmp((const char*)glGetStringi(GL_EXTENSIONS, i), "GL_NVX_gpu_memory_info") == 0) return true;
    }
    return false;
}

int main(int argc, char** argv) {
    // Setup
    if (!Editor::Init()) {
        printf("Editor Failed to Initialize\n");
        return -1;
    }
    RenderSettings::Init();

    //Scene
    auto* scene1 = SceneManager::LoadScene("Showcase");
    Editor::activeScene = scene1;

    // RawEngine --version-benchmark <scene> [--quick]: compare every raymarcher version, then exit
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--version-benchmark" && i + 1 < argc) {
            bool quick = false;
            for (int j = 1; j < argc; j++) quick |= std::string(argv[j]) == "--quick";
            editorWindows::VersionBenchmark::RunFromCommandLine(argv[i + 1], quick);
        }
    }

    // Texture CMGaToTexture("Assets/textures/CMGaTo_crop.png");
    // Texture MarbleTexture("Assets/textures/Marble.png");
    // Texture MetalTexture("Assets/textures/Metal.png");

    bool hasMeminfo = HasNVXMemoryInfo();
    printf("VRam info available: %s \n", hasMeminfo ? "yes" : "no");

    while (!glfwWindowShouldClose(Editor::mainWindow)) {
        // calculate delta mouse
        double mouseXPos, mouseYPos; glfwGetCursorPos(Editor::mainWindow, &mouseXPos, &mouseYPos);
        double deltaX = mouseXPos - Editor::oldMousePos.x, deltaY = mouseYPos - Editor::oldMousePos.y;
        Editor::deltaMouse = glm::vec2(deltaX, deltaY);

        // draw the editor
        Editor::Draw();

        // set material values
        // if (auto CMGaTo = Editor::activeScene->FindGameObjectByName("CMGaTo")) {
        //     CMGaTo->GetComponent<MeshRenderer>()->GetMaterial()->SetTexture("_MainTex", CMGaToTexture.getId());
        // }
        // if (auto Suzanne = Editor::activeScene->FindGameObjectByName("Suzanne")) {
        //     Suzanne->GetComponent<MeshRenderer>()->GetMaterial()->SetTexture("_MainTex", MarbleTexture.getId());
        // }
        // if (auto Sphere = Editor::activeScene->FindGameObjectByName("Sphere")) {
        //     Sphere->GetComponent<MeshRenderer>()->GetMaterial()->SetTexture("_MainTex", MetalTexture.getId());
        // }

        if (Editor::activeScene != nullptr)
            Editor::activeScene->Update();

        Editor::EndFrame();

        // update global variables
        Editor::oldMousePos = glm::vec2(mouseXPos, mouseYPos);
        float finishFrameTime = glfwGetTime();
        Editor::deltaTime = (finishFrameTime - Editor::currentTime);
        Editor::currentTime = finishFrameTime;
    }

    Editor::Shutdown();
    return 0;
}