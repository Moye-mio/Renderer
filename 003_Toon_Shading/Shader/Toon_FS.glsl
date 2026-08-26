#version 430 core

// 半 Lambert × ilm.g 采 Shadow Ramp；ilm.a 选行。
// u_RampParams.w < 0.5 退回 Lambert（无 Ramp），便于 ImGui 对照。
layout(location = 0) in vec2 v2f_TexCoords;
layout(location = 1) in vec3 v2f_NormalVS;
layout(location = 0) out vec4 Color_;

#ifdef VULKAN
#define LAYOUT_BIND(s, b) layout(set = s, binding = b)
#else
#define LAYOUT_BIND(s, b) layout(binding = b)
#endif

LAYOUT_BIND(0, 0) layout(std140) uniform u_ToonShading
{
	mat4 u_ProjectionMatrix;
	mat4 u_ViewMatrix;
	vec4 u_LightDirVSAndAmbient;
	vec4 u_LightColor;
	vec4 u_RampParams;
};

LAYOUT_BIND(0, 1) uniform sampler2D u_DiffuseTexture;
LAYOUT_BIND(0, 2) uniform sampler2D u_IlmTexture;
LAYOUT_BIND(0, 3) uniform sampler2D u_RampTexture;

void main()
{
	vec3 albedo = texture(u_DiffuseTexture, v2f_TexCoords).rgb;
	vec3 N = normalize(v2f_NormalVS);
	vec3 L = normalize(u_LightDirVSAndAmbient.xyz);
	float ndotl = max(dot(N, L), 0.0);

	if (u_RampParams.w < 0.5)
	{
		vec3 ambient = albedo * u_LightDirVSAndAmbient.w;
		vec3 diffuse = albedo * ndotl * u_LightColor.rgb;
		Color_ = vec4(ambient + diffuse, 1.0);
		return;
	}

	vec4 ilm = texture(u_IlmTexture, v2f_TexCoords);
	float halfLambert = dot(N, L) * 0.5 + 0.5;
	float ao = ilm.g;
	float lit = halfLambert * ao;

	float brightFac = u_RampParams.x;
	float greyFac   = u_RampParams.y;
	float darkFac   = u_RampParams.z;
	greyFac = min(greyFac, brightFac - 1e-3);
	darkFac = min(darkFac, greyFac - 1e-3);

	// Ramp 过渡挤在右侧；U 用 dark→bright 压一遍，避免采到 LUT 左半的平涂。
	float rampU = smoothstep(darkFac, brightFac, lit);
	const float kRampTexel = 0.5 / 256.0;
	rampU = clamp(rampU, kRampTexel, 1.0 - kRampTexel);

	// ilm.a ≈ 0 / 0.3 / 0.5 / 0.7 / 1 → 行 0..4。256×20 = 10 行（上昼下夜）。
	float matIndex = clamp(floor(ilm.a * 4.0 + 0.5), 0.0, 4.0);
	bool night = u_RampParams.w > 1.5;
	float row = night ? (matIndex + 5.0) : matIndex;
	// flipVerticallyOnLoad 后 V=0 是 PNG 底。昼在文件上半 → 高 V。
	float rampV = 1.0 - (row + 0.5) / 10.0;

	vec3 rampCol = texture(u_RampTexture, vec2(rampU, rampV)).rgb;
	float bright = smoothstep(greyFac, brightFac, lit);
	vec3 shaded = mix(albedo * rampCol, albedo, bright);
	shaded += albedo * u_LightDirVSAndAmbient.w * 0.25;
	Color_ = vec4(shaded, 1.0);
}
