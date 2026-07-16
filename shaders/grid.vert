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
    float iblIntensity;
    float fogDensity;
    float fogHeight;
    Light lights[8];
    vec4 ambientSky;
    vec4 ambientGround;
    vec4 fogColor;
    vec4 displayParams;
} uFrame;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 color;
} uPush;

layout(location = 0) in vec3 inPosition;

void main() {
    gl_Position = uFrame.viewProj * uPush.model * vec4(inPosition, 1.0);
}
