#version 460

#extension GL_EXT_nonuniform_qualifier : require

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec3 inLightVec;
// layout (location = 4) in vec4 inShadowCoord;
layout (location = 4) in vec4 inFragPosWorld;
layout (location = 5) in vec4 inFragPosView;

layout (location = 0) out vec4 outFragColor;

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

// layout (set = 0, binding = 1) uniform sampler2D shadowMap;
layout (set = 0, binding = 1) uniform sampler2DArray shadowMap;

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

const float bias = 0.0005;
float compute_shadow(vec4 shadowCoord, int layer) {
    float shadow = 0.0;
    float depth = texture(shadowMap, vec3(shadowCoord.xy, layer)).r;
    float cur_depth = shadowCoord.z;
    if (depth + bias < cur_depth) shadow = 1.0;

    return shadow;
}

float PCF(vec4 shadowCoord, int layer) {
    float shadow = 0.0;
    vec2 texSize = 1.0 / textureSize(shadowMap, 0).xy;
    float cur_depth = shadowCoord.z;
    for (int i = -1; i <= 1; ++i) {
        for (int j = -1; j <= 1; ++j) {
            float pcfDepth = texture(shadowMap, vec3(shadowCoord.xy + vec2(i, j) * texSize, layer)).r;
            shadow += cur_depth - bias > pcfDepth? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}

#define NUM_SAMPLES_BLOCKER_SEARCH 16
#define NUM_SAMPLES_PCF 16
#define NEAR_PLANE 0.1
#define LIGHT_WORLD_SIZE 2.0
#define LIGHT_FRUSTRUM_WIDTH 200.0
#define LIGHT_SIZE_UV (LIGHT_WORLD_SIZE / LIGHT_FRUSTRUM_WIDTH)

vec2 poissonDisk[16] = {
    vec2( -0.94201624, -0.39906216 ), 
    vec2( 0.94558609, -0.76890725 ), 
    vec2( -0.094184101, -0.92938870 ), 
    vec2( 0.34495938, 0.29387760 ), 
    vec2( -0.91588581, 0.45771432 ), 
    vec2( -0.81544232, -0.87912464 ), 
    vec2( -0.38277543, 0.27676845 ), 
    vec2( 0.97484398, 0.75648379 ), 
    vec2( 0.44323325, -0.97511554 ), 
    vec2( 0.53742981, -0.47373420 ), 
    vec2( -0.26496911, -0.41893023 ), 
    vec2( 0.79197514, 0.19090188 ), 
    vec2( -0.24188840, 0.99706507 ), 
    vec2( -0.81409955, 0.91437590 ), 
    vec2( 0.19984126, 0.78641367 ), 
    vec2( 0.14383161, -0.14100790 )
};

float findBlocker(vec4 shadowCoord, int layer) {
    float zReceiver = shadowCoord.z;
    float blockerSum = 0;
    int numBlockers = 0;
    float searchWidth = LIGHT_SIZE_UV * (zReceiver - NEAR_PLANE) / zReceiver;
    for (int i = 0; i < NUM_SAMPLES_BLOCKER_SEARCH; ++i) {
        float z = texture(shadowMap, vec3(shadowCoord.xy + poissonDisk[i] * searchWidth, layer)).r;
        if (z + bias < zReceiver) {
            blockerSum += z;
            numBlockers++;
        }
    }
    return numBlockers != 0 ? blockerSum / float(numBlockers) : -1.0;
}

float PCF_filter(vec4 shadowCoord, float filterRadiusUV, int layer) {
    float cur_depth = shadowCoord.z;
    float sum = 0.0;
    for (int i = 0; i < NUM_SAMPLES_PCF; ++i) {
        float pcfDepth = texture(shadowMap, vec3(shadowCoord.xy + poissonDisk[i] * filterRadiusUV, layer)).r;
        sum += cur_depth - bias > pcfDepth? 1.0 : 0.0;
    }
    return sum / NUM_SAMPLES_PCF;
}

float PCSS(vec4 shadowCoord, int layer) {
    // blocker search
    float zBlocker = findBlocker(shadowCoord, layer);
    if (zBlocker < 0) return 0.0f;
    // penumbra size
    float penumbraRatio = (shadowCoord.z - zBlocker) / zBlocker;
    float filterRadiusUV = penumbraRatio * LIGHT_SIZE_UV * NEAR_PLANE / shadowCoord.z;
    // PCF filtering
    return PCF_filter(shadowCoord, filterRadiusUV, layer);
}

float CSM(vec4 shadowCoord, int layer) {
    return PCSS(shadowCoord, layer);
}

// column-major
const mat4 biasMat = mat4(
	0.5, 0.0, 0.0, 0.0,
	0.0, 0.5, 0.0, 0.0,
	0.0, 0.0, 1.0, 0.0,
	0.5, 0.5, 0.0, 1.0 );

void main() {
    // layer
    float viewDepth = abs(inFragPosView.z);
    int layer = 3;
    for (int i = 0; i < 4; ++i)
        if (viewDepth < sceneData.cascadeDistances[i]) {
            layer = i;
            break;
        }
    
    int shadowMode = int(sceneData.sunlightDirection.w);
    if (shadowMode < 3) layer = 0;
    
    // shadowCoord
    vec4 shadowCoord = biasMat * sceneData.lightViewproj[layer] * inFragPosWorld;

    float shadow = 0.0;
    bool enableShadow = sceneData.sunlightColor.w > 0.5 ? true : false;
    if (enableShadow) {
        if (shadowMode == 0)
            shadow = compute_shadow(shadowCoord, layer);
        else if (shadowMode == 1)
            shadow = PCF(shadowCoord, layer);
        else if (shadowMode == 2)
            shadow = PCSS(shadowCoord, layer);
        else if (shadowMode == 3)
            shadow = CSM(shadowCoord, layer);
    }
    
    // debug
    // if (layer == 0) outFragColor = vec4(1.0, 0, 0, 1.0);
    // else if (layer == 1) outFragColor = vec4(0, 1.0, 0, 1.0);
    // else if (layer == 2) outFragColor = vec4(0, 0, 1.0, 1.0);
    // else outFragColor = vec4(1, 1, 0, 1);
    // return;
        
    // vec4 texColor = texture(globalTextures[nonuniformEXT(materialData.albedoID)], inUV);
    // vec3 color = inColor * texColor.xyz * materialData.colorFactors.xyz;
    vec3 color = inColor;

    vec3 N = normalize(inNormal);
    vec3 L = normalize(inLightVec);
    vec3 diffuse = max(0.0, dot(N, L)) * color * sceneData.sunlightColor.xyz;

    vec3 ambient = sceneData.ambientColor.xyz;

    outFragColor = vec4(diffuse * (1.0 - shadow) + ambient, 1.0);
}