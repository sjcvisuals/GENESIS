#version 450

#extension GL_EXT_nonuniform_qualifier : require

// Realtime OpenPBR Surface shading (ASWF / Adobe / Autodesk).
// Implements the high-value lobes: base (metal/dielectric), specular, coat, fuzz, emission.
// Spec: https://academysoftwarefoundation.github.io/OpenPBR/

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
    vec4 cameraPos; // xyz eye, w exposure
    int lightCount;
    float iblIntensity;
    float fogDensity;
    float fogHeight;
    Light lights[8];
    vec4 ambientSky;
    vec4 ambientGround;
    vec4 fogColor;
    vec4 displayParams; // x = mode (0 Rec709, 1 sRGB, 2 OCIO LUT)
} uFrame;

layout(set = 0, binding = 1) readonly buffer InstanceBuffer {
    MeshInstance instances[];
} uInstances;

layout(set = 1, binding = 0) uniform sampler2D materialTextures[256];
layout(set = 2, binding = 0) uniform sampler3D displayLut;

layout(push_constant) uniform Push {
    vec4 baseColor;       // base_color.rgb + geometry_opacity
    vec4 emissive;        // emission * luminance, a = normal_scale
    vec4 materialParams;  // metalness, specular_roughness, alphaMode, alphaCutoff
    uint instanceBase;
    uint textureIndex;
    uint mrTextureIndex;
    uint packedMaps;
    // OpenPBR lobes (128-byte push block)
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

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vWorldNormal;
layout(location = 2) in vec2 vUv;
layout(location = 3) in vec4 vInstanceColor;

layout(location = 0) out vec4 outColor;

const float kPi = 3.14159265359;
const uint kNoMap = 0xFFFFu;

float distributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / max(kPi * denom * denom, 1e-4);
}

float geometrySchlickGGX(float NdotX, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotX / max(NdotX * (1.0 - k) + k, 1e-4);
}

float geometrySmith(float NdotV, float NdotL, float roughness) {
    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float iorToF0(float ior) {
    float r = (ior - 1.0) / max(ior + 1.0, 1e-4);
    return r * r;
}

// Charlie sheen NDF — efficient fuzz / cloth lobe used by OpenPBR-style realtime ports.
float distributionCharlie(float NdotH, float roughness) {
    float invR = 1.0 / max(roughness, 0.04);
    float cos2h = NdotH * NdotH;
    float sin2h = max(1.0 - cos2h, 0.0078125);
    return (2.0 + invR) * pow(sin2h, invR * 0.5) / (2.0 * kPi);
}

float geometrySheen(float NdotV, float NdotL, float roughness) {
    // Ashikhmin-style shadowing for sheen.
    float a = max(roughness, 0.04);
    float V = NdotL * (NdotV * (1.0 - a) + a);
    float L = NdotV * (NdotL * (1.0 - a) + a);
    return clamp(0.5 / max(V + L, 1e-4), 0.0, 1.0);
}

vec3 applyNormalMap(vec3 N, vec3 worldPos, vec2 uv, vec3 tangentNormal, float scale) {
    vec3 mapN = tangentNormal * 2.0 - 1.0;
    mapN.xy *= scale;
    mapN = normalize(mapN);

    vec3 dp1 = dFdx(worldPos);
    vec3 dp2 = dFdy(worldPos);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);

    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

    float invMax = inversesqrt(max(dot(T, T), dot(B, B)));
    T *= invMax;
    B *= invMax;
    mat3 TBN = mat3(T, B, N);
    return normalize(TBN * mapN);
}

vec3 evalMicrofacet(vec3 N, vec3 V, vec3 L, float roughness, vec3 F0, out vec3 F) {
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    vec3 H = normalize(V + L);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    float D = distributionGGX(NdotH, roughness);
    float G = geometrySmith(NdotV, NdotL, roughness);
    F = fresnelSchlick(VdotH, F0);
    return (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
}

// OpenPBR-inspired direct lighting: base diffuse/specular + optional coat + fuzz.
vec3 evalOpenPbrLight(Light light, vec3 N, vec3 V, vec3 worldPos, vec3 baseColor, float metalness,
                      float roughness, float specularWeight, float specularIor, vec3 specularColor,
                      float coatWeight, float coatRoughness, float coatIor, vec3 coatColor,
                      float fuzzWeight, float fuzzRoughness, vec3 fuzzColor) {
    int type = int(light.params.x);
    float range = light.params.y;
    float innerCos = light.params.z;
    float outerCos = light.params.w;

    vec3 L;
    float attenuation = 1.0;
    if (type == 0) {
        L = normalize(-light.direction.xyz);
    } else {
        vec3 toLight = light.position.xyz - worldPos;
        float dist = length(toLight);
        L = toLight / max(dist, 1e-4);
        if (range > 0.0) {
            float t = clamp(dist / range, 0.0, 1.0);
            attenuation = (1.0 - t * t);
            attenuation *= attenuation;
        }
        if (type == 2) {
            vec3 spotDir = normalize(light.direction.xyz);
            float cosAngle = dot(spotDir, -L);
            attenuation *= smoothstep(outerCos, innerCos, cosAngle);
        }
    }

    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) {
        return vec3(0.0);
    }

    vec3 radiance = light.color.rgb * light.color.a * attenuation;
    float NdotV = max(dot(N, V), 0.0);
    vec3 H = normalize(V + L);
    float VdotH = max(dot(V, H), 0.0);

    // Dielectric F0 from IOR; metals use base_color (face) with specular_color edge tint.
    float dielectricF0 = iorToF0(specularIor) * specularWeight;
    vec3 F0_diel = specularColor * dielectricF0;
    vec3 F0_metal = mix(baseColor, specularColor, pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0));
    vec3 F0 = mix(F0_diel, F0_metal, metalness);

    vec3 F;
    vec3 specular = evalMicrofacet(N, V, L, max(roughness, 0.04), F0, F);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metalness);
    vec3 diffuse = kD * baseColor / kPi;
    vec3 base = (diffuse + specular) * radiance * NdotL;

    // Coat: dielectric GGX layered over the base (OpenPBR coat slab).
    if (coatWeight > 1e-4) {
        float coatF0 = iorToF0(coatIor);
        vec3 coatF;
        vec3 coatSpec = evalMicrofacet(N, V, L, max(coatRoughness, 0.04), vec3(coatF0), coatF);
        // Absorption / darkening through the coat medium (coat_darkening authored via coatColor intensity).
        vec3 coatAbsorb = mix(vec3(1.0), coatColor, coatWeight);
        float fresnelCoat = coatF.x;
        base *= mix(vec3(1.0), coatAbsorb * (1.0 - fresnelCoat), coatWeight);
        base += coatSpec * radiance * NdotL * coatWeight;
    }

    // Fuzz: outermost Charlie sheen lobe (cloth / peach fuzz).
    if (fuzzWeight > 1e-4) {
        float NdotH = max(dot(N, H), 0.0);
        float D = distributionCharlie(NdotH, max(fuzzRoughness, 0.04));
        float G = geometrySheen(NdotV, NdotL, max(fuzzRoughness, 0.04));
        vec3 fuzz = fuzzColor * (D * G) * radiance * NdotL;
        base = mix(base, base + fuzz, fuzzWeight);
    }

    return base;
}

vec3 evalOpenPbrAmbient(vec3 N, vec3 V, vec3 baseColor, float metalness, float roughness,
                        float specularWeight, float specularIor, vec3 specularColor, float coatWeight,
                        float coatIor, float fuzzWeight, vec3 fuzzColor) {
    float hemi = N.y * 0.5 + 0.5;
    vec3 irradiance = mix(uFrame.ambientGround.rgb, uFrame.ambientSky.rgb, hemi);
    float NdotV = max(dot(N, V), 0.0);

    float dielectricF0 = iorToF0(specularIor) * specularWeight;
    vec3 F0 = mix(specularColor * dielectricF0, baseColor, metalness);
    vec3 F = fresnelSchlickRoughness(NdotV, F0, roughness);
    vec3 kD = (1.0 - F) * (1.0 - metalness);
    vec3 diffuse = kD * baseColor * irradiance;
    float specStrength = mix(0.35, 0.05, roughness * roughness);
    vec3 specular = F * mix(uFrame.ambientSky.rgb, irradiance, roughness) * specStrength;
    vec3 color = (diffuse + specular) * uFrame.iblIntensity;

    if (coatWeight > 1e-4) {
        float coatF0 = iorToF0(coatIor);
        vec3 coatF = fresnelSchlickRoughness(NdotV, vec3(coatF0), 0.1);
        color *= mix(1.0, 1.0 - coatF.x * 0.85, coatWeight);
        color += coatF * uFrame.ambientSky.rgb * (0.25 * coatWeight) * uFrame.iblIntensity;
    }
    if (fuzzWeight > 1e-4) {
        float rim = pow(1.0 - NdotV, 2.0);
        color += fuzzColor * rim * fuzzWeight * 0.35 * irradiance * uFrame.iblIntensity;
    }
    return color;
}

vec3 filmicCompress(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

float linearToRec709(float v) {
    v = clamp(v, 0.0, 1.0);
    if (v < 0.018) {
        return 4.5 * v;
    }
    return 1.099 * pow(v, 0.45) - 0.099;
}

float linearToSrgb(float v) {
    v = clamp(v, 0.0, 1.0);
    if (v <= 0.0031308) {
        return 12.92 * v;
    }
    return 1.055 * pow(v, 1.0 / 2.4) - 0.055;
}

vec3 applyDisplayTransform(vec3 linearColor) {
    int mode = int(uFrame.displayParams.x + 0.5);
    if (mode == 2) {
        return texture(displayLut, clamp(linearColor, 0.0, 1.0)).rgb;
    }
    vec3 c = filmicCompress(linearColor);
    if (mode == 1) {
        return vec3(linearToSrgb(c.r), linearToSrgb(c.g), linearToSrgb(c.b));
    }
    return vec3(linearToRec709(c.r), linearToRec709(c.g), linearToRec709(c.b));
}

void main() {
    vec4 baseSample = texture(materialTextures[nonuniformEXT(uPush.textureIndex)], vUv);
    vec3 baseColor = uPush.baseColor.rgb * baseSample.rgb * vInstanceColor.rgb;
    float alpha = uPush.baseColor.a * baseSample.a * vInstanceColor.a;

    int alphaMode = int(uPush.materialParams.z);
    float alphaCutoff = uPush.materialParams.w;
    if (alphaMode == 1 && alpha < alphaCutoff) {
        discard;
    }

    float metalness = uPush.materialParams.x;
    float roughness = uPush.materialParams.y;

    // Pre-tonemapped display textures / unlit particles: skip OpenPBR.
    if (alphaMode == 2 && metalness < 0.001 && roughness > 0.98) {
        if (alpha < 0.01) {
            discard;
        }
        const float exposure = max(uFrame.cameraPos.w, 0.01);
        outColor = vec4(applyDisplayTransform(baseColor * exposure), alpha);
        return;
    }

    if (alphaMode == 2 && alpha < 0.01) {
        discard;
    }
    if (uPush.mrTextureIndex != 0xFFFFFFFFu) {
        vec4 mr = texture(materialTextures[nonuniformEXT(uPush.mrTextureIndex)], vUv);
        metalness *= mr.b;
        roughness *= mr.g;
    }
    roughness = max(roughness, 0.04);

    vec3 N = normalize(vWorldNormal);
    uint normalIdx = uPush.packedMaps & kNoMap;
    if (normalIdx != kNoMap) {
        vec3 nSample = texture(materialTextures[nonuniformEXT(normalIdx)], vUv).xyz;
        N = applyNormalMap(N, vWorldPos, vUv, nSample, max(uPush.emissive.a, 0.0));
    }

    vec3 V = normalize(uFrame.cameraPos.xyz - vWorldPos);

    vec3 color = evalOpenPbrAmbient(N, V, baseColor, metalness, roughness, uPush.specularWeight,
                                    uPush.specularIor, uPush.specularColor, uPush.coatWeight,
                                    uPush.coatIor, uPush.fuzzWeight, uPush.fuzzColor);
    for (int i = 0; i < uFrame.lightCount; ++i) {
        color += evalOpenPbrLight(uFrame.lights[i], N, V, vWorldPos, baseColor, metalness, roughness,
                                  uPush.specularWeight, uPush.specularIor, uPush.specularColor,
                                  uPush.coatWeight, uPush.coatRoughness, uPush.coatIor, uPush.coatColor,
                                  uPush.fuzzWeight, uPush.fuzzRoughness, uPush.fuzzColor);
    }

    // Emission sits under the coat in OpenPBR — apply a light coat tint.
    vec3 emissive = uPush.emissive.rgb;
    uint emissiveIdx = (uPush.packedMaps >> 16) & kNoMap;
    if (emissiveIdx != kNoMap) {
        emissive *= texture(materialTextures[nonuniformEXT(emissiveIdx)], vUv).rgb;
    }
    if (uPush.coatWeight > 1e-4) {
        emissive *= mix(vec3(1.0), uPush.coatColor, uPush.coatWeight * 0.65);
    }
    color += emissive;

    if (uFrame.fogDensity > 1e-5) {
        float dist = length(uFrame.cameraPos.xyz - vWorldPos);
        float heightFactor = exp(-uFrame.fogHeight * max(vWorldPos.y, 0.0));
        float fogFactor = 1.0 - exp(-uFrame.fogDensity * dist * heightFactor);
        fogFactor = clamp(fogFactor, 0.0, 1.0);
        color = mix(color, uFrame.fogColor.rgb, fogFactor);
    }

    float exposure = max(uFrame.cameraPos.w, 0.01);
    color = applyDisplayTransform(color * exposure);

    if (alphaMode == 0) {
        alpha = 1.0;
    }

    outColor = vec4(color, alpha);
}
