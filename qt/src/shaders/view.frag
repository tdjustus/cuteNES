#version 440

layout(location = 0) in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D frame;

layout(std140, binding = 0) uniform buf {
    mat4 clipSpaceCorr;
    vec4 params; // x: scanlines on (1) or off (0)
};

// Scanlines keep the top of each NES line and darken its lower part, which
// stays visible at every scale of 2x or more.
const float kScanlineDim = 0.55;

void main()
{
    // The core's XRGB8888 words are B, G, R, X in memory, uploaded as RGBA8.
    vec4 c = texture(frame, v_texcoord);
    float dim = 1.0;
    if (params.x > 0.5 && fract(v_texcoord.y * 240.0) > 0.6) dim = kScanlineDim;
    fragColor = vec4(c.b * dim, c.g * dim, c.r * dim, 1.0);
}
