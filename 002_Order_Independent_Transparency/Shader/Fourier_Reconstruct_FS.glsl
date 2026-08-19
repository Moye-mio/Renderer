#version 430 core

// Fourier Opacity OIT — Pass 3：重建透射率并累加加权颜色。
//
// 傅里叶级数可以逐项解析积分，所以求光学厚度不需要任何数值积分：
//   tau(t) = a0*t + sum_k 1/(PI*k) * [a_k*sin(2PI k t) + b_k*(1-cos(2PI k t))]
// 系数按 a_k = sum_i a0_i*cos(2PI k t_i) 存（省掉标准展开里的因子 2），所以
// 这里的 1/(PI*k) 就是标准公式的 2/(2PI*k)。这样 tau(1) = a0 严格成立，
// 合成阶段的总透射率 exp(-a0) 正好等于 prod(1-alpha_i)。
//
// 与 Weighted-Blended OIT 的差别只在权重：McGuire 用的是一个纯深度启发式函数，
// 这里换成物理意义明确的重建透射率，这正是本算法的核心改进点。
#define PI 3.14159265358979

layout(location = 0) in vec3 v2f_NormalVS;
layout(location = 1) in vec3 v2f_PosVS;
layout(location = 0) out vec4 Color_;

#ifdef VULKAN
#define LAYOUT_BIND(s, b) layout(set = s, binding = b)
#else
#define LAYOUT_BIND(s, b) layout(binding = b)
#endif

LAYOUT_BIND(0, 0) layout(std140) uniform u_SceneShading
{
	mat4 u_ProjectionMatrix;
	mat4 u_ViewMatrix;
	vec4 u_LightDirVSAndAmbient;
	vec4 u_LightColor;
	vec4 u_WeightedParams;
	vec4 u_FourierParams; // x=zMin, y=1/(zMax-zMin), z=谐波阶数
};

LAYOUT_BIND(0, 1) uniform sampler2D u_CoefficientOneTex;
LAYOUT_BIND(0, 2) uniform sampler2D u_CoefficientTwoTex;

#ifdef VULKAN
layout(push_constant) uniform PC {
	layout(offset = 0) mat4 u_ModelMatrix;
	layout(offset = 64) vec4 u_AlbedoOpacity;
} pc;
#define u_AlbedoOpacity pc.u_AlbedoOpacity
#else
uniform vec4 u_AlbedoOpacity;
#endif

void main()
{
	ivec2 pixel = ivec2(gl_FragCoord.xy);
	vec4 coefficientOne = texelFetch(u_CoefficientOneTex, pixel, 0);
	vec4 coefficientTwo = texelFetch(u_CoefficientTwoTex, pixel, 0);

	float a0 = coefficientOne.y;
	float a1 = coefficientOne.z;
	float b1 = coefficientOne.w;
	float a2 = coefficientTwo.x;
	float b2 = coefficientTwo.y;
	float a3 = coefficientTwo.z;
	float b3 = coefficientTwo.w;

	float t = clamp((abs(v2f_PosVS.z) - u_FourierParams.x) * u_FourierParams.y, 0.0, 1.0);
	float harmonics = u_FourierParams.z;

	float c1 = cos(2.0 * PI * t);
	float s1 = sin(2.0 * PI * t);
	float c2 = c1 * c1 - s1 * s1;
	float s2 = 2.0 * c1 * s1;
	float c3 = c2 * c1 - s2 * s1;
	float s3 = s2 * c1 + c2 * s1;

	float opticalDepth = a0 * t;
	if (harmonics >= 1.0) opticalDepth += (a1 * s1 + b1 * (1.0 - c1)) / PI;
	if (harmonics >= 2.0) opticalDepth += (a2 * s2 + b2 * (1.0 - c2)) / (2.0 * PI);
	if (harmonics >= 3.0) opticalDepth += (a3 * s3 + b3 * (1.0 - c3)) / (3.0 * PI);

	// 自遮挡补偿：阶跃函数的傅里叶部分和在跳变点收敛到左右极限的中点，也就是
	// 本片元自己的脉冲在 tau(t) 里只算进了一半，减掉它才是"片元前方"的光学厚度。
	float alpha = clamp(u_AlbedoOpacity.a, 1e-4, 0.9995);
	float selfOpticalDepth = -log(1.0 - alpha);
	float frontOpticalDepth = max(opticalDepth - 0.5 * selfOpticalDepth, 0.0);
	// 3 阶截断逼近 delta 必然有 Gibbs 振铃，夹一下防止过冲把透射率顶到 1 以上。
	float transmittance = clamp(exp(-frontOpticalDepth), 0.0, 1.0);

	vec3 albedo = u_AlbedoOpacity.rgb;
	vec3 N = normalize(v2f_NormalVS);
	vec3 L = normalize(u_LightDirVSAndAmbient.xyz);
	float ndotl = abs(dot(N, L));
	vec3 V = normalize(-v2f_PosVS);
	vec3 H = normalize(L + V);
	float spec = pow(max(dot(N, H), 0.0), 32.0);

	vec3 ambient = albedo * u_LightDirVSAndAmbient.w;
	vec3 diffuse = albedo * ndotl * u_LightColor.rgb;
	vec3 result = ambient + diffuse + vec3(0.18) * spec * u_LightColor.rgb;

	// w = alpha * T_front。sum(w) 在解析意义下恰好等于 1 - T_total，
	// 所以合成阶段的 rgb/a 归一化不会引入额外的尺度误差。
	float weight = alpha * transmittance;
	Color_ = vec4(result * weight, weight);
}
