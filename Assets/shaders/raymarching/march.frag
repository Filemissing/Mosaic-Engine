#version 430 core

in vec2 uv;
out vec4 FragColor;

uniform sampler2D _MainTex;
uniform sampler2D _SceneDepth;
uniform sampler3D _LookupTex;
uniform sampler3D _BrickPoolTex;
uniform usampler3D _BrickIdTex; // same layout as _BrickPoolTex, index of the nearest primitive per texel

struct Primitive {
    vec4 position;
    vec4 rotation;
    vec4 scale;
    vec4 data;
    vec4 color;
};

layout(std430, binding = 0) readonly buffer PrimitiveBuffer {
    Primitive primitives[];
};
uniform int primitiveCount;

uniform vec3 _WorldMin;
uniform vec3 _WorldMax;

uniform vec3 _Resolution;
uniform vec3 _GridResolution;
uniform vec3 _PoolResolution;
uniform int BRICK_CORE;
uniform int BRICK_STORE;

uniform mat4 _InvView;
uniform mat4 _InvProj;
uniform vec3 _CameraPos;
uniform float _Far;

uniform int _MaxSteps;
uniform float _MaxDist;
uniform float _SurfDist;
uniform float _ColorBlend;

float sceneRayDist() {
    float depth = texture(_SceneDepth, uv).r;
    if (depth >= 0.9999) return _Far;
    vec3 ndc = vec3(uv * 2.0 - 1.0, depth * 2.0 - 1.0);
    vec4 viewPos = _InvProj * vec4(ndc, 1.0);
    viewPos /= viewPos.w;
    vec4 worldPos = _InvView * viewPos;
    return length(worldPos.xyz - _CameraPos);
}

vec3 rayDirection() {
    vec2 ndc = uv * 2.0 - 1.0;
    vec4 viewPos = _InvProj * vec4(ndc, -1.0, 1.0);
    viewPos.xyz /= viewPos.w;
    viewPos.w = 0.0;
    return normalize((_InvView * viewPos).xyz);
}

// -- volume sampling --
vec3 worldToUVW(vec3 p) {
    return (p - _WorldMin) / (_WorldMax - _WorldMin);
}

float sampleVolumeRaw(vec3 p, float fallback) {
    vec3 voxel = (p - _WorldMin) / (_WorldMax - _WorldMin) * _Resolution;

    vec3 brickIndexF = floor(voxel / float(BRICK_CORE));
    brickIndexF = clamp(brickIndexF, vec3(0.0), _GridResolution - 1.0);
    ivec3 brickIndex = ivec3(brickIndexF);

    vec4 lookup = texelFetch(_LookupTex, brickIndex, 0);
    if (lookup.w < 0.5) return fallback; // no data here — assume "same as center", contributes ~0 to the gradient

    vec3 localOffset = clamp(voxel - brickIndexF * float(BRICK_CORE), 0.0, float(BRICK_STORE - 1));
    vec3 poolTexelCoord = lookup.xyz + localOffset;
    vec3 uvw = (poolTexelCoord + 0.5) / _PoolResolution;
    return texture(_BrickPoolTex, uvw).r;
}

vec3 calcNormal(vec3 p) {
    float center = sampleVolumeRaw(p, 0.0); // p itself is always inside an allocated brick — this hit, so fallback here is moot
    // one voxel: a fixed world-space offset blurs detail finer than itself (0.5 was ~2.5 voxels at 1024^3)
    vec3 voxelSize = (_WorldMax - _WorldMin) / _Resolution;
    float e = min(voxelSize.x, min(voxelSize.y, voxelSize.z));
    return normalize(vec3(
    sampleVolumeRaw(p + vec3(e,0,0), center) - sampleVolumeRaw(p - vec3(e,0,0), center),
    sampleVolumeRaw(p + vec3(0,e,0), center) - sampleVolumeRaw(p - vec3(0,e,0), center),
    sampleVolumeRaw(p + vec3(0,0,e), center) - sampleVolumeRaw(p - vec3(0,0,e), center)
    ));
}

float sampleVolumeWithSkip(vec3 p, vec3 rd, out float skipDist) {
    skipDist = 0.0;

    vec3 voxel = (p - _WorldMin) / (_WorldMax - _WorldMin) * _Resolution;

    vec3 brickIndexF = floor(voxel / float(BRICK_CORE));
    brickIndexF = clamp(brickIndexF, vec3(0.0), _GridResolution - 1.0);
    ivec3 brickIndex = ivec3(brickIndexF);

    vec4 lookup = texelFetch(_LookupTex, brickIndex, 0);

    if (lookup.w >= 0.5) {
        vec3 localOffset = clamp(voxel - brickIndexF * float(BRICK_CORE), 0.0, float(BRICK_STORE - 1));
        vec3 poolTexelCoord = lookup.xyz + localOffset;
        vec3 uvw = (poolTexelCoord + 0.5) / _PoolResolution;
        return texture(_BrickPoolTex, uvw).r;
    } else if (lookup.w <= -0.5) {
        skipDist = 0.0;
        return -1.0; // negative distance means inside surface
    } else {
        // compute this brick's world-space AABB and find where the ray exits it
        vec3 brickWorldMin = _WorldMin + (brickIndexF * float(BRICK_CORE)) / _Resolution * (_WorldMax - _WorldMin);
        vec3 brickWorldMax = _WorldMin + ((brickIndexF + 1.0) * float(BRICK_CORE)) / _Resolution * (_WorldMax - _WorldMin);

        vec3 invD = 1.0 / rd;
        vec3 t0 = (brickWorldMin - p) * invD;
        vec3 t1 = (brickWorldMax - p) * invD;
        vec3 tMax = max(t0, t1);
        float tExit = min(min(tMax.x, tMax.y), tMax.z);

        skipDist = max(tExit, 0.0001); // guard against a degenerate/zero step if p sits exactly on the boundary
        return 1e9; // signal "no surface here" — the caller advances by skipDist, not this value, as the step
    }
}

// =====================================================================================================================
// Materials and shading
// The hit knows its object: the bake stores a primitive slot per voxel (_BrickIdTex) and primitives[id].color.w holds
// that primitive's material index (written by Raymarcher::SyncPrimitiveSlots).
// =====================================================================================================================

struct Light {
    int type; // 0 = directional, 1 = point
    vec4 color;
    vec3 position;
    vec3 rotation; // forward vector
    float attenuation;
};
uniform Light lights[8];
uniform int lightsAmount;
uniform vec4 ambientColor;
uniform float ambientIntensity;
uniform float _Time;

// mirrored by GPUMaterial in RaymarchMaterialLibrary.h
struct Material {
    vec4 tint;        // rgb tint, a = normal strength
    vec4 params;      // x texture scale, y blend sharpness, z smoothness, w metallic
    ivec4 layers;     // texture layers: x albedo, y normal, z roughness (-1 = none), w surface shader index
    ivec4 layered;    // sub-materials: x flat, y steep, z high (-1 = none), w flags (1 object space, 2 layered)
    vec4 layerRanges; // x slope start, y slope end, z height start, w height end
    vec4 extra;       // x layer noise
};
layout(std430, binding = 6) readonly buffer MaterialBuffer {
    Material materials[];
};
uniform int materialCount;
uniform sampler2DArray _MaterialTextures;

// what a surface shader gets...
struct SurfaceInput {
    vec3 worldPos;
    vec3 worldNormal;
    vec3 objectPos;    // in the primitive's local space (rotation, then scale, as in classify.comp)
    vec3 objectNormal;
    vec3 viewDir;      // from the surface towards the camera
    float time;
    float footprint;   // world size of one pixel at this hit, for texture filtering
    int primitive;
    int material;
};
// ...and what it produces
struct SurfaceData {
    vec3 albedo;
    vec3 normal;       // world space
    float smoothness;
    float metallic;
    vec3 emission;
    bool unlit;
};

// same matrix as eulerToMat3 in classify.comp (world -> local rotation)
mat3 eulerToMat3(vec3 euler) {
    vec3 rad = radians(euler);
    float cx = cos(rad.x), sx = sin(rad.x);
    float cy = cos(rad.y), sy = sin(rad.y);
    float cz = cos(rad.z), sz = sin(rad.z);
    mat3 rx = mat3(1, 0, 0, 0, cx, -sx, 0, sx, cx);
    mat3 ry = mat3(cy, 0, sy, 0, 1, 0, -sy, 0, cy);
    mat3 rz = mat3(cz, -sz, 0, sz, cz, 0, 0, 0, 1);
    return rz * ry * rx;
}

float hash31(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}
// smooth value noise, used to break up the layer transitions
float valueNoise(vec3 p) {
    vec3 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash31(i), hash31(i + vec3(1, 0, 0)), f.x), mix(hash31(i + vec3(0, 1, 0)), hash31(i + vec3(1, 1, 0)), f.x), f.y),
               mix(mix(hash31(i + vec3(0, 0, 1)), hash31(i + vec3(1, 0, 1)), f.x), mix(hash31(i + vec3(0, 1, 1)), hash31(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}

vec3 triplanarWeights(vec3 n, float sharpness) {
    vec3 w = pow(abs(n), vec3(sharpness));
    return w / max(w.x + w.y + w.z, 1e-5);
}

// One texture array layer projected along the three axes. textureGrad with an explicit footprint instead of
// texture(): neighbouring pixels can hit different surfaces, and implicit derivatives would pick a tiny mip there
// and draw seams. A projection seen at a grazing angle gets a proportionally larger footprint (blurs, no shimmer).
vec4 sampleTriplanar(int layer, vec3 p, vec3 n, float scale, float sharpness, float footprint) {
    if (layer < 0) return vec4(1.0);
    vec3 w = triplanarWeights(n, sharpness);
    vec3 an = max(abs(n), vec3(0.2));
    vec4 result = vec4(0.0);
    if (w.x > 0.01) { float g = footprint * scale / an.x; result += w.x * textureGrad(_MaterialTextures, vec3(p.zy * scale, layer), vec2(g, 0.0), vec2(0.0, g)); }
    if (w.y > 0.01) { float g = footprint * scale / an.y; result += w.y * textureGrad(_MaterialTextures, vec3(p.xz * scale, layer), vec2(g, 0.0), vec2(0.0, g)); }
    if (w.z > 0.01) { float g = footprint * scale / an.z; result += w.z * textureGrad(_MaterialTextures, vec3(p.xy * scale, layer), vec2(g, 0.0), vec2(0.0, g)); }
    return result;
}

// Triplanar normal map, whiteout blend (Ben Golus, "Normal Mapping for a Triplanar Shader"): each projection's
// tangent-space normal is swizzled into the space of n and added to it. Returns a normal in the same space as n.
vec3 sampleTriplanarNormal(int layer, vec3 p, vec3 n, float scale, float sharpness, float footprint, float strength) {
    if (layer < 0 || strength <= 0.0) return n;
    vec3 w = triplanarWeights(n, sharpness);
    vec3 an = max(abs(n), vec3(0.2));
    float gx = footprint * scale / an.x, gy = footprint * scale / an.y, gz = footprint * scale / an.z;
    vec3 tx = textureGrad(_MaterialTextures, vec3(p.zy * scale, layer), vec2(gx, 0.0), vec2(0.0, gx)).xyz * 2.0 - 1.0;
    vec3 ty = textureGrad(_MaterialTextures, vec3(p.xz * scale, layer), vec2(gy, 0.0), vec2(0.0, gy)).xyz * 2.0 - 1.0;
    vec3 tz = textureGrad(_MaterialTextures, vec3(p.xy * scale, layer), vec2(gz, 0.0), vec2(0.0, gz)).xyz * 2.0 - 1.0;
    tx.xy *= strength; ty.xy *= strength; tz.xy *= strength;
    // whiteout: tangent xy + surface normal, z multiplied; flip for back-facing projections
    tx = vec3(tx.xy + n.zy, abs(tx.z) * n.x);
    ty = vec3(ty.xy + n.xz, abs(ty.z) * n.y);
    tz = vec3(tz.xy + n.xy, abs(tz.z) * n.z);
    return normalize(tx.zyx * w.x + ty.xzy * w.y + tz.xyz * w.z);
}

// default shading of a plain (non-layered) material: tint x albedo texture, normal map, roughness map
SurfaceData defaultSurface(int materialIndex, SurfaceInput i) {
    Material m = materials[materialIndex];
    bool objectSpace = (m.layered.w & 1) != 0;
    vec3 p = objectSpace ? i.objectPos : i.worldPos;
    vec3 n = objectSpace ? i.objectNormal : i.worldNormal;
    float scale = m.params.x, sharpness = m.params.y;

    SurfaceData s;
    s.albedo = m.tint.rgb * sampleTriplanar(m.layers.x, p, n, scale, sharpness, i.footprint).rgb;
    s.normal = i.worldNormal;
    if (m.layers.y >= 0) {
        vec3 mapped = sampleTriplanarNormal(m.layers.y, p, n, scale, sharpness, i.footprint, m.tint.a);
        // back to world space: object normals were made with the world -> local rotation and divided scale
        s.normal = objectSpace ? normalize(transpose(eulerToMat3(primitives[i.primitive].rotation.xyz))
                                           * (mapped / primitives[i.primitive].scale.xyz))
                               : mapped;
    }
    float roughness = m.layers.z >= 0 ? sampleTriplanar(m.layers.z, p, n, scale, sharpness, i.footprint).r : 0.0;
    s.smoothness = mix(m.params.z, 2.0, roughness);
    s.metallic = m.params.w;
    s.emission = vec3(0.0);
    s.unlit = false;
    return s;
}

// custom surface snippets (Assets/shaders/raymarching/surfaces/*.glsl), stitched in by RaymarchMaterialLibrary:
// each defines void surface(in SurfaceInput i, inout SurfaceData s) and receives the default result in s
// @SURFACE_FUNCTIONS
void applyCustomSurface(int shaderIndex, SurfaceInput i, inout SurfaceData s) {
    switch (shaderIndex) {
// @SURFACE_DISPATCH
        default: break;
    }
}

SurfaceData plainSurface(int materialIndex, SurfaceInput i) {
    i.material = materialIndex;
    SurfaceData s = defaultSurface(materialIndex, i);
    applyCustomSurface(materials[materialIndex].layers.w, i, s);
    return s;
}

SurfaceData blendSurface(SurfaceData a, SurfaceData b, float t) {
    SurfaceData s;
    s.albedo = mix(a.albedo, b.albedo, t);
    s.normal = normalize(mix(a.normal, b.normal, t));
    s.smoothness = mix(a.smoothness, b.smoothness, t);
    s.metallic = mix(a.metallic, b.metallic, t);
    s.emission = mix(a.emission, b.emission, t);
    s.unlit = t < 0.5 ? a.unlit : b.unlit;
    return s;
}

// a layered material picks its sub-materials by slope and height (object space, e.g. the terrain's local height)
SurfaceData evaluateMaterial(int materialIndex, SurfaceInput i) {
    if (materialIndex < 0 || materialIndex >= materialCount) materialIndex = 0;
    Material m = materials[materialIndex];
    if ((m.layered.w & 2) == 0) return plainSurface(materialIndex, i);

    float noise = (valueNoise(i.objectPos * 0.35) - 0.5) * m.extra.x;
    float steep = smoothstep(m.layerRanges.x, m.layerRanges.y, 1.0 - i.worldNormal.y + noise);
    float high = m.layered.z >= 0 ? smoothstep(m.layerRanges.z, m.layerRanges.w, i.objectPos.y + noise * 20.0) : 0.0;

    // at most two sub-materials per pixel: the high band replaces the flat ground, cliffs stay rock
    int ground = (high > 0.5 && m.layered.z >= 0) ? m.layered.z : max(m.layered.x, 0);
    if (steep < 0.01) return plainSurface(ground, i);
    if (steep > 0.99) return plainSurface(max(m.layered.y, 0), i);
    return blendSurface(plainSurface(ground, i), plainSurface(max(m.layered.y, 0), i), steep);
}

// lit.frag's lighting model, with the scene's lights
vec3 lighting(SurfaceData s, vec3 pos, vec3 viewDir) {
    if (s.unlit) return s.albedo + s.emission;
    vec3 ambient = ambientIntensity * ambientColor.rgb * s.albedo;
    vec3 diffuse = vec3(0.0);
    vec3 specular = vec3(0.0);
    int count = lightsAmount;
    for (int l = 0; l < count; l++) {
        Light light = lights[l];
        vec3 fromLight;
        float attenuation = 1.0;
        if (light.type == 0) {
            fromLight = normalize(light.rotation);
        } else {
            fromLight = normalize(pos - light.position);
            float dist = distance(pos, light.position);
            attenuation = (1.0 + 0.1 * dist + 0.1 * dist * dist) * light.attenuation;
        }
        float d = dot(s.normal, -fromLight);
        diffuse += max(d, 0.0) * light.color.rgb * s.albedo / attenuation * (1.0 - s.metallic);
        vec3 reflected = normalize(reflect(fromLight, s.normal));
        vec3 specularColor = mix(s.albedo * light.color.rgb, light.color.rgb, s.metallic);
        specular += pow(max(dot(reflected, viewDir), 0.0), s.smoothness) * sqrt(s.smoothness) * specularColor / attenuation * 0.2;
    }
    if (count == 0) { // scene without lights: keep the old fixed light so things stay visible
        diffuse = s.albedo * (0.15 + 0.85 * max(dot(s.normal, normalize(vec3(0.6, 1.0, 0.4))), 0.0));
    }
    return ambient + diffuse + specular + s.emission;
}

SurfaceInput makeInput(vec3 pos, vec3 normal, vec3 viewDir, float footprint, int primitive) {
    SurfaceInput i;
    i.worldPos = pos;
    i.worldNormal = normal;
    i.viewDir = viewDir;
    i.time = _Time;
    i.footprint = footprint;
    i.primitive = primitive;
    i.material = int(primitives[primitive].color.w);
    // local space as in classify.comp's mapScene: rotate, then divide by scale; normals use the inverse transpose
    mat3 r = eulerToMat3(primitives[primitive].rotation.xyz);
    vec3 scale = primitives[primitive].scale.xyz;
    i.objectPos = r * (pos - primitives[primitive].position.xyz) / scale;
    i.objectNormal = normalize((r * normal) * scale);
    return i;
}

// Shades a hit: the 8 voxel IDs around it give weights per primitive (Color Blend controls how hard the boundary is);
// the two heaviest primitives are shaded with their own material and blended.
vec3 shadeHit(vec3 pos, vec3 normal, vec3 viewDir, float footprint) {
    vec3 voxel = (pos - _WorldMin) / (_WorldMax - _WorldMin) * _Resolution;
    vec3 brickIndexF = clamp(floor(voxel / float(BRICK_CORE)), vec3(0.0), _GridResolution - 1.0);
    vec4 lookup = texelFetch(_LookupTex, ivec3(brickIndexF), 0);

    int primA = -1, primB = -1;
    float weightA = 0.0, weightB = 0.0;
    if (lookup.w >= 0.5) {
        vec3 local = voxel - brickIndexF * float(BRICK_CORE);
        ivec3 base = ivec3(clamp(floor(local), 0.0, float(BRICK_STORE - 2))); // base + 1 stays inside the brick (apron)
        vec3 f = clamp(local - vec3(base), 0.0, 1.0);
        f = clamp((f - 0.5) / max(_ColorBlend, 1e-4) + 0.5, 0.0, 1.0); // 1 = full trilinear, 0 = nearest texel
        ivec3 poolBase = ivec3(lookup.xyz) + base;

        int ids[8];
        float weights[8];
        for (int c = 0; c < 8; c++) {
            ivec3 o = ivec3(c & 1, (c >> 1) & 1, (c >> 2) & 1);
            vec3 w3 = mix(1.0 - f, f, vec3(o));
            uint id = texelFetch(_BrickIdTex, poolBase + o, 0).r;
            ids[c] = id < uint(primitiveCount) ? int(id) : -1;
            weights[c] = w3.x * w3.y * w3.z;
        }
        // total weight per distinct primitive, keep the two largest
        for (int c = 0; c < 8; c++) {
            if (ids[c] < 0) continue;
            bool seen = false;
            for (int k = 0; k < c; k++) if (ids[k] == ids[c]) seen = true;
            if (seen) continue;
            float total = 0.0;
            for (int k = c; k < 8; k++) if (ids[k] == ids[c]) total += weights[k];
            if (total > weightA) { primB = primA; weightB = weightA; primA = ids[c]; weightA = total; }
            else if (total > weightB) { primB = ids[c]; weightB = total; }
        }
    }
    if (primA < 0) { // no pool data (face of a solid brick): default material
        SurfaceData s = defaultSurface(0, makeInput(pos, normal, viewDir, footprint, 0));
        return lighting(s, pos, viewDir);
    }

    SurfaceInput inA = makeInput(pos, normal, viewDir, footprint, primA);
    SurfaceData s = evaluateMaterial(inA.material, inA);
    if (primB >= 0 && weightB > 0.01) {
        SurfaceInput inB = makeInput(pos, normal, viewDir, footprint, primB);
        s = blendSurface(s, evaluateMaterial(inB.material, inB), weightB / (weightA + weightB));
    }
    // a primitive's flat color still tints its material, as before materials existed
    s.albedo *= mix(primitives[primA].color.rgb, primB >= 0 ? primitives[primB].color.rgb : primitives[primA].color.rgb,
                    primB >= 0 ? weightB / (weightA + weightB) : 0.0);
    return lighting(s, pos, viewDir);
}

bool intersectAABB(vec3 ro, vec3 rd, out float tEnter, out float tExit) {
    vec3 invD = 1.0 / rd;
    vec3 t0 = (_WorldMin - ro) * invD;
    vec3 t1 = (_WorldMax - ro) * invD;
    vec3 tMin = min(t0, t1);
    vec3 tMax = max(t0, t1);
    tEnter = max(max(tMin.x, tMin.y), tMin.z);
    tExit  = min(min(tMax.x, tMax.y), tMax.z);
    return tExit >= max(tEnter, 0.0);
}

void main() {
    vec3 ro = _CameraPos;
    vec3 rd = rayDirection();

    float sceneDepthLinear = sceneRayDist();

    float tEnter, tExit;
    if (!intersectAABB(ro, rd, tEnter, tExit)) {
        FragColor = texture(_MainTex, uv); // Ray never touches the box
        return;
    }

    // Start at the box wall (or 0.0 if the camera is inside the box)
    float t = max(0.0, tEnter);

    // Stop marching if we exit the box, hit the far plane, or hit scene geometry
    float tMax = min(min(tExit, _MaxDist), sceneDepthLinear);

    bool hit = false;

    for (int i = 0; i < _MaxSteps; i++) {
        vec3 p = ro + rd * t;

        float skipDist;
        float d = sampleVolumeWithSkip(p, rd, skipDist);

        if (skipDist > 0.0) {
            t += skipDist; // empty brick — jump straight to its far face, not an SDF step
        } else {
            if (d < _SurfDist) { hit = true; break; }
            t += d;
        }

        if (t >= tMax) break;
    }

    if (hit) {
        vec3 pos = ro + rd * t;
        vec3 normal = calcNormal(pos);
        // world size of one pixel at the hit: angle between this pixel's ray and the next one's, times the distance
        vec2 pixel = 1.0 / vec2(textureSize(_MainTex, 0));
        vec2 ndc = uv * 2.0 - 1.0;
        vec4 nextView = _InvProj * vec4(ndc + vec2(0.0, 2.0 * pixel.y), -1.0, 1.0);
        nextView.xyz /= nextView.w;
        vec3 nextDir = normalize((_InvView * vec4(nextView.xyz, 0.0)).xyz);
        float footprint = t * acos(clamp(dot(rd, nextDir), -1.0, 1.0));
        FragColor = vec4(shadeHit(pos, normal, -rd, footprint), 1.0);
    } else {
        FragColor = texture(_MainTex, uv);
    }
}