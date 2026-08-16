#version 430 core

// 延迟光照：从 G-Buffer 采样 Albedo / Normal(view) / Position(view)，
// 对最多 1000 个点光源做 Blinn-Phong 累加（全部在视空间进行，相机位于原点）。
layout(location = 0) in  vec2 v2f_TexCoords;
layout(location = 0) out vec4 Color_;

#ifdef VULKAN
#define LAYOUT_BIND(s, b) layout(set = s, binding = b)
#else
#define LAYOUT_BIND(s, b) layout(binding = b)
#endif

#define MAX_LIGHTS 1000

struct PointLight
{
	vec4 positionVSAndRadius; // xyz: 视空间位置, w: 影响半径
	vec4 colorAndIntensity;   // rgb: 颜色,       w: 强度
};

// std140：PointLight = 2*vec4 = 32B；数组 1000 个 = 32000B；其后 ivec4 = 16B；总计 32016B。
LAYOUT_BIND(0, 0) layout(std140) uniform u_LightBlock
{
	PointLight u_Lights[MAX_LIGHTS];
	ivec4      u_LightCount; // x = 有效光源数
};

LAYOUT_BIND(0, 1) uniform sampler2D u_AlbedoTexture;
LAYOUT_BIND(0, 2) uniform sampler2D u_NormalTexture;
LAYOUT_BIND(0, 3) uniform sampler2D u_PositionTexture;

#ifdef VULKAN
layout(push_constant) uniform PC {
	layout(offset = 0) int u_DebugView;
} pc;
#define u_DebugView pc.u_DebugView
#else
uniform int u_DebugView;
#endif

void main()
{
	ivec2 px = ivec2(gl_FragCoord.xy);
	vec3 albedo    = texelFetch(u_AlbedoTexture,   px, 0).rgb;
	vec3 N         = texelFetch(u_NormalTexture,   px, 0).xyz;
	vec3 fragPosVS = texelFetch(u_PositionTexture, px, 0).xyz;

	// DeferredParams::DebugView：1=Albedo 2=Normal 3=Position（0=Final 走下面光照）
	if (u_DebugView == 1)
	{
		Color_ = vec4(albedo, 1.0);
		return;
	}
	if (u_DebugView == 2)
	{
		Color_ = vec4(N * 0.5 + 0.5, 1.0);
		return;
	}
	if (u_DebugView == 3)
	{
		Color_ = vec4(fragPosVS * 0.1 + 0.5, 1.0);
		return;
	}

	// 背景（无几何）：G-Buffer 清空为 0，法线长度约为 0 -> 输出深色背景。
	if (dot(N, N) < 0.01)
	{
		Color_ = vec4(0.02, 0.02, 0.03, 1.0);
		return;
	}

	N = normalize(N);
	vec3 V = normalize(-fragPosVS); // 视空间相机在原点

	vec3 result = albedo * 0.08; // 环境项

	int count = min(u_LightCount.x, MAX_LIGHTS);
	for (int i = 0; i < count; ++i)
	{
		vec3  Lpos      = u_Lights[i].positionVSAndRadius.xyz;
		float radius    = u_Lights[i].positionVSAndRadius.w;
		vec3  Lcol      = u_Lights[i].colorAndIntensity.rgb;
		float intensity = u_Lights[i].colorAndIntensity.w;

		vec3  Lvec = Lpos - fragPosVS;
		float dist = length(Lvec);
		vec3  L    = Lvec / max(dist, 1e-4);

		// 平滑的半径衰减：在 radius 处衰减到 0
		float att = clamp(1.0 - dist / max(radius, 1e-4), 0.0, 1.0);
		att = att * att;

		float diff = max(dot(N, L), 0.0);
		vec3  H    = normalize(L + V);
		float spec = pow(max(dot(N, H), 0.0), 32.0);

		result += (albedo * diff + vec3(0.25) * spec) * Lcol * intensity * att;
	}

	Color_ = vec4(result, 1.0);
}
