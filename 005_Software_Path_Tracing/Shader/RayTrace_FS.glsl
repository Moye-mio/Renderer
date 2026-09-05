#version 430 core

// Cornell Box 光线追踪测试台。
//
// 场景全是解析几何：一个六面闭合的白盒（相机站在盒内）、两个互不相交的白球，
// 以及贴着天花板、法线朝下的矩形面光源。没有加速结构，求交直接在片元里算，
// 所以 GL / VK 跑的是同一份 shader。
//
// 本 pass 输出的是「累积均值」而不是单帧结果：读上一帧的均值，把本帧新采的
// 样本按 1/(N+n) 混进去。相机一动或采样参数一改，CPU 侧会把 N 清零重来。
// 曝光与色调映射留给 Display_FS.glsl，这里全程线性。

layout(location = 0) in  vec2 v2f_TexCoords;
layout(location = 0) out vec4 Color_;

#ifdef VULKAN
#define LAYOUT_BIND(s, b) layout(set = s, binding = b)
#else
#define LAYOUT_BIND(s, b) layout(binding = b)
#endif

LAYOUT_BIND(0, 0) uniform sampler2D u_History;

LAYOUT_BIND(0, 1) layout(std140) uniform u_Scene
{
	vec4 u_BoxMin;        // xyz: 盒内壁最小角
	vec4 u_BoxMax;        // xyz: 盒内壁最大角
	vec4 u_Sphere0;       // xyz: 球心, w: 半径
	vec4 u_Sphere1;       // xyz: 球心, w: 半径
	vec4 u_LightCenter;   // xyz: 面光源中心，法线固定朝 -Y
	vec4 u_LightHalfSize; // xy: 面光源在 x / z 上的半边长
	vec4 u_LightEmission; // rgb: 面光源辐射亮度
	vec4 u_Albedo;        // rgb: 盒壁与两球共用的白色漫反射率
};

LAYOUT_BIND(0, 2) layout(std140) uniform u_Frame
{
	vec4 u_CameraPos;     // xyz: 相机世界坐标
	vec4 u_CameraRight;   // xyz: 右向量, w: tan(fovY/2) * aspect
	vec4 u_CameraUp;      // xyz: 上向量, w: tan(fovY/2)
	vec4 u_CameraForward; // xyz: 视线方向
	vec4 u_Resolution;    // xy: 分辨率, zw: 1 / 分辨率
	vec4 u_Sampling;      // x: 已累积样本数 N, y: 本帧样本数, z: 最大弹射次数, w: 帧种子
	vec4 u_Options;       // x: 模式, y: NEE 开关, z: AO 半径, w: AO / 光源采样数
};

const float kPi         = 3.14159265359;
const float kRayEpsilon = 1e-4;
const float kInfinity   = 1e30;

// 与 RayTracingContext.h 的 RTTechnique 一一对应。
const int kModeNormal    = 0;
const int kModeDirect    = 1;
const int kModeAO        = 2;
const int kModePathTrace = 3;

// 循环上界要给编译期常量，运行期的采样数 / 弹射数再在循环里 break。
const int kMaxSamplesPerFrame = 64;
const int kMaxBounces         = 64;
const int kMaxShadowSamples   = 64;

// ----------------------------------------------------------------------------
// 随机数：PCG（一次乘加 + 两次异或移位）
//
// 比 fract(sin(dot(...))) 那类哈希稳得多——后者在样本数上千之后会露出
// 明显的结构性条纹，累积到最后收敛不到正确值。
// ----------------------------------------------------------------------------
uint NextRandom(inout uint state)
{
	state = state * 747796405u + 2891336453u;
	uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
	return (word >> 22u) ^ word;
}

float Rand(inout uint state)
{
	return float(NextRandom(state)) * (1.0 / 4294967296.0);
}

// 像素坐标 + 帧序号打散成初始状态；同一像素在不同帧拿到互不相关的序列。
uint InitSeed(uvec2 pixel, uint frameSeed)
{
	uint state = pixel.x * 1973u + pixel.y * 9277u + frameSeed * 26699u;
	return NextRandom(state) | 1u;
}

// ----------------------------------------------------------------------------
// 采样
// ----------------------------------------------------------------------------

// Duff 等人的无分支正交基构造，避免 cross(n, up) 在 n 平行 up 时退化。
void BuildOrthonormalBasis(vec3 n, out vec3 t, out vec3 b)
{
	float s = (n.z >= 0.0) ? 1.0 : -1.0;
	float a = -1.0 / (s + n.z);
	float d = n.x * n.y * a;
	t = vec3(1.0 + s * n.x * n.x * a, s * d, -s * n.x);
	b = vec3(d, s + n.y * n.y * a, -n.y);
}

// 余弦重要性采样，pdf = cos(theta) / PI。
// Lambert BRDF 是 albedo / PI，两者相除刚好把 cos 和 PI 全约掉，
// 所以路径追踪里 throughput 只需要乘 albedo。
vec3 SampleCosineHemisphere(vec3 n, float u1, float u2)
{
	float r   = sqrt(u1);
	float phi = 2.0 * kPi * u2;
	vec3 t, b;
	BuildOrthonormalBasis(n, t, b);
	return normalize(t * (r * cos(phi)) + b * (r * sin(phi)) + n * sqrt(max(0.0, 1.0 - u1)));
}

// ----------------------------------------------------------------------------
// 求交
// ----------------------------------------------------------------------------

// 盒子只有五面墙：+Z 那面是开口，相机站在盒外从开口看进来（和 Cornell Box
// 原始机位一致）。相机在盒内会看不全对面墙和近处地板，那是透视本身的限制。
//
// 逐面求交而不是用 slab：slab 的"内部出射点"写法只在 ro 一定在盒内时成立，
// 这里相机在盒外，而且还要能跳过开口那一面。法线一律朝盒内。
bool IntersectOpenBox(vec3 ro, vec3 rd, out float t, out vec3 n)
{
	t = kInfinity;
	n = vec3(0.0);
	bool hitAnything = false;

	for (int axis = 0; axis < 3; ++axis)
	{
		if (abs(rd[axis]) < 1e-9)
			continue;

		// side 0 是 min 面、side 1 是 max 面；+Z 的 max 面是开口，跳过。
		for (int side = 0; side < 2; ++side)
		{
			if (axis == 2 && side == 1)
				continue;

			float plane = (side == 0) ? u_BoxMin[axis] : u_BoxMax[axis];
			float tFace = (plane - ro[axis]) / rd[axis];
			if (tFace <= kRayEpsilon || tFace >= t)
				continue;

			// 交点要落在这一面的矩形范围内（另外两个轴）。
			vec3 p  = ro + rd * tFace;
			int  a1 = (axis + 1) % 3;
			int  a2 = (axis + 2) % 3;
			if (p[a1] < u_BoxMin[a1] || p[a1] > u_BoxMax[a1]) continue;
			if (p[a2] < u_BoxMin[a2] || p[a2] > u_BoxMax[a2]) continue;

			t = tFace;
			n = vec3(0.0);
			n[axis] = (side == 0) ? 1.0 : -1.0;
			hitAnything = true;
		}
	}

	return hitAnything;
}

// 最近的正根。rd 已归一化，所以二次项系数 a = 1，判别式省掉 4ac。
bool IntersectSphere(vec3 ro, vec3 rd, vec4 sphere, out float t)
{
	vec3  oc = ro - sphere.xyz;
	float b  = dot(oc, rd);
	float c  = dot(oc, oc) - sphere.w * sphere.w;
	float disc = b * b - c;
	if (disc < 0.0)
		return false;

	float sq = sqrt(disc);
	t = -b - sq;
	if (t <= kRayEpsilon)
		t = -b + sq;
	return t > kRayEpsilon;
}

// 面光源单面发光（法线 -Y）：只有自下往上的射线才可能看到发光面，
// 这一个条件同时排掉了灯与天花板之间那条缝里向上打的射线。
bool IntersectLight(vec3 ro, vec3 rd, out float t)
{
	if (rd.y <= 1e-6)
		return false;

	t = (u_LightCenter.y - ro.y) / rd.y;
	if (t <= kRayEpsilon)
		return false;

	vec3 p = ro + rd * t;
	return all(lessThanEqual(abs(p.xz - u_LightCenter.xz), u_LightHalfSize.xy));
}

struct Hit
{
	float t;
	vec3  normal;
	bool  isEmitter;
};

bool TraceScene(vec3 ro, vec3 rd, out Hit hit)
{
	hit.t         = kInfinity;
	hit.normal    = vec3(0.0, 1.0, 0.0);
	hit.isEmitter = false;
	bool hitAnything = false;

	float tBox;
	vec3  nBox;
	if (IntersectOpenBox(ro, rd, tBox, nBox) && tBox < hit.t)
	{
		hit.t         = tBox;
		hit.normal    = nBox;
		hit.isEmitter = false;
		hitAnything   = true;
	}

	float tSphere;
	if (IntersectSphere(ro, rd, u_Sphere0, tSphere) && tSphere < hit.t)
	{
		hit.t         = tSphere;
		hit.normal    = normalize(ro + rd * tSphere - u_Sphere0.xyz);
		hit.isEmitter = false;
		hitAnything   = true;
	}
	if (IntersectSphere(ro, rd, u_Sphere1, tSphere) && tSphere < hit.t)
	{
		hit.t         = tSphere;
		hit.normal    = normalize(ro + rd * tSphere - u_Sphere1.xyz);
		hit.isEmitter = false;
		hitAnything   = true;
	}

	float tLight;
	if (IntersectLight(ro, rd, tLight) && tLight < hit.t)
	{
		hit.t         = tLight;
		hit.normal    = vec3(0.0, -1.0, 0.0);
		hit.isEmitter = true;
		hitAnything   = true;
	}

	return hitAnything;
}

// 只要 maxT 之内有遮挡就返回 true，不关心命中了谁，比 TraceScene 省一半活。
//
// 面光源不参与遮挡：NEE 的阴影线要打到它，AO 的短射线撞到灯也不算被挡。
bool Occluded(vec3 ro, vec3 rd, float maxT)
{
	float t;
	if (IntersectSphere(ro, rd, u_Sphere0, t) && t < maxT) return true;
	if (IntersectSphere(ro, rd, u_Sphere1, t) && t < maxT) return true;

	// 盒子是凸的、面光源也在盒内，所以 NEE 的阴影线段整段都在盒内，墙面永远
	// 挡不住光；AO 的短射线则确实需要墙面参与，靠近墙角时的变暗就是它算出来的。
	// 两种用法共用这一个函数即可。
	vec3 n;
	if (IntersectOpenBox(ro, rd, t, n) && t < maxT) return true;

	return false;
}

// ----------------------------------------------------------------------------
// 直接光：面光源上均匀取一点，把面积 pdf 换算到立体角
//
//   pdf_area  = 1 / area
//   pdf_omega = pdf_area * dist^2 / cosLight
//   Lo        = (albedo / PI) * Li * cosSurface / pdf_omega
//
// 返回值已经含了 Lambert BRDF 的 1 / PI，但**不含 albedo**——调用方自己乘，
// 这样路径追踪里可以直接和 throughput 相乘。
// ----------------------------------------------------------------------------
vec3 SampleAreaLight(vec3 p, vec3 n, inout uint rng)
{
	vec2 u  = vec2(Rand(rng), Rand(rng)) * 2.0 - 1.0;
	vec3 lp = u_LightCenter.xyz
	        + vec3(u.x * u_LightHalfSize.x, 0.0, u.y * u_LightHalfSize.y);

	vec3  toLight = lp - p;
	float dist2   = dot(toLight, toLight);
	float dist    = sqrt(dist2);
	vec3  wi      = toLight / dist;

	float cosSurface = dot(n, wi);
	// 光源法线固定朝下 (0, -1, 0)，从光源看向着色点的方向是 -wi，于是
	// cosLight = dot((0,-1,0), -wi) = +wi.y。写成 -wi.y 的话只有位于光源
	// 上方的天花板会被点亮，地板和墙全被当成背面直接返回 0。
	float cosLight   = wi.y;
	if (cosSurface <= 0.0 || cosLight <= 0.0)
		return vec3(0.0);

	if (Occluded(p + n * kRayEpsilon, wi, dist - 2.0 * kRayEpsilon))
		return vec3(0.0);

	float area = 4.0 * u_LightHalfSize.x * u_LightHalfSize.y;
	return u_LightEmission.rgb * (cosSurface * cosLight * area) / (dist2 * kPi);
}

// ----------------------------------------------------------------------------
// 四种算法
// ----------------------------------------------------------------------------

vec3 ShadeNormal(vec3 ro, vec3 rd)
{
	Hit hit;
	if (!TraceScene(ro, rd, hit))
		return vec3(0.0);
	return hit.normal * 0.5 + 0.5;
}

// 只算一次可见性 + 面光源直接光。光源采样数就是软阴影的质量旋钮：
// 取 1 时半影全是噪点，加大之后半影才平滑。
vec3 ShadeDirect(vec3 ro, vec3 rd, inout uint rng)
{
	Hit hit;
	if (!TraceScene(ro, rd, hit))
		return vec3(0.0);
	if (hit.isEmitter)
		return u_LightEmission.rgb;

	vec3 p = ro + rd * hit.t;
	int  n = clamp(int(u_Options.w), 1, kMaxShadowSamples);

	vec3 sum = vec3(0.0);
	for (int i = 0; i < kMaxShadowSamples; ++i)
	{
		if (i >= n) break;
		sum += SampleAreaLight(p, hit.normal, rng);
	}
	return u_Albedo.rgb * sum / float(n);
}

// 射线 AO：半球余弦采样，命中距离小于 aoRadius 就算被遮。
// 这里刻意不带光源，只看几何遮蔽量，方便和光栅化的 SSAO 对照。
vec3 ShadeAmbientOcclusion(vec3 ro, vec3 rd, inout uint rng)
{
	Hit hit;
	if (!TraceScene(ro, rd, hit))
		return vec3(0.0);
	if (hit.isEmitter)
		return vec3(1.0);

	vec3  p      = ro + rd * hit.t;
	vec3  origin = p + hit.normal * kRayEpsilon;
	float radius = max(u_Options.z, kRayEpsilon);
	int   n      = clamp(int(u_Options.w), 1, kMaxShadowSamples);

	float occluded = 0.0;
	for (int i = 0; i < kMaxShadowSamples; ++i)
	{
		if (i >= n) break;
		vec3 wi = SampleCosineHemisphere(hit.normal, Rand(rng), Rand(rng));
		if (Occluded(origin, wi, radius))
			occluded += 1.0;
	}

	return u_Albedo.rgb * (1.0 - occluded / float(n));
}

// 路径追踪：余弦重要性采样 + 可选 NEE + 俄罗斯轮盘。
//
// 因为余弦 pdf 和 Lambert BRDF 完全约掉，throughput 每次弹射只乘 albedo；
// 所有物体又共用同一个白色 albedo，所以这里能很直接地看出多次弹射的能量衰减。
vec3 ShadePathTrace(vec3 ro, vec3 rd, inout uint rng)
{
	int  maxBounces = clamp(int(u_Sampling.z), 1, kMaxBounces);
	bool nee        = u_Options.y > 0.5;

	vec3 radiance   = vec3(0.0);
	vec3 throughput = vec3(1.0);

	for (int bounce = 0; bounce < kMaxBounces; ++bounce)
	{
		if (bounce >= maxBounces) break;

		Hit hit;
		if (!TraceScene(ro, rd, hit))
			break;

		if (hit.isEmitter)
		{
			// 开了 NEE 时，除主光线之外的光源命中已经被上一次弹射的
			// SampleAreaLight 算过了，再加一次就是重复计光（画面明显偏亮）。
			if (bounce == 0 || !nee)
				radiance += throughput * u_LightEmission.rgb;
			break;
		}

		vec3 p = ro + rd * hit.t;
		vec3 n = hit.normal;

		if (nee)
			radiance += throughput * u_Albedo.rgb * SampleAreaLight(p, n, rng);

		throughput *= u_Albedo.rgb;

		// 俄罗斯轮盘：前几次弹射不裁，之后按 throughput 的量级决定生死，
		// 活下来的按存活概率补偿，期望值不变。
		if (bounce >= 3)
		{
			float survive = clamp(max(throughput.r, max(throughput.g, throughput.b)),
			                      0.05, 1.0);
			if (Rand(rng) > survive) break;
			throughput /= survive;
		}

		rd = SampleCosineHemisphere(n, Rand(rng), Rand(rng));
		ro = p + n * kRayEpsilon;
	}

	return radiance;
}

// ----------------------------------------------------------------------------

// 像素内抖动：累积够样本之后就等于免费的抗锯齿。
vec3 PrimaryRayDir(vec2 uv, inout uint rng)
{
	vec2 jitter = (vec2(Rand(rng), Rand(rng)) - 0.5) * u_Resolution.zw;
	vec2 ndc    = (uv + jitter) * 2.0 - 1.0;
	return normalize(u_CameraForward.xyz
	               + u_CameraRight.xyz * (ndc.x * u_CameraRight.w)
	               + u_CameraUp.xyz    * (ndc.y * u_CameraUp.w));
}

void main()
{
	int   mode           = int(u_Options.x);
	float accumulated    = u_Sampling.x;
	int   samplesPerPass = clamp(int(u_Sampling.y), 1, kMaxSamplesPerFrame);

	uvec2 pixel = uvec2(v2f_TexCoords * u_Resolution.xy);
	uint  rng   = InitSeed(pixel, uint(u_Sampling.w));

	vec3 sum = vec3(0.0);
	for (int s = 0; s < kMaxSamplesPerFrame; ++s)
	{
		if (s >= samplesPerPass) break;

		vec3 rd = PrimaryRayDir(v2f_TexCoords, rng);
		vec3 ro = u_CameraPos.xyz;

		vec3 sample_;
		if (mode == kModeNormal)      sample_ = ShadeNormal(ro, rd);
		else if (mode == kModeDirect) sample_ = ShadeDirect(ro, rd, rng);
		else if (mode == kModeAO)     sample_ = ShadeAmbientOcclusion(ro, rd, rng);
		else                          sample_ = ShadePathTrace(ro, rd, rng);

		sum += sample_;
	}

	// 一个 NaN / Inf 混进累积缓冲就再也洗不掉了（之后每一帧都会被它污染），
	// 所以在写入前拦一道。
	if (any(isnan(sum)) || any(isinf(sum)))
		sum = vec3(0.0);

	// accumulated == 0 时历史缓冲还没写过内容，必须真的跳过采样：
	// 写成三目运算的话 texture() 里的垃圾值仍会参与 previous * 0，
	// 只要那垃圾值是 NaN，NaN * 0 依然是 NaN，第一帧就把累积毒死了。
	float total = float(samplesPerPass);
	if (accumulated > 0.0)
	{
		sum   += texture(u_History, v2f_TexCoords).rgb * accumulated;
		total += accumulated;
	}

	Color_ = vec4(sum / total, 1.0);
}
