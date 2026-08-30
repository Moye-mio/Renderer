#version 430 core
// 屏幕空间内线：法线不连续（折边 / 硬边 / UV 缝）+ 深度不连续（自遮挡）。
// 邻像素落在背景上的边是外轮廓，背面外扩已经画过，这里跳过以免描两遍。
layout(location = 0) out vec4 Color_;

#ifdef VULKAN
#define LAYOUT_BIND(s, b) layout(set = s, binding = b)
#else
#define LAYOUT_BIND(s, b) layout(binding = b)
#endif

LAYOUT_BIND(0, 0) layout(std140) uniform u_Crease
{
	vec4 u_Detect;     // x=nThresh  y=nSoft  z=dThresh  w=dSoft
	vec4 u_WidthCtrl;  // x=basePx   y=minPx  z=maxPx    w=refDistance
	vec4 u_FadeCtrl;   // x=fadeStart y=fadeEnd z=fadeStrength w=falloffPower
	vec4 u_FadeColor;  // rgb=远处线色  w=enable（<0.5 只做颜色拷贝）
	vec4 u_Background; // rgb=合成背景（离屏 MRT 清 0，背景在这里补）
	vec4 u_Part[4];    // 按 Part：rgb=线色 a=线宽倍率
};

LAYOUT_BIND(0, 1) uniform sampler2D u_SceneColor;
LAYOUT_BIND(0, 2) uniform sampler2D u_CreaseGBuffer;

vec3 OctDecode(vec2 e)
{
	vec3 n = vec3(e.x, e.y, 1.0 - abs(e.x) - abs(e.y));
	if (n.z < 0.0)
		n.xy = (vec2(1.0) - abs(n.yx)) * vec2(e.x >= 0.0 ? 1.0 : -1.0, e.y >= 0.0 ? 1.0 : -1.0);
	return normalize(n);
}

float EdgeAt(ivec2 q, vec3 n0, float z0, float part0, float nThresh, float nSoft,
             float dThresh, float dSoft)
{
	ivec2 size = textureSize(u_CreaseGBuffer, 0);
	if (q.x < 0 || q.y < 0 || q.x >= size.x || q.y >= size.y)
		return 0.0;

	vec4 g1 = texelFetch(u_CreaseGBuffer, q, 0);
	// 邻像素是背景：这是外轮廓，不在这里画。
	if (g1.z < 1e-4)
		return 0.0;

	vec3 n1 = OctDecode(g1.xy);
	float nd = 1.0 - clamp(dot(n0, n1), 0.0, 1.0);
	float dd = abs(z0 - g1.z) / max(min(z0, g1.z), 1e-3);
	float partEdge = abs(part0 - g1.w) > 0.5 ? 1.0 : 0.0;
	return max(partEdge, max(smoothstep(nThresh, nThresh + nSoft, nd),
	                         smoothstep(dThresh, dThresh + dSoft, dd)));
}

void main()
{
	ivec2 p = ivec2(gl_FragCoord.xy);
	vec4 g0 = texelFetch(u_CreaseGBuffer, p, 0);
	if (g0.z < 1e-4)
	{
		Color_ = vec4(u_Background.rgb, 1.0);
		return;
	}

	vec3 scene = texelFetch(u_SceneColor, p, 0).rgb;
	if (u_FadeColor.w < 0.5)
	{
		Color_ = vec4(scene, 1.0);
		return;
	}

	vec3 n0 = OctDecode(g0.xy);
	float z0 = g0.z;
	int partIdx = clamp(int(g0.w + 0.5), 0, 3);
	vec4 part = u_Part[partIdx];

	float distScale = pow(clamp(u_WidthCtrl.w / z0, 0.0, 1.0),
	                      max(u_FadeCtrl.w, 1e-3));
	float px = clamp(u_WidthCtrl.x * part.a * distScale,
	                 u_WidthCtrl.y, u_WidthCtrl.z);
	px = part.a > 0.0 ? px : 0.0;
	if (px <= 0.0)
	{
		Color_ = vec4(scene, 1.0);
		return;
	}

	// 远处抬高法线阈值，浅折边先消失，相当于减线。
	float nThresh = mix(u_Detect.x + 0.22, u_Detect.x, distScale);
	float nSoft = max(u_Detect.y, 1e-3);
	float dThresh = u_Detect.z;
	float dSoft = max(u_Detect.w, 1e-3);

	// 8 邻域 × 若干环：第 r 环贡献 saturate(px - (r-1))，亚像素宽度靠 smoothstep。
	const ivec2 kDir[8] = ivec2[](
		ivec2( 1,  0), ivec2(-1,  0), ivec2( 0,  1), ivec2( 0, -1),
		ivec2( 1,  1), ivec2(-1,  1), ivec2( 1, -1), ivec2(-1, -1));

	float edge = 0.0;
	int rings = min(int(ceil(px)), 6);
	for (int r = 1; r <= rings; ++r)
	{
		float ring = clamp(px - float(r - 1), 0.0, 1.0);
		if (ring <= 0.0)
			break;
		for (int i = 0; i < 8; ++i)
		{
			edge = max(edge, ring * EdgeAt(p + kDir[i] * r, n0, z0, g0.w,
			                               nThresh, nSoft, dThresh, dSoft));
			if (edge >= 0.999)
				break;
		}
		if (edge >= 0.999)
			break;
	}

	float fade = smoothstep(u_FadeCtrl.x, u_FadeCtrl.y, z0) * u_FadeCtrl.z;
	vec3 lineCol = mix(part.rgb, u_FadeColor.rgb, fade);
	Color_ = vec4(mix(scene, lineCol, clamp(edge, 0.0, 1.0)), 1.0);
}
