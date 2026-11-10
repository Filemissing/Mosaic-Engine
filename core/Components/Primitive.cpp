//
// Created by micha on 26/03/2026.
//

#include "Primitive.h"

#include <cmath>
#include <glm/glm.hpp>

#include "imgui.h"
#include "../GameObject.h"
#include "../Assets/Scene.h"
#include "../RaymarchMaterialLibrary.h"

namespace core {
    namespace {
        // same matrix as eulerToMat3 in classify.comp (world -> local rotation). Not Transform's own matrix: the bounds
        // have to match exactly what the shader evaluates (glm's mat3 constructor is column-major, like GLSL's)
        glm::mat3 EulerToMat3(glm::vec3 euler) {
            glm::vec3 rad = glm::radians(euler);
            float cx = std::cos(rad.x), sx = std::sin(rad.x);
            float cy = std::cos(rad.y), sy = std::sin(rad.y);
            float cz = std::cos(rad.z), sz = std::sin(rad.z);
            glm::mat3 rx(1, 0, 0, 0, cx, -sx, 0, sx, cx);
            glm::mat3 ry(cy, 0, sy, 0, 1, 0, -sy, 0, cy);
            glm::mat3 rz(cz, -sz, 0, sz, cz, 0, 0, 0, 1);
            return rz * ry * rx;
        }
    }

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

    std::optional<AABB> Primitive::GetWorldBounds() const {
        // local bounds per type, read off the SDFs in classify.comp (before scaling)
        const glm::vec3 d = glm::abs(data);
        glm::vec3 localMin, localMax;
        switch (type) {
            case Sphere:   localMax = glm::vec3(d.x);                 localMin = -localMax; break;
            case Box:      localMax = d;                              localMin = -localMax; break;
            case Pyramid:  localMin = glm::vec3(-d.x, 0.0f, -d.x);    localMax = glm::vec3(d.x, d.y, d.x); break; // data.x = base half size, data.y = height
            case Capsule:  localMax = glm::vec3(d.x, d.y + d.x, d.x); localMin = -localMax; break;
            case Cylinder: localMax = glm::vec3(d.x, d.y, d.x);       localMin = -localMax; break;
            case Torus:    localMax = glm::vec3(d.x + d.y, d.y, d.x + d.y); localMin = -localMax; break;
            case Cone:     localMax = glm::vec3(d.x, d.y, d.x);       localMin = -localMax; break; // conservative: height in both directions
            default:       return std::nullopt; // plane, terrain: unbounded
        }

        // classify maps world -> local as R * (p - pos) / scale, so local -> world is pos + R^T * (local * scale)
        const Transform& transform = gameObject->transform;
        const glm::mat3 localToWorld = glm::transpose(EulerToMat3(transform.rotation));
        AABB bounds{glm::vec3(1e30f), glm::vec3(-1e30f)};
        for (int corner = 0; corner < 8; corner++) {
            glm::vec3 local((corner & 1) ? localMax.x : localMin.x,
                            (corner & 2) ? localMax.y : localMin.y,
                            (corner & 4) ? localMax.z : localMin.z);
            glm::vec3 world = transform.position + localToWorld * (local * transform.scale);
            bounds.min = glm::min(bounds.min, world);
            bounds.max = glm::max(bounds.max, world);
        }
        return bounds;
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