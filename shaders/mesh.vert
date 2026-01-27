#version 460

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec3 outColor;
layout (location = 2) out vec2 outUV;
layout (location = 3) out vec3 outLightVec;
// layout (location = 4) out vec4 outShadowCoord;
layout (location = 4) out vec4 outFragPosWorld;
layout (location = 5) out vec4 outFragPosView;

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

layout (push_constant) uniform GPUDrawPushConstants
{
    mat4 worldMatrix;
    VertexBuffer vertexBuffer;
    int cascadeIndex;
} pushConstants;

// global scene data
layout (set = 0, binding = 0) uniform GPUSceneData
{
    mat4 view;
    mat4 proj;
    mat4 viewproj;
    mat4 lightViewproj[4];
    vec4 cascadeDistances;
    vec4 ambientColor;
    vec4 sunlightDirection;
    vec4 sunlightColor;
} sceneData;

// column-major
// const mat4 biasMat = mat4(
// 	0.5, 0.0, 0.0, 0.0,
// 	0.0, 0.5, 0.0, 0.0,
// 	0.0, 0.0, 1.0, 0.0,
// 	0.5, 0.5, 0.0, 1.0 );

void main() {
    Vertex v = pushConstants.vertexBuffer.vertices[gl_VertexIndex];
    gl_Position = sceneData.viewproj * pushConstants.worldMatrix * vec4(v.position, 1.0);

	outNormal = (pushConstants.worldMatrix * vec4(v.normal, 1.0)).xyz;
	outColor = v.color.xyz;
	outUV = vec2(v.uv_x, v.uv_y);
	outLightVec = normalize(-sceneData.sunlightDirection.xyz);
	// outShadowCoord = (biasMat * sceneData.lightViewproj * pushConstants.worldMatrix * vec4(v.position, 1.0));
    outFragPosWorld = pushConstants.worldMatrix * vec4(v.position, 1.0);
    outFragPosView = sceneData.view * outFragPosWorld;
}