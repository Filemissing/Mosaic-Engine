//
// Created by micha on 17/10/2025.

#include "./MeshRenderer.h"
#include "../assimpLoader.h"
#include "Camera.h"
#include "imgui.h"
#include "../GameObject.h"

namespace core {
    MeshRenderer::MeshRenderer(Model* model, Material* material)
    : model(model), material(material) {}

    MeshRenderer::~MeshRenderer() {
        delete model;
        delete material;
        delete texture;
    }

    void MeshRenderer::Render() {
        if (!material || !model || !Camera::GetMainCamera()) return;

        material->SetMat4("mvpMatrix",
            Camera::GetMainCamera()->GetProjection() *
                Camera::GetMainCamera()->GetView() *
                gameObject->transform.GetMatrix());

        material->SetMat4("modelMatrix",
            gameObject->transform.GetMatrix());

        material->SetMat4("viewMatrix",
            Camera::GetMainCamera()->GetView());

        material->SetMat4("projectionMatrix",
            Camera::GetMainCamera()->GetProjection());

        material->Bind();
        model->render();
        glBindVertexArray(0);
    }

    Model* MeshRenderer::GetModel() const {return model;}
    Material* MeshRenderer::GetMaterial() const {return material;}

    void MeshRenderer::OnInspectorGUI() {
        ImGui::Text((std::string("Model Path: ") + model->assetPath).c_str());
        ImGui::Text("Material Info: ");
        ImGui::Indent(20);
        ImGui::Text((std::string("Vertex Shader: ") + material->vertexShaderPath).c_str());
        ImGui::Text((std::string("Fragment Shader: ") + material->fragmentShaderPath).c_str());
        ImGui::Text((std::string("Texture: ") + (texturePath.empty() ? "(none)" : texturePath)).c_str());

        ImGui::Spacing();

        ImGui::SliderFloat("Smoothness", &material->smoothness, 0.1f, 500.0f);
        ImGui::SliderFloat("Metallic", &material->metallic, 0.0f, 1.0f);
        ImGui::Unindent(20);
    }

    nlohmann::json MeshRenderer::Serialize() {
        nlohmann::json json;
        json["type"] = "MeshRenderer";
        json["model"] = model->assetPath;

        json["material"]["vertex"] = material->vertexShaderPath;
        json["material"]["fragment"] = material->fragmentShaderPath;

        json["material"]["smoothness"] = material->smoothness;
        json["material"]["metallic"] = material->metallic;
        if (!texturePath.empty()) json["material"]["texture"] = texturePath;

        return json;
    }

    void MeshRenderer::Deserialize(const nlohmann::json& json) {
        model = AssimpLoader::loadModel(json["model"]);

        material = new Material(
            json["material"]["vertex"],
            json["material"]["fragment"]
            );

        material->smoothness = json["material"]["smoothness"];
        material->metallic = json["material"]["metallic"];

        // optional: without it Material::Bind falls back to white.png
        if (json["material"].contains("texture")) {
            texturePath = json["material"]["texture"].get<std::string>();
            texture = new Texture(texturePath);
            material->SetTexture2D("_MainTex", texture->getId());
        }
    }
}
