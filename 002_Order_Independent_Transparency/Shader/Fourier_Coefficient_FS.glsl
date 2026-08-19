#version 430 core

// Fourier Opacity OIT — Pass 2：累加傅里叶系数。
//
// 把每个半透明片元看成一个狄拉克式的消光事件：要让它单独作用后的透射率等于
// 1-alpha，脉冲强度必须是 a0 = -ln(1-alpha)。于是沿视线的消光函数
//   sigma(t) = sum_i a0_i * delta(t - t_i)
// 透射率的连乘就变成了光学厚度的求和，而加法天然与提交顺序无关 —— 这正是
// 本算法能用 One/One 加性混合、且完全不需要排序的根本原因。
//
// sigma 无法直接存（每像素片元数不定），改存它在截断傅里叶基上的投影。由
// delta 的筛选性质，单个片元的贡献就是三角函数在该片元深度上的采样值，因此
// 两张 MRT 各自 One/One 累加即可。
#define PI 3.14159265358979

layout(location = 0) in vec3 v2f_NormalVS;
layout(location = 1) in vec3 v2f_PosVS;

layout(location = 0) out vec4 CoefficientOne_; // (片元计数, a0, a1, b1)
layout(location = 1) out vec4 CoefficientTwo_; // (a2, b2, a3, b3)

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
	// alpha 逼近 1 时 -ln(1-alpha) 发散，夹住上界避免出 inf 污染整张系数图。
	float alpha = clamp(u_AlbedoOpacity.a, 1e-4, 0.9995);
	float a0 = -log(1.0 - alpha);

	// 归一化到 [0,1]：窗口由 CPU 按半透明几何的视空间深度包络逐帧给出。
	float t = clamp((abs(v2f_PosVS.z) - u_FourierParams.x) * u_FourierParams.y, 0.0, 1.0);

	// 只调用一次 sin/cos，2、3 倍角用和角公式递推，省下 4 次超越函数。
	float c1 = cos(2.0 * PI * t);
	float s1 = sin(2.0 * PI * t);
	float c2 = c1 * c1 - s1 * s1;
	float s2 = 2.0 * c1 * s1;
	float c3 = c2 * c1 - s2 * s1;
	float s3 = s2 * c1 + c2 * s1;

	CoefficientOne_ = vec4(1.0, a0, a0 * c1, a0 * s1);
	CoefficientTwo_ = vec4(a0 * c2, a0 * s2, a0 * c3, a0 * s3);
}
