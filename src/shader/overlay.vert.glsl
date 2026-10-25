#version 450

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec4 colour;

layout(push_constant) uniform Transform
{
    vec2 scale;
    vec2 translate;
} transform;

layout(location = 0) out vec4 fragmentColour;
layout(location = 1) out vec2 fragmentUv;

void main()
{
    fragmentColour = colour;
    fragmentUv     = uv;
    gl_Position    = vec4(position * transform.scale + transform.translate, 0.0, 1.0);
}
