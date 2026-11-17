#version 450

layout(location = 0) in vec4 vColor;
layout(location = 0) out vec4 outColor;

void main() {
    // Solid white silhouette edge (inverted-hull back faces only).
    if (vColor.a < 0.01) {
        discard;
    }
    outColor = vColor;
}
