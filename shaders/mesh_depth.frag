#version 450

#extension GL_EXT_nonuniform_qualifier : require

layout(set = 1, binding = 0) uniform sampler2D materialTextures[256];

layout(push_constant) uniform Push {
    vec4 baseColor;
    vec4 emissive;
    vec4 materialParams;
    uint instanceBase;
    uint textureIndex;
    uint mrTextureIndex;
    uint packedMaps;
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
