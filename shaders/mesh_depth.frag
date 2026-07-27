#version 450

#extension GL_EXT_nonuniform_qualifier : require

layout(set = 1, binding = 0) uniform sampler2D materialTextures[256];

// Must match MeshMaterialPushConstants (256 bytes). Depth pass uses alpha clip fields.
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

layout(location = 2) in vec2 vUv;

void main() {
    int alphaMode = int(uPush.materialParams.z);

    // Blended / volume materials must not write depth in the prepass.
    // Fluid volumes especially: their base texture is a Z-slice atlas — sampling it
    // with face UVs carves stair-step holes (and can paint the atlas onto faces).
    if (alphaMode == 2 || alphaMode == 3) {
        discard;
    }

    // Cutout / mask materials still participate so foliage etc. get correct depth.
    if (alphaMode == 1) {
        float alpha = uPush.baseColor.a * texture(materialTextures[nonuniformEXT(uPush.textureIndex)], vUv).a;
        if (alpha < uPush.materialParams.w) {
            discard;
        }
    }
}
