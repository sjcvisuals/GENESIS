#version 450

// Selection edge overlay: slight normal expand + fresnel rim in the fragment shader.

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
    float transmissionWeight;
    float transmissionDepth;
    float subsurfaceWeight;
    float subsurfaceRadius;
    vec3 transmissionColor;
    float thinFilmWeight;
    vec3 subsurfaceColor;
    float thinFilmThickness;
    vec3 transmissionScatter;
    float thinFilmIor;
    vec3 subsurfaceRadiusScale;
    float transmissionScatterAnisotropy;
    float transmissionDispersionScale;
    float transmissionDispersionAbbe;
    float subsurfaceScatterAnisotropy;
    float thinWalled;
    float baseDiffuseRoughness;
    float coatDarkening;
    float pad0;
    float pad1;
    float pad2;
    float pad3;
    float pad4;
    float pad5;
} uPush;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vWorldNormal;
layout(location = 2) out vec4 vColor;
layout(location = 3) out float vRimPower;
layout(location = 4) out vec3 vCameraPos;

void main() {
    MeshInstance inst = uInstances.instances[uPush.instanceBase + gl_InstanceIndex];
    vec3 n = mat3(inst.normalMatrix) * inNormal;
    float nLen2 = dot(n, n);
    vec3 worldN = nLen2 > 1e-8 ? normalize(n) : vec3(0.0, 1.0, 0.0);

    vec3 worldPos = (inst.model * vec4(inPosition, 1.0)).xyz;
    // Slight shell expand so the rim band has thickness on screen.
    float dist = max(length(uFrame.cameraPos.xyz - worldPos), 0.4);
    worldPos += worldN * clamp(0.004 * dist, 0.008, 0.08);

    vWorldPos = worldPos;
    vWorldNormal = worldN;
    vColor = uPush.baseColor;
    vRimPower = max(uPush.materialParams.x, 1.0);
    vCameraPos = uFrame.cameraPos.xyz;
    gl_Position = uFrame.viewProj * vec4(worldPos, 1.0);
}
