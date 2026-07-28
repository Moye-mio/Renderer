#version 460
// ============================================================================
// 0xx_RayQueryHello - raygen.rgen.glsl（RT Pipeline / 路线 B）
// raygen 着色器：逐 launch 像素从正交相机发射一条光线（traceRayEXT），
// 由 miss/closesthit 写回 payload，最终写入 storage image。
// 由 VKShaderCompiler 以 SPIR-V 1.5 + GL_EXT_ray_tracing 编译（任务 15）。
// ============================================================================
#extension GL_EXT_ray_tracing : require

layout(set = 0, binding = 0, rgba8) uniform image2D u_Output;
layout(set = 0, binding = 1) uniform accelerationStructureEXT u_TLAS;

layout(location = 0) rayPayloadEXT vec3 payload;

void main()
{
    ivec2 pix = ivec2(gl_LaunchIDEXT.xy);
    ivec2 sz  = ivec2(gl_LaunchSizeEXT.xy);

    vec2 uv  = (vec2(pix) + 0.5) / vec2(sz);
    vec2 ndc = uv * 2.0 - 1.0;
    float aspect = float(sz.x) / float(sz.y);

    vec3 origin = vec3(ndc.x * aspect * 1.2, ndc.y * 1.2, 2.0);
    vec3 dir    = vec3(0.0, 0.0, -1.0);

    payload = vec3(0.0);
    traceRayEXT(u_TLAS, gl_RayFlagsOpaqueEXT, 0xFF,
                0 /*sbtRecordOffset*/, 0 /*sbtRecordStride*/, 0 /*missIndex*/,
                origin, 0.001, dir, 10.0, 0 /*payload location*/);

    imageStore(u_Output, pix, vec4(payload, 1.0));
}
