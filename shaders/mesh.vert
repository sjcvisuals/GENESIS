#version 450

struct Light {
    vec4 position;
    vec4 direction;
    vec4 color;
    vec4 params;
};

struct MeshInstance {
    mat4 model;
    mat4 normalMatrix;
    vec4 boundsCenter;
    vec4 boundsExtent;
    vec4 color;
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

layout(set = 0, binding = 1) readonly buffer InstanceBuffer {
    MeshInstance instances[];
} uInstances;

// Must match MeshMaterialPushConstants (128 bytes). Vertex stage reads instanceBase only.
layout(push_constant) uniform Push {
    vec4 baseColor;
    vec4 emissive;
    vec4 materialParams;
    uint instanceBase;
    uint textureIndex;
    uint mrTextureIndex;
    uint packedMaps;
    float specularWeight;
    float specularIor;
    float coatWeight;
    float coatRoughness;
    vec3 specularColor;
    float coatIor;
    vec3 coatColor;
    float fuzzWeight;
    vec3 fuzzColor;
    float fuzzRoughness;
} uPush;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vWorldNormal;
layout(location = 2) out vec2 vUv;
layout(location = 3) out vec4 vInstanceColor;

void main() {
    MeshInstance inst = uInstances.instances[uPush.instanceBase + gl_InstanceIndex];
    vec4 worldPos = inst.model * vec4(inPosition, 1.0);
    gl_Position = uFrame.viewProj * worldPos;
    vWorldPos = worldPos.xyz;
    vWorldNormal = mat3(inst.normalMatrix) * inNormal;
    vUv = inUv;
    vInstanceColor = inst.color;
}
