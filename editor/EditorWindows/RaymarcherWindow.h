// RaymarcherWindow.h
#pragma once
#include "EditorWindow.h"
#include <glm/vec3.hpp>

namespace core { class Raymarcher; }

namespace editor::editorWindows {
    class RaymarcherWindow : public EditorWindow {
        using EditorWindow::EditorWindow;

        core::Raymarcher* raymarcher = nullptr;

        // the volume is always a cube, so the window edits one value per setting (read from the raymarcher in OnEnable)
        int pendingRes = 1024;
        float pendingWorldMin = -100.0f;
        float pendingWorldMax = 100.0f;
        int pendingBrickPoolDim = 64;

    public:
        void OnEnable() override;
        void OnGUI() override;
    };
}