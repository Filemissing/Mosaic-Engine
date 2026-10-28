//
// Created by micha on 17/10/2025.
//

#ifndef RAWENGINE_MESHRENDERER_H
#define RAWENGINE_MESHRENDERER_H

#include "Component.h"
#include "../Assets/Material.h"
#include "../Assets/model.h"
#include "../GameObject.h"

namespace core {
    class MeshRenderer : public Component {
        Model* model;
        Material* material;
        Texture* texture = nullptr; // optional _MainTex, from the scene's "material.texture"
        std::string texturePath;
    public:
        MeshRenderer() : model(nullptr), material(nullptr) {}
        explicit MeshRenderer(GameObject* gameObject) : MeshRenderer() {gameObject->AddComponent(this);};
        MeshRenderer(Model* model, Material* material);
        ~MeshRenderer() override;

        Model* GetModel() const;
        Material* GetMaterial() const;

        void Render();

        const char* GetTypeName() override { return "MeshRenderer"; }

        void OnInspectorGUI() override;

        nlohmann::json Serialize() override;
        void Deserialize(const nlohmann::json&) override;
    };
    REGISTER_COMPONENT(MeshRenderer)
}

#endif //RAWENGINE_MESHRENDERER_H