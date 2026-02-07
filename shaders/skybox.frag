#version 460

layout (location = 0) in vec3 inUVW;

layout (location = 0) out vec4 outFragColor;

layout(set = 0, binding = 2) uniform samplerCube skyboxTexture;

void main() 
{
    outFragColor = texture(skyboxTexture, inUVW);
}