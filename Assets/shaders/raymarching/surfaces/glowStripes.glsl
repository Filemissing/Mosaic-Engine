// Example: glowing stripes that scroll up the object over time (emission is added on top of the lighting).
void surface(in SurfaceInput i, inout SurfaceData s) {
    float stripe = smoothstep(0.8, 0.95, fract(i.objectPos.y * 1.5 - i.time * 0.4));
    s.emission += vec3(0.2, 0.8, 1.0) * stripe * 1.5;
}
