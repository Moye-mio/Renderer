#version 460
// ============================================================================
// 0xx_RayQueryHello - blit.vert.glsl
// 全屏三角形：由 gl_VertexIndex 生成覆盖屏幕的大三角形，输出 UV 供片元采样
// 光追结果 storage image。无顶点输入。
// ============================================================================
layout(location = 0) out vec2 vUV;

void main()
{
    // gl_VertexIndex: 0,1,2 → 覆盖 [-1,3] 的全屏三角形
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vUV = p;                              // 0..2
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
