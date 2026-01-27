#version 460

#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

void main() {
    Vertex v = pushConstants.vertexBuffer.vertices[gl_VertexIndex];
    gl_Position = sceneData.lightViewproj[pushConstants.cascadeIndex] * pushConstants.worldMatrix * vec4(v.position, 1.0);
}