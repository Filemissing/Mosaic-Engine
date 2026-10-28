//
// Created by micha on 19/10/2025.
//

#ifndef RAWENGINE_PLAYERCONTROLLER_H
#define RAWENGINE_PLAYERCONTROLLER_H
#include "Component.h"
#include "../GameObject.h"

namespace core {
    class PlayerController : public Component {
    private:
        bool isMoving = false;
    public:
        float moveSpeed = 5000.0f;
        float rotateSpeed = 20000.0f;

        PlayerController() = default;
        PlayerController(GameObject* gameObject) : PlayerController() {gameObject->AddComponent(this);};

        void Update() override;

        const char* GetTypeName() override { return "PlayerController"; }

        void OnInspectorGUI() override;

        nlohmann::json Serialize() override;
        void Deserialize(const nlohmann::json&) override;
    };
    REGISTER_COMPONENT(PlayerController)
}

#endif //RAWENGINE_PLAYERCONTROLLER_H