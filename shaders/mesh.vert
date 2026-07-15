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
    vec4 baseColor;
    vec4 emissive;
    vec4 materialParams;
} uPush;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vWorldNormal;
layout(location = 2) out vec2 vUv;

void main() {
    vec4 worldPos = uPush.model * vec4(inPosition, 1.0);
    gl_Position = uFrame.viewProj * worldPos;
    vWorldPos = worldPos.xyz;
    mat3 normalMatrix = mat3(transpose(inverse(uPush.model)));
    vWorldNormal = normalMatrix * inNormal;
    vUv = inUv;
}
