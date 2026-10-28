// Sandstone strata: horizontal bands of lighter and darker rock that wobble a little, like layered sediment.
// Surface snippet contract (see march.frag): s arrives filled with the default material result; change what you need.
// Available: i.worldPos/objectPos, i.worldNormal/objectNormal, i.viewDir, i.time, i.footprint, i.material,
// materials[i.material], valueNoise(vec3), hash31(vec3), sampleTriplanar(...).
void surface(in SurfaceInput i, inout SurfaceData s) {
    vec3 p = i.objectPos;
    float y = p.y + (valueNoise(p * 0.12) - 0.5) * 4.0;             // wobbling layer heights
    float bands = sin(y * 1.1) * 0.5 + 0.5;                          // broad layers
    float fine = valueNoise(vec3(p.xz * 0.08, y * 3.0));             // thin layers inside them
    float shade = mix(0.6, 1.2, mix(bands, fine, 0.4));
    s.albedo = materials[i.material].tint.rgb * shade;
}
