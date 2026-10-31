#version 440

// The picture's quad: xy in normalised device coordinates, uv in the
// 256x240 texture.
layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texcoord;
layout(location = 0) out vec2 v_texcoord;

layout(std140, binding = 0) uniform buf {
    mat4 clipSpaceCorr; // QRhi::clipSpaceCorrMatrix()
};

void main()
{
    v_texcoord = texcoord;
    gl_Position = clipSpaceCorr * vec4(position, 0.0, 1.0);
}
