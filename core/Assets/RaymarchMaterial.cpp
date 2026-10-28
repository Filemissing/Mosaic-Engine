//
// Material for ray-marched primitives, see RaymarchMaterial.h
//

#include "RaymarchMaterial.h"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace core {
    namespace {
        template <typename T>
        void Read(const nlohmann::json& json, const char* key, T& value) {
            if (json.contains(key)) value = json[key].get<T>();
        }
    }

    bool RaymarchMaterial::Load(const std::string& file) {
        std::ifstream stream(file);
        if (!stream.is_open()) {
            std::cerr << "Raymarch material not found: " << file << std::endl;
            return false;
        }

        nlohmann::json json;
        try {
            stream >> json;
        } catch (const std::exception& e) {
            std::cerr << "Raymarch material " << file << " is not valid JSON: " << e.what() << std::endl;
            return false;
        }

        path = file;
        if (json.contains("tint")) tint = glm::vec3(json["tint"][0], json["tint"][1], json["tint"][2]);
        Read(json, "smoothness", smoothness);
        Read(json, "metallic", metallic);
        Read(json, "textureScale", textureScale);
        Read(json, "blendSharpness", blendSharpness);
        if (json.contains("textureOffset")) textureOffset = glm::vec2(json["textureOffset"][0], json["textureOffset"][1]);
        Read(json, "objectSpace", objectSpace);
        Read(json, "albedoTexture", albedoTexture);
        Read(json, "normalTexture", normalTexture);
        Read(json, "roughnessTexture", roughnessTexture);
        Read(json, "normalStrength", normalStrength);
        Read(json, "surfaceShader", surfaceShader);
        Read(json, "layered", layered);
        Read(json, "flatMaterial", flatMaterial);
        Read(json, "steepMaterial", steepMaterial);
        Read(json, "highMaterial", highMaterial);
        Read(json, "slopeStart", slopeStart);
        Read(json, "slopeEnd", slopeEnd);
        Read(json, "heightStart", heightStart);
        Read(json, "heightEnd", heightEnd);
        Read(json, "layerNoise", layerNoise);
        return true;
    }

    bool RaymarchMaterial::Save() const {
        nlohmann::json json;
        json["tint"] = {tint.x, tint.y, tint.z};
        json["smoothness"] = smoothness;
        json["metallic"] = metallic;
        json["textureScale"] = textureScale;
        json["blendSharpness"] = blendSharpness;
        json["textureOffset"] = {textureOffset.x, textureOffset.y};
        json["objectSpace"] = objectSpace;
        json["albedoTexture"] = albedoTexture;
        json["normalTexture"] = normalTexture;
        json["roughnessTexture"] = roughnessTexture;
        json["normalStrength"] = normalStrength;
        json["surfaceShader"] = surfaceShader;
        json["layered"] = layered;
        json["flatMaterial"] = flatMaterial;
        json["steepMaterial"] = steepMaterial;
        json["highMaterial"] = highMaterial;
        json["slopeStart"] = slopeStart;
        json["slopeEnd"] = slopeEnd;
        json["heightStart"] = heightStart;
        json["heightEnd"] = heightEnd;
        json["layerNoise"] = layerNoise;

        std::ofstream stream(path);
        if (!stream.is_open()) {
            std::cerr << "Could not save raymarch material: " << path << std::endl;
            return false;
        }
        stream << json.dump(4);
        return true;
    }
} // core
