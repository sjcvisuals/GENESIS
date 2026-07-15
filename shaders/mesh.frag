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

layout(set = 1, binding = 0) uniform sampler2D baseColorTex;

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vWorldNormal;
layout(location = 2) in vec2 vUv;

layout(location = 0) out vec4 outColor;

const float kPi = 3.14159265359;

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

vec3 evalLight(Light light, vec3 N, vec3 V, vec3 worldPos, float roughness, float metallic, vec3 albedo) {
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
            attenuation = 1.0 - t * t;
        }
        if (type == 2) {
            vec3 spotDir = normalize(light.direction.xyz);
            float cosAngle = dot(spotDir, -L);
            attenuation *= smoothstep(outerCos, innerCos, cosAngle);
        }
    }

    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    if (NdotL <= 0.0) {
        return vec3(0.0);
    }

    vec3 radiance = light.color.rgb * light.color.a;
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    float D = distributionGGX(NdotH, roughness);
    float G = geometrySmith(NdotV, NdotL, roughness);
    vec3 F = fresnelSchlick(VdotH, F0);

    vec3 numerator = D * G * F;
    float denominator = 4.0 * max(NdotV * NdotL, 1e-4);
    vec3 specular = numerator / max(denominator, 1e-4);

    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / kPi;

    return (diffuse + specular) * radiance * NdotL * attenuation;
}

void main() {
    vec3 N = normalize(vWorldNormal);
    vec3 V = normalize(uFrame.cameraPos.xyz - vWorldPos);

    float metallic = uPush.materialParams.x;
    float roughness = max(uPush.materialParams.y, 0.04);
    int alphaMode = int(uPush.materialParams.z);
    float alphaCutoff = uPush.materialParams.w;

    vec3 albedo = uPush.baseColor.rgb * texture(baseColorTex, vUv).rgb;
    vec3 color = vec3(0.03) * albedo;

    for (int i = 0; i < uFrame.lightCount; ++i) {
        color += evalLight(uFrame.lights[i], N, V, vWorldPos, roughness, metallic, albedo);
    }
    color += uPush.emissive.rgb * uPush.emissive.a;

    float alpha = uPush.baseColor.a * texture(baseColorTex, vUv).a;
    if (alphaMode == 1 && alpha < alphaCutoff) {
        discard;
    }

    outColor = vec4(color, alpha);
}
