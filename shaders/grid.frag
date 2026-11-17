#version 450

layout(push_constant) uniform Push {
    mat4 model;
    vec4 color; // rgb line color, a = opacity
} uPush;

layout(location = 0) in vec3 vWorldPos;

layout(location = 0) out vec4 outColor;

void main() {
    // Meter-scale XZ ground grid for room/install sizing on a dark stage.
    // Major lines every 1 m; minor every 0.25 m.
    const float minor_cell = 0.25;
    const float major_cell = 1.0;

    vec2 xz = vWorldPos.xz;
    vec2 minor_uv = abs(fract(xz / minor_cell + 0.5) - 0.5);
    vec2 major_uv = abs(fract(xz / major_cell + 0.5) - 0.5);

    // Screen-stable line width via derivatives.
    vec2 minor_w = fwidth(xz / minor_cell);
    vec2 major_w = fwidth(xz / major_cell);
    float minor = 1.0 - smoothstep(0.0, 1.35, min(minor_uv.x / max(minor_w.x, 1e-5),
                                                   minor_uv.y / max(minor_w.y, 1e-5)));
    float major = 1.0 - smoothstep(0.0, 1.15, min(major_uv.x / max(major_w.x, 1e-5),
                                                   major_uv.y / max(major_w.y, 1e-5)));

    // Axis emphasis (world X / Z through origin) for orientation.
    float axis_x = 1.0 - smoothstep(0.0, 1.25, abs(xz.y) / max(fwidth(xz.y), 1e-5));
    float axis_z = 1.0 - smoothstep(0.0, 1.25, abs(xz.x) / max(fwidth(xz.x), 1e-5));
    float axes = max(axis_x, axis_z);

    float line = max(max(minor * 0.22, major * 0.95), axes);
    float dist = length(xz);
    float fade = 1.0 - smoothstep(14.0, 28.0, dist);
    float alpha = line * uPush.color.a * fade;
    if (alpha < 0.01) {
        discard;
    }

    // Slightly lift axis lines toward a cool readable accent on black.
    vec3 rgb = mix(uPush.color.rgb, uPush.color.rgb * 1.25 + vec3(0.05, 0.12, 0.14), axes * 0.55);
    outColor = vec4(rgb, alpha);
}
