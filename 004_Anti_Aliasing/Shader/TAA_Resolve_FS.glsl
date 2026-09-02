#version 430 core

// TAA 时域合成：按 velocity 回看 history，邻域 AABB / variance clip 压鬼影。
layout(location = 0) in vec2 v2f_TexCoords;
layout(location = 0) out vec4 Color_;

#ifdef VULKAN
#define LAYOUT_BIND(s, b) layout(set = s, binding = b)
#else
#define LAYOUT_BIND(s, b) layout(binding = b)
#endif

LAYOUT_BIND(0, 0) uniform sampler2D u_CurrColor;
LAYOUT_BIND(0, 1) uniform sampler2D u_Velocity;
LAYOUT_BIND(0, 2) uniform sampler2D u_History;

LAYOUT_BIND(0, 3) layout(std140) uniform u_TAAParams
{
	vec4 u_Params; // x: 当前帧混合权重, y: clamp 模式, z: history 是否有效
	vec4 u_Jitter; // xy: 当前帧 NDC jitter（换算 UV 时 × 0.5）
};

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

// 沿中心到 hist 的方向裁到 AABB，比分量 clamp 少染色。
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

void main()
{
	vec2 uv = v2f_TexCoords;
	vec3 curr = textureLod(u_CurrColor, uv, 0.0).rgb;
	vec2 velocity = textureLod(u_Velocity, uv, 0.0).xy;

	// 当前像素里是 jitter 后的样本；history 存在无 jitter 的像素中心。
	vec2 histUV = uv - u_Jitter.xy * 0.5 - velocity;

	float feedback = u_Params.x;
	int clampMode = int(u_Params.y + 0.5);
	bool historyValid = u_Params.z > 0.5;
	bool inside = histUV.x >= 0.0 && histUV.x <= 1.0
		&& histUV.y >= 0.0 && histUV.y <= 1.0;

	if (!historyValid || !inside)
	{
		Color_ = vec4(curr, 1.0);
		return;
	}

	vec3 hist = textureLod(u_History, histUV, 0.0).rgb;
	vec2 texel = 1.0 / vec2(textureSize(u_CurrColor, 0));

	if (clampMode == 1)
	{
		vec3 nmin = RGBToYCoCg(curr);
		vec3 nmax = nmin;
		for (int y = -1; y <= 1; ++y)
		{
			for (int x = -1; x <= 1; ++x)
			{
				if (x == 0 && y == 0)
					continue;
				vec3 n = RGBToYCoCg(textureLod(u_CurrColor, uv + vec2(float(x), float(y)) * texel, 0.0).rgb);
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
				vec3 n = textureLod(u_CurrColor, uv + vec2(float(x), float(y)) * texel, 0.0).rgb;
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

	vec3 result = mix(hist, curr, clamp(feedback, 0.0, 1.0));
	Color_ = vec4(result, 1.0);
}
