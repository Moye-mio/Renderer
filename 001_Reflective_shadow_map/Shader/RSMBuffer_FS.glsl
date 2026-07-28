#version 430 core

// 任务 10：v2f 输入必须显式 layout(location=N)，与 RSMBuffer_VS 对齐。
layout(location = 0) in vec2 v2f_TexCoords;
layout(location = 1) in vec3 v2f_Normal;
layout(location = 2) in vec3 v2f_FragPosInViewSpace;

layout (location = 0) out vec3 Flux_;
layout (location = 1) out vec3 Normal_;
layout (location = 2) out vec3 Position_;

#ifdef VULKAN
#define LAYOUT_BIND(s, b) layout(set = s, binding = b)
#else
#define LAYOUT_BIND(s, b) layout(binding = b)
#endif

LAYOUT_BIND(0, 1) uniform sampler2D u_DiffuseTexture;

// u_LightColor 是常量，且 RSMBufferPass 没有为它注册 push constant range；
// 直接在 shader 里用 const vec3 替代原来的 "uniform vec3 = vec3(1)"。
const vec3 u_LightColor = vec3(1.0);

void main()
{
    vec3 TexelColor = texture(u_DiffuseTexture, v2f_TexCoords).rgb;
    //TexelColor = pow(TexelColor, vec3(2.2f));
    vec3 VPLFlux = u_LightColor * TexelColor;
    Flux_ = VPLFlux;
    Normal_ = v2f_Normal;
    Position_ = v2f_FragPosInViewSpace;
}