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

vec3 sampleColor(vec3 p) {
    vec3 voxel = (p - _WorldMin) / (_WorldMax - _WorldMin) * _Resolution;

    vec3 brickIndexF = floor(voxel / float(BRICK_CORE));
    brickIndexF = clamp(brickIndexF, vec3(0.0), _GridResolution - 1.0);

    vec4 lookup = texelFetch(_LookupTex, ivec3(brickIndexF), 0);
    if (lookup.w < 0.5) return vec3(1.0); // no pool data — only happens on the face of a solid brick

    // ids can't be hardware-filtered, so filter the colors they point to by hand (trilinear over the 8 neighbours)
    vec3 local = voxel - brickIndexF * float(BRICK_CORE);
    ivec3 base = ivec3(clamp(floor(local), 0.0, float(BRICK_STORE - 2))); // base + 1 stays inside the brick thanks to the apron
    vec3 f = clamp(local - vec3(base), 0.0, 1.0);
    f = clamp((f - 0.5) / max(_ColorBlend, 1e-4) + 0.5, 0.0, 1.0); // 1 = full trilinear, 0 = nearest texel

    ivec3 poolBase = ivec3(lookup.xyz) + base;
    vec3 colorSum = vec3(0.0);
    float weightSum = 0.0;
    for (int i = 0; i < 8; i++) {
        ivec3 o = ivec3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        vec3 w3 = mix(1.0 - f, f, vec3(o));
        float w = w3.x * w3.y * w3.z;

        uint id = texelFetch(_BrickIdTex, poolBase + o, 0).r;
        if (id >= uint(primitiveCount)) continue; // no primitive, or primitives removed since the last bake
        colorSum += w * primitives[id].color.rgb;
        weightSum += w;
    }

    return weightSum > 0.0 ? colorSum / weightSum : vec3(1.0);
}

vec3 shade(vec3 pos, vec3 normal, vec3 col) {
    vec3 lightDir = normalize(vec3(0.6, 1.0, 0.4));
    float diff = max(dot(normal, lightDir), 0.0);
    return col * (0.15 + 0.85 * diff);
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
        vec3 color = shade(pos, normal, sampleColor(pos));
        FragColor = vec4(color, 1.0);
    } else {
        FragColor = texture(_MainTex, uv);
    }
}