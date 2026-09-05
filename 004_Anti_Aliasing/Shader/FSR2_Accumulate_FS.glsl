#version 430 core

// FSR 2.0 时域累加：在显示分辨率上把低分辨率当前帧重建出来，再按 velocity 回看
// 显示分辨率 history。相对 FSR 1.0 的差别是「多帧 jitter 样本叠到高分辨率」，
// 相对 TAA 的差别是当前帧画在渲染分辨率、history 存在显示分辨率。
//
// 官方 FSR 2 还有 lock / reactive mask / 自动曝光等，这里保留教学核心：
// Lanczos-2 重建、最近深度膨胀运动向量、深度不连续时丢掉 history、邻域 clamp。
layout(location = 0) in vec2 v2f_TexCoords;
layout(location = 0) out vec4 Color_;

#ifdef VULKAN
#define LAYOUT_BIND(s, b) layout(set = s, binding = b)
#else
#define LAYOUT_BIND(s, b) layout(binding = b)
#endif

LAYOUT_BIND(0, 0) uniform sampler2D u_CurrColor;
LAYOUT_BIND(0, 1) uniform sampler2D u_Velocity;
LAYOUT_BIND(0, 2) uniform sampler2D u_DepthVS;
LAYOUT_BIND(0, 3) uniform sampler2D u_History;

LAYOUT_BIND(0, 4) layout(std140) uniform u_FSR2Params
{
	vec4 u_Params;      // x: 当前帧混合权重, y: clamp 模式, z: history 是否有效
	vec4 u_Jitter;      // xy: 当前帧 NDC jitter（换算 UV 时 × 0.5）
	vec4 u_RenderSize;  // xy: 渲染分辨率, zw: 其倒数
	vec4 u_DisplaySize; // xy: 显示分辨率, zw: 其倒数
};

const float kPi = 3.14159265;

vec3 RGBToYCoCg(vec3 c)
{
	return vec3(
		0.25 * c.r + 0.5 * c.g + 0.25 * c.b,
		0.5 * c.r - 0.5 * c.b,
		-0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}

vec3 YCoCgToRGB(vec3 c)
{
	return vec3(
		c.x + c.y - c.z,
		c.x + c.z,
		c.x - c.y - c.z);
}

vec3 ClipAABB(vec3 hist, vec3 aabbMin, vec3 aabbMax)
{
	vec3 center = 0.5 * (aabbMin + aabbMax);
	vec3 extents = 0.5 * (aabbMax - aabbMin);
	vec3 delta = hist - center;
	vec3 t = abs(delta) / max(extents, vec3(1e-4));
	float maxT = max(max(t.x, t.y), t.z);
	if (maxT > 1.0)
		return center + delta / maxT;
	return hist;
}

float Lanczos2(float x)
{
	x = abs(x);
	if (x < 1e-5)
		return 1.0;
	if (x >= 2.0)
		return 0.0;
	float pix = kPi * x;
	return 2.0 * sin(pix) * sin(pix * 0.5) / (pix * pix);
}

// 显示像素中心映射回渲染图连续坐标，用 4×4 Lanczos-2 重建当前帧。
vec3 ReconstructCurrent(vec2 displayUV)
{
	vec2 renderPos = displayUV * u_RenderSize.xy - 0.5;
	vec2 fp = floor(renderPos);
	vec2 pp = renderPos - fp;

	vec3 acc = vec3(0.0);
	float wSum = 0.0;
	vec3 nmin = vec3(1e10);
	vec3 nmax = vec3(-1e10);

	for (int y = -1; y <= 2; ++y)
	{
		for (int x = -1; x <= 2; ++x)
		{
			vec2 tap = clamp(fp + vec2(float(x), float(y)), vec2(0.0), u_RenderSize.xy - 1.0);
			vec3 c = texelFetch(u_CurrColor, ivec2(tap), 0).rgb;
			float w = Lanczos2(float(x) - pp.x) * Lanczos2(float(y) - pp.y);
			acc += c * w;
			wSum += w;
			nmin = min(nmin, c);
			nmax = max(nmax, c);
		}
	}

	return clamp(acc / max(wSum, 1e-4), nmin, nmax);
}

// 3×3 里取最近表面的 velocity / 深度，避免细栏杆被背景运动带着走。
void DilateClosest(vec2 uv, out vec2 velocity, out float depth, out float depthMin, out float depthMax)
{
	vec2 texel = u_RenderSize.zw;
	velocity = textureLod(u_Velocity, uv, 0.0).xy;
	depth = textureLod(u_DepthVS, uv, 0.0).r;
	depthMin = depth;
	depthMax = depth;

	for (int y = -1; y <= 1; ++y)
	{
		for (int x = -1; x <= 1; ++x)
		{
			if (x == 0 && y == 0)
				continue;
			vec2 suv = uv + vec2(float(x), float(y)) * texel;
			float z = textureLod(u_DepthVS, suv, 0.0).r;
			depthMin = min(depthMin, z);
			depthMax = max(depthMax, z);
			if (z < depth)
			{
				depth = z;
				velocity = textureLod(u_Velocity, suv, 0.0).xy;
			}
		}
	}
}

void main()
{
	vec2 uv = v2f_TexCoords;
	vec3 curr = ReconstructCurrent(uv);

	vec2 velocity;
	float depth;
	float depthMin;
	float depthMax;
	DilateClosest(uv, velocity, depth, depthMin, depthMax);

	// 当前像素里是 jitter 后的样本；history 存在无 jitter 的像素中心。
	vec2 histUV = uv - u_Jitter.xy * 0.5 - velocity;

	float feedback = u_Params.x;
	int clampMode = int(u_Params.y + 0.5);
	bool historyValid = u_Params.z > 0.5;
	bool inside = histUV.x >= 0.0 && histUV.x <= 1.0
		&& histUV.y >= 0.0 && histUV.y <= 1.0;

	// 邻域深度跨度大，当作遮挡变化，少信 history。
	float depthGap = depthMax - depthMin;
	float disocclude = clamp(depthGap / max(depth, 1e-3), 0.0, 1.0);

	if (!historyValid || !inside)
	{
		Color_ = vec4(curr, 1.0);
		return;
	}

	vec3 hist = textureLod(u_History, histUV, 0.0).rgb;
	vec2 renderTexel = u_RenderSize.zw;

	if (clampMode == 1)
	{
		vec3 nmin = RGBToYCoCg(curr);
		vec3 nmax = nmin;
		for (int y = -1; y <= 1; ++y)
		{
			for (int x = -1; x <= 1; ++x)
			{
				vec3 n = RGBToYCoCg(textureLod(u_CurrColor, uv + vec2(float(x), float(y)) * renderTexel, 0.0).rgb);
				nmin = min(nmin, n);
				nmax = max(nmax, n);
			}
		}
		hist = YCoCgToRGB(ClipAABB(RGBToYCoCg(hist), nmin, nmax));
	}
	else if (clampMode == 2)
	{
		vec3 m1 = vec3(0.0);
		vec3 m2 = vec3(0.0);
		for (int y = -1; y <= 1; ++y)
		{
			for (int x = -1; x <= 1; ++x)
			{
				vec3 n = textureLod(u_CurrColor, uv + vec2(float(x), float(y)) * renderTexel, 0.0).rgb;
				m1 += n;
				m2 += n * n;
			}
		}
		m1 /= 9.0;
		m2 /= 9.0;
		vec3 sigma = sqrt(max(m2 - m1 * m1, vec3(0.0)));
		const float gamma = 1.25;
		hist = clamp(hist, m1 - gamma * sigma, m1 + gamma * sigma);
	}

	float currW = clamp(feedback + (1.0 - feedback) * disocclude, 0.0, 1.0);
	vec3 result = mix(hist, curr, currW);
	Color_ = vec4(result, 1.0);
}
