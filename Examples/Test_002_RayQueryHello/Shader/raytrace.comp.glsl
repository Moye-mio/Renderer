#version 460
// ============================================================================
// 0xx_RayQueryHello - raytrace.comp.glsl
// Ray Query（GL_EXT_ray_query）compute 着色器：逐像素从正交相机发射光线，
// 对 TLAS 求交，命中三角形时以重心坐标着色，未命中写背景色，结果写入
// storage image。由 VKShaderCompiler 在运行期以 SPIR-V 1.5 目标编译
// （任务 10）。本示例仅在支持光追的 VK 后端运行。
// ============================================================================
#extension GL_EXT_ray_query : require

layout(local_size_x = 16, local_size_y = 16) in;

// binding=0：输出 storage image（rgba8）
layout(set = 0, binding = 0, rgba8) uniform image2D u_Output;
// binding=1：场景 TLAS
layout(set = 0, binding = 1) uniform accelerationStructureEXT u_TLAS;

void main()
{
    ivec2 pix = ivec2(gl_GlobalInvocationID.xy);
    ivec2 sz  = imageSize(u_Output);
    if (pix.x >= sz.x || pix.y >= sz.y) return;

    // 屏幕 UV → NDC（Vulkan：uv(0,0) 对应左上角）
    vec2 uv  = (vec2(pix) + 0.5) / vec2(sz);
    vec2 ndc = uv * 2.0 - 1.0;
    float aspect = float(sz.x) / float(sz.y);

    // 正交相机：位于 z=2 沿 -Z 看向三角形所在的 z=0 平面
    vec3 origin = vec3(ndc.x * aspect * 1.2, ndc.y * 1.2, 2.0);
    vec3 dir    = vec3(0.0, 0.0, -1.0);

    rayQueryEXT rq;
    rayQueryInitializeEXT(rq, u_TLAS, gl_RayFlagsOpaqueEXT, 0xFF,
                          origin, 0.001, dir, 10.0);
    while (rayQueryProceedEXT(rq)) {}

    vec3 color = vec3(0.05, 0.06, 0.12); // 背景（深蓝）
    if (rayQueryGetIntersectionTypeEXT(rq, true) ==
        gl_RayQueryCommittedIntersectionTriangleEXT)
    {
        // 命中：用重心坐标做可辨识的彩色着色
        vec2 bary = rayQueryGetIntersectionBarycentricsEXT(rq, true);
        color = vec3(1.0 - bary.x - bary.y, bary.x, bary.y);
    }

    imageStore(u_Output, pix, vec4(color, 1.0));
}
