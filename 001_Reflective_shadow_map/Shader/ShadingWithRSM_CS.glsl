#version 430 core
#pragma optionNV (unroll all)	//暂时不知道有没有起作用

#define LOCAL_GROUP_SIZE 16
#define VPL_NUM 32

layout (local_size_x = LOCAL_GROUP_SIZE, local_size_y = LOCAL_GROUP_SIZE) in;

// 任务 8：双后端 binding 装饰宏。glslang 编译 Vulkan 时预定义 VULKAN=100。
#ifdef VULKAN
#define LAYOUT_BIND(s, b) layout(set = s, binding = b)
#else
#define LAYOUT_BIND(s, b) layout(binding = b)
#endif

// Vulkan 中 storage image / UBO / sampler 共享同一 binding 命名空间，
// 因此分配互不重叠的 binding 槽。GL 中各类型命名空间分开，但用统一编号也合法。
//   binding=0 : storage image u_OutputImage
//   binding=1 : VPL UBO
//   binding=2 : u_Matrices4ProjectionWorld（CS 中实际未使用，保留以与 cpp 端 ResourceBinding 对齐）
//   binding=3..8 : 6 张 sampler2D（GBuffer 3 + RSM 3）

LAYOUT_BIND(0, 0) layout(rgba32f) uniform writeonly image2D u_OutputImage;

LAYOUT_BIND(0, 1) layout(std140) uniform VPLsSampleCoordsAndWeights
{
    vec4 u_VPLsSampleCoordsAndWeights[VPL_NUM];
};

LAYOUT_BIND(0, 2) layout(std140) uniform u_Matrices4ProjectionWorld
{
    mat4 u_ProjectionMatrix;
    mat4 u_ViewMatrix;
};

LAYOUT_BIND(0, 3) uniform sampler2D u_AlbedoTexture;
LAYOUT_BIND(0, 4) uniform sampler2D u_NormalTexture;
LAYOUT_BIND(0, 5) uniform sampler2D u_PositionTexture;
LAYOUT_BIND(0, 6) uniform sampler2D u_RSMFluxTexture;
LAYOUT_BIND(0, 7) uniform sampler2D u_RSMNormalTexture;
LAYOUT_BIND(0, 8) uniform sampler2D u_RSMPositionTexture;

// 任务 8：5 个 push constant 在 Vulkan 中合并到 push_constant block。
// layout offset 与 ShadingWithRSMPass.cpp 中 cpp 侧 offset 严格对齐（参见 cpp 注释）。
#ifdef VULKAN
layout(push_constant) uniform PC {
    layout(offset = 0)  mat4  u_LightVPMatrixMulInverseCameraViewMatrix;
    layout(offset = 64) float u_MaxSampleRadius;
    layout(offset = 68) int   u_RSMSize;
    layout(offset = 72) int   u_VPLNum;
    layout(offset = 80) vec3  u_LightDirInViewSpace;
} pc;
#define u_LightVPMatrixMulInverseCameraViewMatrix pc.u_LightVPMatrixMulInverseCameraViewMatrix
#define u_MaxSampleRadius                          pc.u_MaxSampleRadius
#define u_RSMSize                                  pc.u_RSMSize
#define u_VPLNum                                   pc.u_VPLNum
#define u_LightDirInViewSpace                      pc.u_LightDirInViewSpace
#else
uniform mat4  u_LightVPMatrixMulInverseCameraViewMatrix;
uniform float u_MaxSampleRadius;
uniform int   u_RSMSize;
uniform int   u_VPLNum;
uniform vec3  u_LightDirInViewSpace;
#endif

vec3 calcVPLIrradiance(vec3 vVPLFlux, vec3 vVPLNormal, vec3 vVPLPos, vec3 vFragPos, vec3 vFragNormal, float vWeight)
{
    vec3 VPL2Frag = normalize(vFragPos - vVPLPos);
    return vVPLFlux * max(dot(vVPLNormal, VPL2Frag), 0) * max(dot(vFragNormal, -VPL2Frag), 0) * vWeight;
}

void main()
{
    if (u_VPLNum != VPL_NUM)
    return;

    ivec2 FragPos = ivec2(gl_GlobalInvocationID.xy);
    ivec2 ImageSize = imageSize(u_OutputImage);
    if (FragPos.x >= ImageSize.x || FragPos.y >= ImageSize.y)
    return;

    vec3 FragViewNormal = normalize(texelFetch(u_NormalTexture, FragPos, 0).xyz);
    vec3 FragAlbedo = texelFetch(u_AlbedoTexture, FragPos, 0).xyz;
    vec3 FragViewPos = texelFetch(u_PositionTexture, FragPos, 0).xyz;

    vec4 FragPosInLightSpace = u_LightVPMatrixMulInverseCameraViewMatrix * vec4(FragViewPos, 1);
    FragPosInLightSpace /= FragPosInLightSpace.w;
    vec2 FragNDCPos4Light = (FragPosInLightSpace.xy + 1) / 2;
    float RSMTexelSize = 1.0 / u_RSMSize;
    vec3 DirectIllumination;
    if (FragPosInLightSpace.z < 0.0f || FragPosInLightSpace.x > 1.0f || FragPosInLightSpace.y > 1.0f || FragPosInLightSpace.x < 0.0f || FragPosInLightSpace.y < 0.0f)
    DirectIllumination = vec3(0.1) * FragAlbedo;
    else
    DirectIllumination = FragAlbedo * max(dot(-u_LightDirInViewSpace, FragViewNormal), 0.1);
    vec3 IndirectIllumination = vec3(0);
    for (int i = 0; i < u_VPLNum; ++i)
    {
        vec3 VPLSampleCoordAndWeight = u_VPLsSampleCoordsAndWeights[i].xyz;
        vec2 VPLSamplePos = FragNDCPos4Light + u_MaxSampleRadius * VPLSampleCoordAndWeight.xy * RSMTexelSize;
        vec3 VPLFlux = texture(u_RSMFluxTexture, VPLSamplePos).xyz;
        vec3 VPLNormalInViewSpace = normalize(texture(u_RSMNormalTexture, VPLSamplePos).xyz);
        vec3 VPLPositionInViewSpace = texture(u_RSMPositionTexture, VPLSamplePos).xyz;

        IndirectIllumination += calcVPLIrradiance(VPLFlux, VPLNormalInViewSpace, VPLPositionInViewSpace, FragViewPos, FragViewNormal, VPLSampleCoordAndWeight.z);
    }
    IndirectIllumination *= FragAlbedo;

    vec3 Result = DirectIllumination  + IndirectIllumination / u_VPLNum;

    imageStore(u_OutputImage, FragPos, vec4(Result, 1.0));
}