#version 460
// ============================================================================
// 0xx_RayQueryHello - closesthit.rchit.glsl（RT Pipeline / 路线 B）
// 命中三角形：用重心坐标着色写入 payload。
// ============================================================================
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec3 payload;
hitAttributeEXT vec2 attribs;

void main()
{
    // attribs = (b1, b2)；b0 = 1 - b1 - b2
    vec3 bary = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
    payload = bary;
}
