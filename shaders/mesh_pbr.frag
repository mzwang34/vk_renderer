#version 460

#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec3 inLightVec;
layout (location = 4) in vec4 inFragPosWorld;
layout (location = 5) in vec4 inFragPosView;

layout (location = 0) out vec4 outFragColor;

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

const float PI = 3.14159265359;

// column-major
const mat4 biasMat = mat4(
	0.5, 0.0, 0.0, 0.0,
	0.0, 0.5, 0.0, 0.0,
	0.0, 0.0, 1.0, 0.0,
	0.5, 0.5, 0.0, 1.0 );

// shadow mapping --------------------------------------------------------------------------
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

float calcShadow() {
    // layer
    float viewDepth = abs(inFragPosView.z);
    int layer = NUM_CASCADES - 1;
    for (int i = 0; i < NUM_CASCADES; ++i)
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
    return shadow;
}

// Cook-Torrance ---------------------------------------------------------------------------
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = r * r / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx1 = GeometrySchlickGGX(NdotV, roughness);
    float ggx2 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    vec3 N = normalize(inNormal);
    vec3 camPos = vec3(inverse(sceneData.view)[3]);
    vec3 V = normalize(camPos - inFragPosWorld.xyz);
    vec3 L = normalize(inLightVec);
    vec3 H = normalize(V + L);

    vec4 albedoTex = texture(globalTextures[nonuniformEXT(materialData.albedoID)], inUV);
    if (albedoTex.a < 0.5) discard;
    vec3 albedo = materialData.colorFactors.xyz * albedoTex.xyz * inColor;
    vec4 mrTex = texture(globalTextures[nonuniformEXT(materialData.metalRoughID)], inUV);
    float metallic = materialData.metal_rough_factors.x * mrTex.b;
    float roughness = materialData.metal_rough_factors.y * mrTex.g;

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    vec3 Lo = vec3(0.0);
    vec3 radiance = sceneData.sunlightColor.rgb;

    // cook-torrance brdf
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic; // metallic surface doesn't refract light thus have no diffuse reflections

    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;

    float NdotL = max(dot(N, L), 0.0);
    Lo += (kD * albedo / PI + specular) * NdotL * radiance;

    vec3 ambient = sceneData.ambientColor.xyz * albedo;

    float shadow = calcShadow();
    
    outFragColor = vec4(ambient + Lo * (1.0 - shadow), 1.0);
}