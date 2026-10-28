//
// Created by micha on 23/10/2025.
//

#include <glad/glad.h>

#include "Editor.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "EditorWindows/BenchmarkTool.h"
#include "EditorWindows/HierarchyWindow.h"
#include "EditorWindows/Inspector.h"
#include "EditorWindows/RaymarcherWindow.h"
#include "EditorWindows/SceneLoader.h"
#include "EditorWindows/TransformWindow.h"
#include "EditorWindows/VersionBenchmark.h"

namespace editor {
    // static fields
    int Editor::g_width = 800;
    int Editor::g_height = 600;

    float Editor::m_windowBorderSize = 0.0f;
    float Editor::m_windowRoundingValue = 0.0f;
    ImVec2 Editor::m_windowPadding = ImVec2(0, 0);
    ImGuiID Editor::m_dockspaceId = 0;

    editorWindows::ViewPort* Editor::viewPort = nullptr;
    std::vector<editorWindows::EditorWindow*> Editor::windows;
    GLFWwindow* Editor::mainWindow = nullptr;

    core::Scene* Editor::activeScene = nullptr;
    core::GameObject* Editor::selectedObject = nullptr;

    glm::vec2 Editor::oldMousePos = glm::vec2();
    glm::vec2 Editor::deltaMouse = glm::vec2();
    float Editor::currentTime = 0.0;
    float Editor::deltaTime = 0.0;

    // private
    void Editor::FramebufferSizeCallback(GLFWwindow *window, int width, int height) {
        g_width = width;
        g_height = height;
        glViewport(0, 0, width, height);
    }
    bool Editor::InitGLFW(GLFWwindow*& outWindow) {
        glfwInit();
        glfwWindowHint(GLFW_SAMPLES, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    #ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    #endif

        GLFWwindow* window = glfwCreateWindow(g_width, g_height, "LearnOpenGL", nullptr, nullptr);
        if (window == nullptr) {
            printf("Failed to create GLFW window\n");
            glfwTerminate();
            return false;
        }
        glfwMakeContextCurrent(window);
        glfwSwapInterval(0); // disable Vsync

        glfwSetFramebufferSizeCallback(window, FramebufferSizeCallback);

        if (!gladLoadGLLoader((GLADloadproc) glfwGetProcAddress)) {
            printf("Failed to initialize GLAD\n");
            return false;
        }

        outWindow = window;
        return true;
    }
    void Editor::InitImGui() {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        //Setup platforms
        ImGui_ImplGlfw_InitForOpenGL(mainWindow, true);
        ImGui_ImplOpenGL3_Init("#version 400");
    }
    void Editor::DrawDockSpace() {
        ImGuiWindowFlags window_flags =
            ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNavFocus;

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::SetNextWindowViewport(vp->ID);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, m_windowBorderSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, m_windowBorderSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, m_windowPadding);
        ImGui::Begin("DockSpaceHost", nullptr, window_flags);
        ImGui::PopStyleVar(3);

        m_dockspaceId = ImGui::GetID("MainDockspace");
        ImGui::DockSpace(m_dockspaceId, ImVec2(0, 0), ImGuiDockNodeFlags_None);
    }
    void Editor::InitImGuiStyles() {
        ImGuiIO& io = ImGui::GetIO();
        ImGuiStyle& style = ImGui::GetStyle();

        io.Fonts->AddFontFromFileTTF("fonts/Roboto-Regular.ttf", 16.0f);
    }

    template <typename T>
    T* CreateEditorWindow(const std::string& name) {
        T* win = new T(name);
        win->OnEnable();
        return win;
    }
    // public
    bool Editor::Init() {
        if (!InitGLFW(mainWindow)) {
            printf("Failed to create GLFW window\n");
            return false;
        }
        InitImGui();
        InitImGuiStyles();

        // add default windows here
        windows.push_back(CreateEditorWindow<editorWindows::HierarchyWindow>("Hierarchy"));
        windows.push_back(CreateEditorWindow<editorWindows::SceneLoader>("Scene Loader"));
        windows.push_back(CreateEditorWindow<editorWindows::ViewPort>("ViewPort"));
        windows.push_back(CreateEditorWindow<editorWindows::Inspector>("Inspector"));
        windows.push_back(CreateEditorWindow<editorWindows::BenchmarkTool>("Benchmark Tool"));
        windows.push_back(CreateEditorWindow<editorWindows::RaymarcherWindow>("Ray-marcher"));
        windows.push_back(CreateEditorWindow<editorWindows::VersionBenchmark>("Version Benchmark"));

        return true;
    }

    void Editor::Draw() {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // before any window draws, so a benchmark can swap the scene/version and place the camera for this frame
        editorWindows::VersionBenchmark::Tick();

        DrawDockSpace();
        DrawMainMenu();

         for (auto window : windows) {
             window->Draw();
         }

        ImGui::End(); // end dockspace window
    }

    static std::vector<float> fpsHistory;
    static float GetAverageFPS(float deltaTime)
    {
        fpsHistory.push_back(1.0f / deltaTime);
        if (fpsHistory.size() > 60)
            fpsHistory.erase(fpsHistory.begin());
        float sum = 0.0f;
        for (float fps : fpsHistory)
            sum += fps;
        return sum / fpsHistory.size();
    }
    void Editor::DrawMainMenu() {
        if (!ImGui::BeginMainMenuBar()) return;

        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Exit")) { glfwSetWindowShouldClose(mainWindow, true); }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Settings")) {
            if (ImGui::BeginMenu("Extra Options"))
            {
                ImGui::DragFloat("Font Scale", &ImGui::GetIO().FontGlobalScale, 0.01f, 0.5f, 3.0f);
                ImGui::DragFloat("Window Rounding", &m_windowRoundingValue, 0.1f, 0.0f, 12.0f);
                ImGui::DragFloat("Window Border Size", &m_windowBorderSize, 0.1f, 0.0f, 5.0f);
                ImGui::DragFloat2("Window Padding", &m_windowPadding[0], 0.1f, 0.0f, 20.0f);
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Windows")) {
            for (auto window : windows) {
                ImGui::Checkbox(window->name.c_str(), &window->isEnabled);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            if (ImGui::BeginMenu("Post Processing")) {
                if (viewPort) {
                    for (auto pass : viewPort->GetRenderer()->postProcessingEffects) {
                        ImGui::Checkbox(pass->name.c_str(), &pass->enabled);
                        pass->OnGUI();
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        ImGui::Text("FPS: %.0f", GetAverageFPS(deltaTime));
        ImGui::EndMainMenuBar();
    }

    void Editor::EndFrame() {
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(mainWindow);
        glfwPollEvents();
    }

    glm::vec2 Editor::GetMainWindowSize() {
        return {g_width, g_height};
    }
    glm::vec2 Editor::GetViewPortSize() {
        if (!viewPort) return glm::vec2(0, 0);

        return {viewPort->width(), viewPort->height()};
    }

    GLuint Editor::GetFrameBuffer() {
        if (viewPort) return viewPort->framebuffer();
        return 0;
    }

    void Editor::Shutdown() {
        editorWindows::VersionBenchmark::Stop("Engine closed during the run; partial results written");

        // close all windows
        for (auto window : windows) {
            delete window;
        }

        delete activeScene;

        // Terminate processes
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        glfwTerminate();
    }
} // Editor