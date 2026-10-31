#version 440

layout(location = 0) in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D frame;

void main()
{
    // The core's XRGB8888 words are B, G, R, X in memory, uploaded as RGBA8.
    vec4 c = texture(frame, v_texcoord);
    fragColor = vec4(c.b, c.g, c.r, 1.0);
}
