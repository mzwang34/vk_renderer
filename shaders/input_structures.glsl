layout(set = 0, binding = 0) uniform SceneData {
	mat4 view;
	mat4 proj;
	mat4 viewproj;
	vec4 ambientColor;
	vec4 sunlightDirection;
	vec4 sunlightColor;
} sceneData;

#extension GL_EXT_nonuniform_qualifier : enable
layout(set = 1, binding = 0) uniform sampler2D globalTextures[];

layout(set = 2, binding = 0) uniform GLTFMaterialData{   
	vec4 colorFactors;
	vec4 metal_rough_factors;
	int albedoID;
	int normalID;
	int metalRoughID;
	int padding;
} materialData;