#version 460

#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout (location = 0) out vec3 outUVW;

void main() {
    Vertex v = pushConstants.vertexBuffer.vertices[gl_VertexIndex];
    outUVW = v.position;
    outUVW.y *= -1.0;

    // remove translation
    mat4 viewNoTranslation = mat4(mat3(sceneData.view));
    vec4 pos = sceneData.proj * viewNoTranslation * vec4(v.position, 1.0);
    gl_Position = pos.xyww; // set depth to infinite far
}