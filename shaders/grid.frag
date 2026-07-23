#version 450

layout(push_constant) uniform Push {
    mat4 model;
    vec4 color; // rgb line color, a = opacity
} uPush;

layout(location = 0) in vec3 vWorldPos;

layout(location = 0) out vec4 outColor;

void main() {
    // Procedural XZ ground grid — avoids line-list rasterization bugs that
    // filled the ground plane as solid triangles on some Vulkan drivers.
    const float major_every = 4.0;
    const float cell = 1.0;

    vec2 xz = vWorldPos.xz;
    vec2 cell_uv = abs(fract(xz / cell + 0.5) - 0.5);
    vec2 major_uv = abs(fract(xz / (cell * major_every) + 0.5) - 0.5);

    // Screen-stable line width via derivatives.
    vec2 cell_w = fwidth(xz / cell);
    vec2 major_w = fwidth(xz / (cell * major_every));
    float minor = 1.0 - smoothstep(0.0, 1.5, min(cell_uv.x / max(cell_w.x, 1e-5), cell_uv.y / max(cell_w.y, 1e-5)));
    float major = 1.0 - smoothstep(0.0, 1.25, min(major_uv.x / max(major_w.x, 1e-5), major_uv.y / max(major_w.y, 1e-5)));

    float line = max(minor * 0.45, major);
    // Fade with distance so the grid stays quiet in the distance.
    float dist = length(vWorldPos.xz);
    float fade = 1.0 - smoothstep(8.0, 18.0, dist);
    float alpha = line * uPush.color.a * fade;
    if (alpha < 0.01) {
        discard;
    }

    outColor = vec4(uPush.color.rgb, alpha);
}
