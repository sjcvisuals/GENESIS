#version 450

layout(push_constant) uniform Push {
    mat4 model;
    vec4 color;
} uPush;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = uPush.color;
}
