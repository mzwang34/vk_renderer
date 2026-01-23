#version 460

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

struct Vertex {
    vec3 position;
    float uv_x;
    vec3 normal;
    float uv_y;
    vec4 color;
};

layout (buffer_reference, scalar) readonly buffer VertexBuffer {
    Vertex vertices[];
};

layout (push_constant) uniform GPUDrawPushConstants
{
    mat4 worldMatrix;
    VertexBuffer vertexBuffer;
} pushConstants;

// global scene data
layout (set = 0, binding = 0) uniform GPUSceneData
{
    mat4 view;
    mat4 proj;
    mat4 viewproj;
    mat4 lightViewproj;
    vec4 ambientColor;
    vec4 sunlightDirection;
    vec4 sunlightColor;
} sceneData;

void main() {
    Vertex v = pushConstants.vertexBuffer.vertices[gl_VertexIndex];
    gl_Position = sceneData.lightViewproj * pushConstants.worldMatrix * vec4(v.position, 1.0);
}