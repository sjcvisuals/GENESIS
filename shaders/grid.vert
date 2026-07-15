#version 450

struct Light {
    vec4 position;
    vec4 direction;
    vec4 color;
    vec4 params;
};

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 viewProj;
    vec4 cameraPos;
    int lightCount;
    int _pad0;
    int _pad1;
    int _pad2;
    Light lights[8];
} uFrame;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 color;
} uPush;

layout(location = 0) in vec3 inPosition;

void main() {
    gl_Position = uFrame.viewProj * uPush.model * vec4(inPosition, 1.0);
}
