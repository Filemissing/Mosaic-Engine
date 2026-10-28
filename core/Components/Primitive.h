//
// Created by micha on 26/03/2026.
//

#ifndef RAWENGINE_PRIMITIVE_H
#define RAWENGINE_PRIMITIVE_H
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <string>

#include "Component.h"

namespace core {
    struct GPUPrimitive {
        glm::vec4 position; // xyz = position, w = type
        glm::vec4 rotation; // xyz = rotation
        glm::vec4 scale;    // xyz = scale
        glm::vec4 data;     // xyz = data
        glm::vec4 color;    // xyz = color
    };

    class Primitive : public Component {
    public:
        using Component::Component; // inherit Constructors
        ~Primitive() override;

        enum PrimitiveType {
            Sphere,
            Box,
            Pyramid,
            Capsule,
            Cylinder,
            Torus,
            Cone,
            Plane,
            Terrain
        };

        PrimitiveType type = Sphere;
        glm::vec3 color = glm::vec3(1);
        glm::vec3 data = glm::vec3(1, 1, 1);
        std::string materialPath; // Assets/Materials/*.json, empty = default material

        GPUPrimitive GetGPUPrimitive() const;

        void Start() override;
        void OnInspectorGUI() override;

        nlohmann::json Serialize() override;
        void Deserialize(const nlohmann::json& json) override;
        const char* GetTypeName() override { return "Primitive"; }
    };

    REGISTER_COMPONENT(Primitive)
} // core


#endif //RAWENGINE_PRIMITIVE_H