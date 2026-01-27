#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

#define NUM_CASCADES 4

struct Vertex {
    vec3 position;
    float uv_x;
    vec3 normal;
    float uv_y;
    vec4 color;
};

layout(buffer_reference, scalar) readonly buffer VertexBuffer {
    Vertex vertices[];
};

layout (set = 0, binding = 0, scalar) uniform GPUSceneData
{
    mat4 view;
    mat4 proj;
    mat4 viewproj;
    mat4 lightViewproj[NUM_CASCADES];
    float cascadeDistances[NUM_CASCADES];
    vec4 ambientColor;
    vec4 sunlightDirection;
    vec4 sunlightColor;
} sceneData;

layout (push_constant, scalar) uniform GPUDrawPushConstants
{
    mat4 worldMatrix;
    VertexBuffer vertexBuffer;
    int cascadeIndex;
} pushConstants;