#version 450

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vWorldNormal;
layout(location = 2) in vec4 vColor;
layout(location = 3) in float vRimPower;
layout(location = 4) in vec3 vCameraPos;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 N = normalize(vWorldNormal);
    vec3 V = normalize(vCameraPos - vWorldPos);
    float ndotv = abs(dot(N, V));
    float rim = pow(clamp(1.0 - ndotv, 0.0, 1.0), max(vRimPower, 1.0));
    // Wide soft band — readable on black stage without filling the whole actor.
    float edge = smoothstep(0.0, 0.85, rim);
    float alpha = vColor.a * mix(0.08, 1.0, edge);
    if (alpha < 0.05) {
        discard;
    }
    outColor = vec4(vColor.rgb, alpha);
}
