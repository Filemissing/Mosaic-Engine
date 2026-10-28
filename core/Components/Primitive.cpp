//
// Created by micha on 26/03/2026.
//

#include "Primitive.h"

#include "imgui.h"
#include "../GameObject.h"
#include "../Assets/Scene.h"
#include "../RaymarchMaterialLibrary.h"

namespace core {
    void Primitive::Start() {
        gameObject->scene->primitives.push_back(this);
    }

    Primitive::~Primitive() {
        auto it = std::find(gameObject->scene->primitives.begin(), gameObject->scene->primitives.end(), this);
        if (it != gameObject->scene->primitives.end()) {
            gameObject->scene->primitives.erase(it);
        }
    }

    GPUPrimitive Primitive::GetGPUPrimitive() const {
        GPUPrimitive primitive{};
        primitive.position = glm::vec4(gameObject->transform.position, type);
        primitive.rotation = glm::vec4(gameObject->transform.rotation, 0);
        primitive.scale = glm::vec4(gameObject->transform.scale, 0);
        primitive.data = glm::vec4(data, 0);
        primitive.color = glm::vec4(color, 0);
        return primitive;
    }


    void Primitive::OnInspectorGUI() {
        static const char* lightTypeLabels[] = {
            "Sphere",
            "Box",
            "Pyramid",
            "Capsule",
            "Cylinder",
            "Torus",
            "Cone",
            "Plane",
            "Terrain"
        };

        int value = static_cast<int>(type);

        if (ImGui::Combo(
            "primitiveType",
            &value,
            lightTypeLabels,
            IM_ARRAYSIZE(lightTypeLabels)))
        {
            type = static_cast<PrimitiveType>(value);
        }

        ImGui::SliderFloat3("color", glm::value_ptr(color), 0.0f, 1.0f);
        ImGui::DragFloat3("halfExtents", glm::value_ptr(data));

        // material (shading only, no rebake needed)
        ImGui::SeparatorText("Material");
        if (ImGui::BeginCombo("material", materialPath.empty() ? "(default)" : materialPath.c_str())) {
            if (ImGui::Selectable("(default)", materialPath.empty())) materialPath.clear();
            for (const auto& file : RaymarchMaterialLibrary::ListMaterialFiles()) {
                if (ImGui::Selectable(file.c_str(), file == materialPath)) materialPath = file;
            }
            ImGui::EndCombo();
        }
        if (RaymarchMaterialLibrary* library = RaymarchMaterialLibrary::Instance())
            library->DrawMaterialEditor(materialPath);
    }

    nlohmann::json Primitive::Serialize() {
        nlohmann::json json;
        json["type"] = "Primitive";
        json["primitiveType"] = type;
        json["color"] = {};

        json["color"]["r"] = color.r;
        json["color"]["g"] = color.g;
        json["color"]["b"] = color.b;

        json["halfExtents"] = {};
        json["halfExtents"]["x"] = data.x;
        json["halfExtents"]["y"] = data.y;
        json["halfExtents"]["z"] = data.z;

        if (!materialPath.empty()) json["material"] = materialPath;

        return json;
    }
    void Primitive::Deserialize(const nlohmann::json& json) {
        type = json["primitiveType"];

        color = glm::vec3(
            json["color"]["r"],
            json["color"]["g"],
            json["color"]["b"]
            );

        data = glm::vec3(
            json["halfExtents"]["x"],
            json["halfExtents"]["y"],
            json["halfExtents"]["z"]
            );

        if (json.contains("material")) materialPath = json["material"].get<std::string>();
    }
} // core