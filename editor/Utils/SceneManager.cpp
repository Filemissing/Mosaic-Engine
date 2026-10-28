//
// Created by micha on 01/11/2025.
//

#include "SceneManager.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <rttr/registration>

#include "../Editor.h"
using namespace nlohmann;
using namespace rttr;
using namespace core;

namespace editor {
    std::string SceneManager::basePath = "Assets/Scenes/";

    void SceneManager::SaveScene(const Scene* scene, const std::string& sceneName) {
        json sceneJson = json::object();

        std::string name;
        if (sceneName != "") name = sceneName;
        else name = scene->name;

        sceneJson["name"] = name;

        sceneJson["ambientColor"]["r"] = scene->ambientColor.r;
        sceneJson["ambientColor"]["g"] = scene->ambientColor.g;
        sceneJson["ambientColor"]["b"] = scene->ambientColor.b;
        sceneJson["ambientColor"]["a"] = scene->ambientColor.a;

        sceneJson["ambientIntensity"] = scene->ambientIntensity;

        for (GameObject* obj : scene->objects) {
            sceneJson["objects"].push_back(obj->Serialize());
        }

        std::ofstream out(basePath + name + ".json");
        out << sceneJson.dump(4);
    }
    Scene* SceneManager::LoadScene(const std::string& name) {
        auto path = basePath + name + ".json";
        if (!std::filesystem::exists(path)) {
            printf("Scene name was not found");
            return nullptr;
        }
        std::ifstream in(path);
        json sceneJson = json::parse(in);

        auto* scene = new Scene(sceneJson["name"]);

        json color = sceneJson["ambientColor"];
        scene->ambientColor = glm::vec4(color["r"], color["g"], color["b"], color["a"]);
        if (sceneJson.contains("ambientIntensity")) scene->ambientIntensity = sceneJson["ambientIntensity"];

        for (const auto& object : sceneJson["objects"]) {
            auto* gameObject = new GameObject(scene);
            gameObject->Deserialize(object);
            scene->objects.push_back(gameObject);
        }

        return scene;
    }

    void SceneManager::DestoryActiveScene() {
        delete Editor::activeScene;
    }
    void SceneManager::SetActiveScene(Scene* scene) {
        DestoryActiveScene();

        Editor::activeScene = scene;
    }

} // editor