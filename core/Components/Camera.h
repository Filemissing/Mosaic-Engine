//
// Created by micha on 13/10/2025.
//

#ifndef RAWENGINE_CAMERA_H
#define RAWENGINE_CAMERA_H
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Component.h"
#include "../GameObject.h"

namespace core {
    class Camera : public Component {
    private:
        int width, height;
        glm::mat4 projection;

        float fov = 45.0f;

    public:
        float nearPlane = 0.1f;
        float farPlane = 600.0f; // the raymarcher stops at the far plane too, the default world is 200 units across

        glm::mat4 GetView() const;
        glm::mat4 GetProjection() const;
        void RecalculateProjection(float width, float height);

        Camera() : Camera(1, 1) {}
        explicit Camera(GameObject* gameObject) : Camera(1, 1, gameObject) {gameObject->AddComponent(this);};
        Camera(int width, int height, GameObject* gameObject = nullptr);
        ~Camera() override;

        static Camera* GetMainCamera();

        const char* GetTypeName() override { return "Camera"; }

        void OnInspectorGUI() override;

        nlohmann::json Serialize() override;
        void Deserialize(const nlohmann::json&) override;
    };
    REGISTER_COMPONENT(Camera)
}

#endif //RAWENGINE_CAMERA_H