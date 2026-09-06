#version 460
#extension GL_EXT_ray_query : require

// 全屏太阳阴影 mask：从 GBuffer 的世界坐标出发对 Sponza TLAS 打一根 shadow ray。
// 延迟着色的直接光要乘这张 mask，否则屏幕上的直接光没有阴影，而 probe 里的
// 直接光有，两套光照不自洽，GI 也就没什么可看的了。
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform accelerationStructureEXT u_TLAS;

layout(set = 0, binding = 1, std140) uniform DDGIVolume
{
	vec4  u_ProbeOrigin;
	vec4  u_ProbeSpacing;
	ivec4 u_ProbeCounts;
	vec4  u_LightDir;
	vec4  u_LightColor;
	vec4  u_SkyColor;
	ivec4 u_Atlas;
	ivec4 u_Frame;
	vec4  u_Bounce;
	vec4  u_RayRot0;
	vec4  u_RayRot1;
	vec4  u_RayRot2;
} vol;

layout(set = 0, binding = 2) uniform sampler2D u_NormalTexture;
layout(set = 0, binding = 3) uniform sampler2D u_PositionTexture;
layout(set = 0, binding = 4, r16f) uniform image2D u_ShadowMask;

// 太阳当平行光处理，射线长度取够穿出整个 Sponza 即可。
const float SUN_RAY_LENGTH = 64.0;

void main()
{
	ivec2 px = ivec2(gl_GlobalInvocationID.xy);
	ivec2 size = imageSize(u_ShadowMask);
	if (px.x >= size.x || px.y >= size.y)
		return;

	vec2 uv = (vec2(px) + 0.5) / vec2(size);
	vec3 n = texture(u_NormalTexture, uv).rgb;
	if (dot(n, n) < 1e-4)
	{
		// 背景像素没有几何，直接给全亮，着色端本来也不会用到。
		imageStore(u_ShadowMask, px, vec4(1.0));
		return;
	}
	n = normalize(n);

	vec3 worldPos = texture(u_PositionTexture, uv).rgb;
	vec3 L = normalize(vol.u_LightDir.xyz);

	float shadow = 0.0;
	if (dot(n, L) > 0.0)
	{
		rayQueryEXT rq;
		uint flags = gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT;
		rayQueryInitializeEXT(rq, u_TLAS, flags, 0xFF, worldPos + n * 0.01, 0.002, L, SUN_RAY_LENGTH);
		while (rayQueryProceedEXT(rq)) {}
		if (rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionNoneEXT)
			shadow = 1.0;
	}

	imageStore(u_ShadowMask, px, vec4(shadow));
}
