#version 460
// ============================================================================
// 0xx_RayQueryHello - miss.rmiss.glsl（RT Pipeline / 路线 B）
// 未命中：写入背景色到 payload。
// ============================================================================
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec3 payload;

void main()
{
    payload = vec3(0.05, 0.06, 0.12);
}
