#version 430 core

// 前向光照：与 DeferredLighting_FS 同一套视空间 Blinn-Phong / 半径平方衰减。
// albedo 从漫反射纹理采样，不读 G-Buffer。
layout(location = 0) in vec2 v2f_TexCoords;
layout(location = 1) in vec3 v2f_Normal;
layout(location = 2) in vec3 v2f_FragPosInViewSpace;
layout(location = 0) out vec4 Color_;

#ifdef VULKAN
#define LAYOUT_BIND(s, b) layout(set = s, binding = b)
#else
#define LAYOUT_BIND(s, b) layout(binding = b)
#endif

#define MAX_LIGHTS 5

struct PointLight
{
	vec4 positionVSAndRadius; // xyz: 视空间位置, w: 影响半径
	vec4 colorAndIntensity;   // rgb: 颜色,       w: 强度
};

// std140：与 SharedShadingParams::LightBlockData 对齐（176B）。
LAYOUT_BIND(0, 2) layout(std140) uniform u_LightBlock
{
	PointLight u_Lights[MAX_LIGHTS];
	ivec4      u_LightCount; // x = 有效光源数
};

LAYOUT_BIND(0, 1) uniform sampler2D u_DiffuseTexture;

void main()
{
	vec3 albedo    = texture(u_DiffuseTexture, v2f_TexCoords).rgb;
	vec3 N         = normalize(v2f_Normal);
	vec3 fragPosVS = v2f_FragPosInViewSpace;
	vec3 V         = normalize(-fragPosVS);

	vec3 result = albedo * 0.08; // 与 Deferred / SharedShadingParams::ambient 对齐

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

		float att = clamp(1.0 - dist / max(radius, 1e-4), 0.0, 1.0);
		att = att * att;

		float diff = max(dot(N, L), 0.0);
		vec3  H    = normalize(L + V);
		float spec = pow(max(dot(N, H), 0.0), 32.0);

		result += (albedo * diff + vec3(0.25) * spec) * Lcol * intensity * att;
	}

	Color_ = vec4(result, 1.0);
}
