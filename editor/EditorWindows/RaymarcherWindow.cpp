// RaymarcherWindow.cpp
#include "RaymarcherWindow.h"
#include "../../core/Raymarcher.h"
#include "imgui.h"
#include "../Editor.h"

namespace editor::editorWindows {
    void RaymarcherWindow::OnEnable() {
        raymarcher = Editor::viewPort->GetRenderer()->GetRaymarcher();
        pendingRes = raymarcher->GetResolution().x;
        pendingWorldMin = raymarcher->GetWorldMin().x;
        pendingWorldMax = raymarcher->GetWorldMax().x;
        pendingBrickPoolDim = raymarcher->GetBrickPoolDim().x;
    }

    void RaymarcherWindow::OnGUI() {
        if (!raymarcher) {
            ImGui::TextDisabled("No Raymarcher bound");
            return;
        }

        ImGui::SeparatorText("Volume");
        ImGui::DragInt("Resolution", &pendingRes, 1.0f, 1, 4096);
        ImGui::DragFloat("World Min", &pendingWorldMin, 0.1f);
        ImGui::DragFloat("World Max", &pendingWorldMax, 0.1f);
        ImGui::DragInt("Brick pool dimensions", &pendingBrickPoolDim, 0.1f, 1, 256);
        ImGui::SetItemTooltip("Pool size in bricks per axis; holds %d bricks", pendingBrickPoolDim * pendingBrickPoolDim * pendingBrickPoolDim);

        if (ImGui::Button("Apply Volume Settings")) {
            raymarcher->SetBrickPoolDim(glm::ivec3(pendingBrickPoolDim));
            raymarcher->SetWorldBounds(glm::vec3(pendingWorldMin), glm::vec3(pendingWorldMax));
            raymarcher->EnsureVolumeSized(glm::ivec3(pendingRes));
            raymarcher->MarkDirty();
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Bake");
        ImGui::Text("State: %s", raymarcher->IsDirty() ? "Dirty" : "Up to date");
        ImGui::BeginDisabled(!raymarcher->IsDirty());
        if (ImGui::Button("Rebake Now")) raymarcher->Bake(Editor::activeScene->primitives);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Force Rebake")) { raymarcher->MarkDirty(); raymarcher->Bake(Editor::activeScene->primitives); }

        ImGui::Checkbox("Auto-rebake on change", &raymarcher->autoRebake);
        ImGui::SetItemTooltip("Rebake only the bricks around primitives that moved, every frame");
        ImGui::DragInt("Max rebake chunks per frame", &raymarcher->maxRebakeChunksPerFrame, 0.1f, 0, 1000);
        ImGui::SetItemTooltip("0 = unlimited. Chunks over the limit stay marked dirty and are rebaked in later frames");
        if (ImGui::DragInt("Bake budget (MiB)", &raymarcher->bakeBudgetMiB, 1.0f, 8, 8192))
            raymarcher->ApplyBakeBudget();
        ImGui::SetItemTooltip("Scratch VRAM one chunk may use while baking; sets the chunk size");
        ImGui::DragFloat("Coarse skip margin", &raymarcher->coarseMargin, 0.05f, 1.0f, 10.0f);
        ImGui::SetItemTooltip("How far (in brick half-diagonals) from a surface a brick still gets baked on a full bake.\n"
                              "Raise it if parts of the terrain go missing.");

        ImGui::Text("Chunk size: %d bricks", raymarcher->GetChunkBricks());
        ImGui::Text("Dirty chunks pending: %d (last frame: %d processed)",
                    raymarcher->pendingDirtyChunks, raymarcher->lastChunksProcessed);
        ImGui::Text("Pool slots used: %d / %d", raymarcher->poolSlotsUsed, raymarcher->GetPoolCapacity());
        if (raymarcher->droppedBricks > 0)
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "Pool full: %d bricks dropped", raymarcher->droppedBricks);

        ImGui::Spacing();
        ImGui::SeparatorText("Marching");
        ImGui::DragInt("Max Steps", &raymarcher->maxSteps, 1.0f, 1, 1000);
        ImGui::DragFloat("Max Distance", &raymarcher->maxDist, 1.0f, 0.1f, 10000.0f);
        ImGui::DragFloat("Surface Distance", &raymarcher->surfDist, 0.0001f, 0.00001f, 1.0f, "%.5f");
        ImGui::SliderFloat("Color Blend", &raymarcher->colorBlend, 0.0f, 1.0f);

        ImGui::Spacing();
        ImGui::SeparatorText("Noise");
        ImGui::DragInt2("2D Resolution", &raymarcher->noise2DResolution.x, 1.0f, 1, 4096);
        ImGui::DragInt3("3D Resolution", &raymarcher->noise3DResolution.x, 1.0f, 1, 512);
        ImGui::DragFloat("2D Frequency", &raymarcher->noise2DFrequency, 0.1f, 0.01f, 100.0f);
        ImGui::DragFloat("3D Frequency", &raymarcher->noise3DFrequency, 0.1f, 0.01f, 100.0f);
        ImGui::InputScalar("Seed", ImGuiDataType_U32, &raymarcher->noiseSeed);
        if (ImGui::Button("Rebake Noise")) {
            raymarcher->BakeNoise();
            raymarcher->MarkDirty();
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Terrain");
        // a terrain change needs a full rebake: mark dirty when the widget is released, not on every drag step
        auto rebakeOnRelease = [&] { if (ImGui::IsItemDeactivatedAfterEdit()) raymarcher->MarkDirty(); };
        ImGui::DragInt("Octaves", &raymarcher->octaves, 1.0f, 1, 12); rebakeOnRelease();
        ImGui::DragFloat("Lacunarity", &raymarcher->lacunarity, 0.01f, 1.0f, 4.0f); rebakeOnRelease();
        ImGui::DragFloat("Persistence", &raymarcher->persistence, 0.01f, 0.0f, 1.0f); rebakeOnRelease();
        ImGui::DragFloat("Warp Strength", &raymarcher->warpStrength, 0.01f, 0.0f, 4.0f); rebakeOnRelease();
        ImGui::SetItemTooltip("Irregularity of the plateau outlines");
        ImGui::DragFloat("Height Scale", &raymarcher->heightScale, 0.5f, 0.0f, 200.0f); rebakeOnRelease();
        ImGui::DragFloat("Terrain Scale", &raymarcher->terrainScale, 0.005f, 0.01f, 4.0f); rebakeOnRelease();
        ImGui::SetItemTooltip("Frequency of the mesas: lower = fewer, larger plateaus");
        ImGui::DragFloat("Terraces", &raymarcher->terraceCount, 0.05f, 1.0f, 30.0f, "%.1f"); rebakeOnRelease();
        ImGui::SliderFloat("Cliff Sharpness", &raymarcher->cliffSharpness, 0.0f, 1.0f); rebakeOnRelease();
        ImGui::DragFloat("Surface Detail", &raymarcher->detailAmount, 0.001f, 0.0f, 0.5f); rebakeOnRelease();
        ImGui::DragFloat("Overhang Strength", &raymarcher->overhangStrength, 0.01f, 0.0f, 5.0f); rebakeOnRelease();
        ImGui::DragFloat("Overhang Scale", &raymarcher->overhangScale, 0.01f, 0.05f, 10.0f); rebakeOnRelease();
        ImGui::DragFloat("Cliff Roughness", &raymarcher->cliffRoughness, 0.05f, 0.0f, 20.0f); rebakeOnRelease();

        ImGui::Spacing();
        ImGui::SeparatorText("Debug Visualization");
        ImGui::Checkbox("Enable 3D texture view", &raymarcher->debug3D);

        if (raymarcher->debug3D) {
            const char* texNames[] = { "Sign Texture (last chunk)", "Seed Texture (last chunk)", "3D Noise Texture", "Brick flags (last chunk)", "Lookup Texture", "Dirty Texture", "BrickPool Texture" };
            int currentTex = static_cast<int>(raymarcher->debugTex3D);
            if (ImGui::Combo("Texture", &currentTex, texNames, IM_ARRAYSIZE(texNames))) {
                raymarcher->debugTex3D = static_cast<core::Raymarcher::DebugTexture3D>(currentTex);
            }

            ImGui::SliderInt("Slice Z", &raymarcher->sliceZ, 0, raymarcher->GetResolution().z - 1);
            ImGui::SliderInt("Mode", &raymarcher->mode, 0, 2);
            ImGui::DragFloat("Display Scale", &raymarcher->displayScale, 0.1f, 0.001f, 1000.0f);
        }

        ImGui::Checkbox("Enable 2D texture view", &raymarcher->debug2D);

        if (raymarcher->debug2D) {
            const char* texNames[] = { "2D Noise Texture" };
            int currentTex = static_cast<int>(raymarcher->debugTex2D);
            if (ImGui::Combo("Texture", &currentTex, texNames, IM_ARRAYSIZE(texNames))) {
                raymarcher->debugTex2D = static_cast<core::Raymarcher::DebugTexture2D>(currentTex);
            }

            ImGui::SliderInt("Mode", &raymarcher->mode, 0, 2);
            ImGui::DragFloat("Display Scale", &raymarcher->displayScale, 0.1f, 0.001f, 1000.0f);
        }
    }
}
