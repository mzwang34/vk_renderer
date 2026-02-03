#version 460

#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec3 outColor;
layout (location = 2) out vec2 outUV;
layout (location = 3) out vec3 outLightVec;
layout (location = 4) out vec4 outFragPosWorld;
layout (location = 5) out vec4 outFragPosView;


void main() {
    Vertex v = pushConstants.vertexBuffer.vertices[gl_VertexIndex];
    gl_Position = sceneData.viewproj * pushConstants.worldMatrix * vec4(v.position, 1.0);

	outNormal = mat3(pushConstants.worldMatrix) * v.normal;
	outColor = v.color.xyz;
	outUV = vec2(v.uv_x, v.uv_y);
	outLightVec = normalize(-sceneData.sunlightDirection.xyz);
    outFragPosWorld = pushConstants.worldMatrix * vec4(v.position, 1.0);
    outFragPosView = sceneData.view * outFragPosWorld;
}