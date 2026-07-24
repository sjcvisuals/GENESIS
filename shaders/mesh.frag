#version 450

#extension GL_EXT_nonuniform_qualifier : require

// Realtime OpenPBR Surface (ASWF / Adobe / Autodesk).
// Spec: https://academysoftwarefoundation.github.io/OpenPBR/
// Optional lobes (transmission / SSS / thin-film / coat / fuzz) are weight-gated.

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
    // Extended OpenPBR (offset 128)
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

float distributionCharlie(float NdotH, float roughness) {
    float invR = 1.0 / max(roughness, 0.04);
    float cos2h = NdotH * NdotH;
    float sin2h = max(1.0 - cos2h, 0.0078125);
    return (2.0 + invR) * pow(sin2h, invR * 0.5) / (2.0 * kPi);
}

float geometrySheen(float NdotV, float NdotL, float roughness) {
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

// Cheap thin-film iridescence — Airy phase modulation of F0 (weight-gated).
vec3 evalThinFilmF0(vec3 F0, float cosTheta, float thicknessUm, float filmIor, float weight) {
    if (weight < 1e-4) {
        return F0;
    }
    float eta = max(filmIor, 1.01);
    float sin2t = (1.0 - cosTheta * cosTheta) / (eta * eta);
    float cosBeta = sqrt(max(1.0 - sin2t, 0.0));
    float tNm = max(thicknessUm, 0.0) * 1000.0;
    vec3 lambda = vec3(650.0, 510.0, 475.0);
    vec3 phase = (4.0 * kPi * eta * tNm * cosBeta) / lambda;
    vec3 irid = 0.5 + 0.5 * cos(phase);
    return mix(F0, mix(F0, irid, 0.85), weight);
}

// Oren-Nayar-ish diffuse response from base_diffuse_roughness (cheap).
float diffuseResponse(float NdotL, float NdotV, float diffuseRoughness) {
    if (diffuseRoughness < 1e-4) {
        return NdotL;
    }
    float sigma = diffuseRoughness;
    float sigma2 = sigma * sigma;
    float A = 1.0 - (sigma2 / (2.0 * (sigma2 + 0.33)));
    float B = 0.45 * sigma2 / (sigma2 + 0.09);
    float cosPhi = 1.0; // omit azimuth term for speed
    float sinAlpha = sqrt(max(1.0 - min(NdotL, NdotV) * min(NdotL, NdotV), 0.0));
    float tanBeta = sqrt(max(1.0 - max(NdotL, NdotV) * max(NdotL, NdotV), 0.0)) /
                    max(max(NdotL, NdotV), 1e-4);
    return max(NdotL * (A + B * cosPhi * sinAlpha * tanBeta), 0.0);
}

// Wrap-lighting SSS — no diffusion profile / no screen-space blur.
vec3 evalSubsurface(vec3 N, vec3 L, vec3 V, vec3 albedo, float weight, float radius,
                     vec3 radiusScale, float aniso) {
    if (weight < 1e-4) {
        return vec3(0.0);
    }
    float wrap = clamp(radius * 0.35, 0.0, 1.0);
    float NdotL = dot(N, L);
    float wrapped = clamp((NdotL + wrap) / (1.0 + wrap), 0.0, 1.0);
    // Mild forward/back scatter tilt from anisotropy (Henye–Greenstein stand-in).
    float phase = 1.0 + aniso * 0.35 * NdotL;
    vec3 sssAlbedo = mix(albedo, uPush.subsurfaceColor, 0.65) * radiusScale;
    return sssAlbedo * (wrapped * phase) / kPi * weight;
}

vec3 beerExtinction(vec3 transmissionColor, float depth, float pathLen) {
    if (depth < 1e-5) {
        return transmissionColor;
    }
    vec3 sigmaA = -log(max(transmissionColor, vec3(1e-4))) / depth;
    return exp(-sigmaA * max(pathLen, 0.0));
}

vec3 evalTransmission(vec3 N, vec3 V, float metalness, float NdotV, vec3 F,
                      float iblIntensity) {
    float T = uPush.transmissionWeight * (1.0 - metalness);
    if (T < 1e-4) {
        return vec3(0.0);
    }
    float fresnelBlock = max(F.r, max(F.g, F.b));
    float transmit = (1.0 - fresnelBlock) * T;

    bool thin = uPush.thinWalled > 0.5;
    float pathLen = thin ? 0.0 : max(uPush.transmissionDepth * 0.65, 0.0);
    vec3 beer = beerExtinction(uPush.transmissionColor, uPush.transmissionDepth, pathLen);

    // Fake refracted ambient (no SSR / no mesh refraction — stays cheap).
    vec3 refrDir = thin ? -V : refract(-V, N, 1.0 / max(uPush.specularIor, 1.01));
    if (dot(refrDir, refrDir) < 1e-6) {
        refrDir = -V;
    }
    float hemi = refrDir.y * 0.5 + 0.5;
    vec3 transmittedAmbient = mix(uFrame.ambientGround.rgb, uFrame.ambientSky.rgb, hemi);

    float scatterAmt = max(max(uPush.transmissionScatter.r, uPush.transmissionScatter.g),
                           uPush.transmissionScatter.b);
    if (scatterAmt > 1e-4) {
        float g = uPush.transmissionScatterAnisotropy;
        float scatterMix = scatterAmt * (0.35 + 0.25 * abs(g));
        transmittedAmbient = mix(transmittedAmbient, uPush.transmissionScatter * transmittedAmbient,
                                 clamp(scatterMix, 0.0, 1.0));
    }

    // Cheap dispersion: slight RGB phase of transmitted color when scale > 0.
    if (uPush.transmissionDispersionScale > 1e-4) {
        float abbe = max(uPush.transmissionDispersionAbbe, 1.0);
        float spread = uPush.transmissionDispersionScale * (30.0 / abbe);
        vec3 disp = vec3(1.0 + spread, 1.0, 1.0 - spread * 0.8);
        beer *= mix(vec3(1.0), disp, uPush.transmissionDispersionScale);
    }

    return transmittedAmbient * beer * transmit * iblIntensity;
}

vec3 evalOpenPbrLight(Light light, vec3 N, vec3 V, vec3 worldPos, vec3 baseColor, float metalness,
                      float roughness, float opaqueWeight) {
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
    float NdotV = max(dot(N, V), 0.0);
    vec3 H = normalize(V + L);
    float VdotH = max(dot(V, H), 0.0);
    vec3 radiance = light.color.rgb * light.color.a * attenuation;

    float dielectricF0 = iorToF0(uPush.specularIor) * uPush.specularWeight;
    vec3 F0_diel = uPush.specularColor * dielectricF0;
    vec3 F0_metal = mix(baseColor, uPush.specularColor, pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0));
    vec3 F0 = mix(F0_diel, F0_metal, metalness);
    F0 = evalThinFilmF0(F0, VdotH, uPush.thinFilmThickness, uPush.thinFilmIor, uPush.thinFilmWeight);

    vec3 F;
    vec3 specular = evalMicrofacet(N, V, L, max(roughness, 0.04), F0, F);
    float diffResp = diffuseResponse(NdotL, NdotV, uPush.baseDiffuseRoughness);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metalness) * opaqueWeight;
    vec3 diffuse = kD * baseColor / kPi * diffResp;

    // Mix in wrap SSS under the opaque dielectric path (weight-gated).
    if (uPush.subsurfaceWeight > 1e-4 && metalness < 0.999) {
        vec3 sss = evalSubsurface(N, L, V, baseColor, 1.0, uPush.subsurfaceRadius,
                                  uPush.subsurfaceRadiusScale, uPush.subsurfaceScatterAnisotropy);
        float sssW = uPush.subsurfaceWeight * opaqueWeight;
        diffuse = mix(diffuse, sss, sssW);
    }

    vec3 base = (diffuse + specular * NdotL) * radiance;

    if (uPush.coatWeight > 1e-4) {
        float coatF0 = iorToF0(uPush.coatIor);
        vec3 coatF;
        vec3 coatSpec = evalMicrofacet(N, V, L, max(uPush.coatRoughness, 0.04), vec3(coatF0), coatF);
        vec3 coatAbsorb = mix(vec3(1.0), uPush.coatColor, uPush.coatWeight);
        float dark = mix(1.0, uPush.coatDarkening, uPush.coatWeight);
        base *= mix(vec3(1.0), coatAbsorb * (1.0 - coatF.x) * dark, uPush.coatWeight);
        base += coatSpec * radiance * NdotL * uPush.coatWeight;
    }

    if (uPush.fuzzWeight > 1e-4 && NdotL > 0.0) {
        float NdotH = max(dot(N, H), 0.0);
        float D = distributionCharlie(NdotH, max(uPush.fuzzRoughness, 0.04));
        float G = geometrySheen(NdotV, NdotL, max(uPush.fuzzRoughness, 0.04));
        vec3 fuzz = uPush.fuzzColor * (D * G) * radiance * NdotL;
        base = mix(base, base + fuzz, uPush.fuzzWeight);
    }

    return base;
}

vec3 evalOpenPbrAmbient(vec3 N, vec3 V, vec3 baseColor, float metalness, float roughness,
                        float opaqueWeight) {
    float hemi = N.y * 0.5 + 0.5;
    vec3 irradiance = mix(uFrame.ambientGround.rgb, uFrame.ambientSky.rgb, hemi);
    float NdotV = max(dot(N, V), 0.0);

    float dielectricF0 = iorToF0(uPush.specularIor) * uPush.specularWeight;
    vec3 F0 = mix(uPush.specularColor * dielectricF0, baseColor, metalness);
    F0 = evalThinFilmF0(F0, NdotV, uPush.thinFilmThickness, uPush.thinFilmIor, uPush.thinFilmWeight);
    vec3 F = fresnelSchlickRoughness(NdotV, F0, roughness);
    vec3 kD = (1.0 - F) * (1.0 - metalness) * opaqueWeight;
    vec3 diffuse = kD * baseColor * irradiance;

    if (uPush.subsurfaceWeight > 1e-4 && metalness < 0.999) {
        vec3 sss = mix(baseColor, uPush.subsurfaceColor, 0.65) * uPush.subsurfaceRadiusScale;
        float wrapHemi = hemi * 0.65 + 0.35;
        diffuse = mix(diffuse, sss * irradiance * wrapHemi, uPush.subsurfaceWeight * opaqueWeight);
    }

    float specStrength = mix(0.35, 0.05, roughness * roughness);
    vec3 specular = F * mix(uFrame.ambientSky.rgb, irradiance, roughness) * specStrength;
    vec3 color = (diffuse + specular) * uFrame.iblIntensity;

    color += evalTransmission(N, V, metalness, NdotV, F, uFrame.iblIntensity);

    if (uPush.coatWeight > 1e-4) {
        float coatF0 = iorToF0(uPush.coatIor);
        vec3 coatF = fresnelSchlickRoughness(NdotV, vec3(coatF0), 0.1);
        float dark = mix(1.0, uPush.coatDarkening, uPush.coatWeight);
        color *= mix(1.0, (1.0 - coatF.x * 0.85) * dark, uPush.coatWeight);
        color += coatF * uFrame.ambientSky.rgb * (0.25 * uPush.coatWeight) * uFrame.iblIntensity;
    }
    if (uPush.fuzzWeight > 1e-4) {
        float rim = pow(1.0 - NdotV, 2.0);
        color += uPush.fuzzColor * rim * uPush.fuzzWeight * 0.35 * irradiance * uFrame.iblIntensity;
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

    // Opaque dielectric fraction after transmission mix (OpenPBR tree).
    float opaqueWeight = 1.0 - uPush.transmissionWeight * (1.0 - metalness);

    vec3 color = evalOpenPbrAmbient(N, V, baseColor, metalness, roughness, opaqueWeight);
    for (int i = 0; i < uFrame.lightCount; ++i) {
        color += evalOpenPbrLight(uFrame.lights[i], N, V, vWorldPos, baseColor, metalness, roughness,
                                  opaqueWeight);
    }

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

    // Transmission drives transparency (thin-walled stronger).
    if (uPush.transmissionWeight > 1e-4 && metalness < 0.999) {
        float t = uPush.transmissionWeight * (uPush.thinWalled > 0.5 ? 0.85 : 0.45);
        alpha = mix(alpha, alpha * (1.0 - t), 1.0);
        alpha = max(alpha, 0.04);
        alphaMode = 2;
    }

    if (alphaMode == 0) {
        alpha = 1.0;
    }

    outColor = vec4(color, alpha);
}
