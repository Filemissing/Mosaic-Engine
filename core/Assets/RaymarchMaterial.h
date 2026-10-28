//
// Material for ray-marched primitives: triplanar textures, lighting parameters, an optional custom surface shader
// snippet, or (layered) sub-materials picked by slope and height. Stored as JSON in Assets/Materials/.
//

#ifndef RAWENGINE_RAYMARCHMATERIAL_H
#define RAWENGINE_RAYMARCHMATERIAL_H

#include <string>
#include <glm/glm.hpp>

namespace core {
    class RaymarchMaterial {
    public:
        std::string path; // file this material was loaded from / is saved to

        glm::vec3 tint{1.0f};
        float smoothness = 16.0f;    // specular exponent, same meaning as Material::smoothness in lit.frag
        float metallic = 0.0f;

        // triplanar projection
        float textureScale = 0.25f;  // texture repeats per world unit (object space: per local unit)
        float blendSharpness = 4.0f; // higher = harder transition between the three projections
        glm::vec2 textureOffset{0.0f}; // added to the projected texture coordinates (0.5 centres a texture on the origin)
        bool objectSpace = true;     // projected in the primitive's local space, so textures move with the object

        // textures (empty = none)
        std::string albedoTexture;
        std::string normalTexture;
        std::string roughnessTexture;
        float normalStrength = 1.0f;

        // custom surface shader snippet (empty = default shading only)
        std::string surfaceShader;

        // layered material: picks sub-materials by slope and height instead of using the fields above
        bool layered = false;
        std::string flatMaterial;    // plateau tops / gentle slopes
        std::string steepMaterial;   // cliff walls
        std::string highMaterial;    // optional band above heightStart (empty = none)
        float slopeStart = 0.35f;    // 1 - normal.y where the steep material starts
        float slopeEnd = 0.55f;      // ... and fully takes over
        float heightStart = 30.0f;   // local height where the high material starts
        float heightEnd = 40.0f;     // ... and fully takes over
        float layerNoise = 0.15f;    // raggedness of the transitions

        bool Load(const std::string& file);
        bool Save() const;
    };
} // core

#endif //RAWENGINE_RAYMARCHMATERIAL_H
