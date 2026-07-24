#version 450

#extension GL_EXT_nonuniform_qualifier : require

layout(set = 1, binding = 0) uniform sampler2D materialTextures[256];

// Must match MeshMaterialPushConstants (128 bytes). Depth pass uses alpha clip fields.
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

layout(location = 2) in vec2 vUv;

void main() {
    int alphaMode = int(uPush.materialParams.z);
    if (alphaMode == 1 || alphaMode == 2) {
        float alpha = uPush.baseColor.a * texture(materialTextures[nonuniformEXT(uPush.textureIndex)], vUv).a;
        if (alphaMode == 1 && alpha < uPush.materialParams.w) {
            discard;
        }
        if (alphaMode == 2 && alpha < 0.01) {
            discard;
        }
    }
}
