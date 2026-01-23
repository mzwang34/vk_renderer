#version 460

#extension GL_EXT_nonuniform_qualifier : require

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec3 inLightVec;
layout (location = 4) in vec4 inShadowCoord;

layout (location = 0) out vec4 outFragColor;

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

layout (set = 0, binding = 1) uniform sampler2D shadowMap;

layout (set = 1, binding = 0) uniform sampler2D globalTextures[];

layout (set = 2, binding = 0) uniform MaterialConstants
{
	vec4 colorFactors;
	vec4 metal_rough_factors;
	int albedoID;
	int normalID;
	int metalRoughID;
	int padding;
} materialData;

float compute_shadow(vec4 shadowCoord) {
    float shadow = 1.0;
    float bias = 0.001;
    float depth = texture(shadowMap, shadowCoord.xy).r;
    float cur_depth = shadowCoord.z;
    if (depth + bias < cur_depth) shadow = 0.0;

    return shadow;
}

void main() {
    float shadow = 1.0;
    bool enableShadow = sceneData.sunlightColor.w > 0.5 ? true : false;
    if (enableShadow)
        shadow = compute_shadow(inShadowCoord);

    // vec4 texColor = texture(globalTextures[nonuniformEXT(materialData.albedoID)], inUV);
    // vec3 color = inColor * texColor.xyz * materialData.colorFactors.xyz;
    vec3 color = inColor;

    vec3 N = normalize(inNormal);
    vec3 L = normalize(inLightVec);
    vec3 diffuse = max(0.0, dot(N, L)) * color * sceneData.sunlightColor.xyz;

    vec3 ambient = sceneData.ambientColor.xyz;

    outFragColor = vec4(diffuse * shadow + ambient, 1.0);
}