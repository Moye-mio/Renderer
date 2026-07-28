#version 460
// ============================================================================
// 0xx_RayQueryHello - blit.frag.glsl
// 采样光追 compute 写出的 storage image，输出到 backbuffer。
// ============================================================================
layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 oColor;

layout(set = 0, binding = 0) uniform sampler2D u_Tex;

void main()
{
    oColor = texture(u_Tex, vUV);
}
