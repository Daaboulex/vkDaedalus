#version 450

layout(set = 0, binding = 0) uniform sampler2D overlayTexture;

layout(location = 0) in vec4 fragmentColour;
layout(location = 1) in vec2 fragmentUv;

layout(location = 0) out vec4 outColour;

void main()
{
    outColour = fragmentColour * texture(overlayTexture, fragmentUv);
}
