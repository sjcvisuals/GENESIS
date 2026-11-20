#version 450

layout(push_constant) uniform Push {
    vec2 screen_size; // pixels
    float scale;
    float _pad;
} uPush;

layout(location = 0) in vec2 inPos;   // unscaled pixel coords (top-left origin)
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 vColor;

void main() {
    vec2 px = inPos * uPush.scale;
    vec2 ndc = vec2(px.x / uPush.screen_size.x, px.y / uPush.screen_size.y) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
    vColor = inColor;
}
