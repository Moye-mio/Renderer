#version 430 core

// FSR 1.0 的 EASU（Edge Adaptive Spatial Upsampling）：把低分辨率颜色放大到显示分辨率。
// 用亮度估计局部边缘方向与强度，把带负瓣的 Lanczos 近似核旋到边缘坐标系再各向异性缩放，
// 沿边插值、垂直边缘锐化，最后用中心四像素的 min/max 压振铃。
//
// u_Mode.x 选择三条路径：
//   0 = 双线性（对照组）
//   1 = 标准 EASU：12 tap，四个 2x2 邻域按亚像素位置双线性混合边缘估计，带 dering clamp
//   2 = Mobile Friendly EASU：6 tap，只以 f 为中心估一次边，负瓣更弱，无 dering clamp
//
// 官方实现用 textureGather 一次取 2x2 省带宽，这里用 texelFetch 逐点取，
// 结果一致但偏移关系更直观。
layout(location = 0) in vec2 v2f_TexCoords;
layout(location = 0) out vec4 Color_;

#ifdef VULKAN
#define LAYOUT_BIND(s, b) layout(set = s, binding = b)
#else
#define LAYOUT_BIND(s, b) layout(binding = b)
#endif

LAYOUT_BIND(0, 0) uniform sampler2D u_InputColor;

LAYOUT_BIND(0, 1) layout(std140) uniform u_EASUParams
{
	vec4 u_Con0;       // xy: 输入/输出尺寸比，zw: 半像素对齐 0.5 * scale - 0.5
	vec4 u_InputSize;  // xy: 输入分辨率，zw: 其倒数
	vec4 u_OutputSize; // xy: 输出分辨率，zw: 其倒数
	vec4 u_Mode;       // x: 0 双线性 / 1 标准 EASU / 2 Mobile Friendly EASU
};

// 亮度近似：L = B * 0.5 + (R * 0.5 + G)，强调绿通道，比 Rec.709 点乘更省 ALU。
float EasuLuma(vec3 c)
{
	return c.b * 0.5 + (c.r * 0.5 + c.g);
}

// fp 是输入图上的整数像素格，off 是相对它的整数偏移。
vec3 EasuTexel(vec2 fp, vec2 off)
{
	vec2 p = clamp(fp + off, vec2(0.0), u_InputSize.xy - 1.0);
	return texelFetch(u_InputColor, ivec2(p), 0).rgb;
}

// 以 C 为中心、A/B/D/E 为上/左/右/下，累加边缘方向 Dir 与边缘强度 Feature。
// FeatureX = saturate(|D-B| / max(|D-C|, |C-B|))：中间没有台阶噪声才算真正的边。
void EasuSetF(inout vec2 dir, inout float len, float w,
              float lA, float lB, float lC, float lD, float lE)
{
	float lenX = max(abs(lD - lC), abs(lC - lB));
	lenX = 1.0 / max(lenX, 1e-6);
	float dirX = lD - lB;
	dir.x += dirX * w;
	lenX = clamp(abs(dirX) * lenX, 0.0, 1.0);
	len += lenX * lenX * w;

	float lenY = max(abs(lE - lC), abs(lC - lA));
	lenY = 1.0 / max(lenY, 1e-6);
	float dirY = lE - lA;
	dir.y += dirY * w;
	lenY = clamp(abs(dirY) * lenY, 0.0, 1.0);
	len += lenY * lenY * w;
}

// 归一化 Dir、压 Feature，导出各向异性长度 len2、负瓣强度 lob 与距离裁剪 clp。
void EasuKernel(inout vec2 dir, inout float len, bool mobile,
                out vec2 len2, out float lob, out float clp)
{
	float dirR = dot(dir, dir);
	// 平坦区（梯度过小）强制 Dir = (1, 0)，避免除零
	dir = (dirR < (1.0 / 32768.0)) ? vec2(1.0, 0.0) : dir * inversesqrt(dirR);

	// Feature 从 {0,2} 压到 {0,1} 再平方：弱边缘几乎不改变滤波核
	len = len * 0.5;
	len *= len;

	// 沿边缘拉长到 stretch，垂直边缘压窄到 0.5
	float stretch = 1.0 / max(abs(dir.x), abs(dir.y));
	len2 = vec2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);

	// 负瓣强度：移动版范围更窄、更保守，配合没有 dering clamp 也不容易振铃
	lob = mobile ? (4.0 - len) / 12.0 : 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;
	clp = 1.0 / lob;
}

// 单个 tap：偏移旋到边缘坐标系 → 各向异性缩放 → 近似 Lanczos2 窗求权重。
void EasuTap(inout vec3 aC, inout float aW, vec2 off, vec2 dir, vec2 len2,
             float lob, float clp, vec3 c)
{
	vec2 v = vec2(off.x * dir.x + off.y * dir.y,
	              off.x * -dir.y + off.y * dir.x);
	v *= len2;
	// 角上的 tap 容易跑出窗外，用 clp 限制距离
	float d2 = min(dot(v, v), clp);

	// (25/16 * (2/5 * x^2 - 1)^2 - (25/16 - 1)) * (lob * x^2 - 1)^2
	float wB = 2.0 / 5.0 * d2 - 1.0;
	float wA = lob * d2 - 1.0;
	wB *= wB;
	wA *= wA;
	wB = 25.0 / 16.0 * wB - (25.0 / 16.0 - 1.0);
	float w = wB * wA;

	aC += c * w;
	aW += w;
}

// 标准 EASU：12 tap
//         b   c
//     e   f   g   h
//     i   j   k   l
//         n   o
vec3 EasuStandard(vec2 fp, vec2 pp)
{
	vec3 cB = EasuTexel(fp, vec2( 0.0, -1.0));
	vec3 cC = EasuTexel(fp, vec2( 1.0, -1.0));
	vec3 cE = EasuTexel(fp, vec2(-1.0,  0.0));
	vec3 cF = EasuTexel(fp, vec2( 0.0,  0.0));
	vec3 cG = EasuTexel(fp, vec2( 1.0,  0.0));
	vec3 cH = EasuTexel(fp, vec2( 2.0,  0.0));
	vec3 cI = EasuTexel(fp, vec2(-1.0,  1.0));
	vec3 cJ = EasuTexel(fp, vec2( 0.0,  1.0));
	vec3 cK = EasuTexel(fp, vec2( 1.0,  1.0));
	vec3 cL = EasuTexel(fp, vec2( 2.0,  1.0));
	vec3 cN = EasuTexel(fp, vec2( 0.0,  2.0));
	vec3 cO = EasuTexel(fp, vec2( 1.0,  2.0));

	float lB = EasuLuma(cB);
	float lC = EasuLuma(cC);
	float lE = EasuLuma(cE);
	float lF = EasuLuma(cF);
	float lG = EasuLuma(cG);
	float lH = EasuLuma(cH);
	float lI = EasuLuma(cI);
	float lJ = EasuLuma(cJ);
	float lK = EasuLuma(cK);
	float lL = EasuLuma(cL);
	float lN = EasuLuma(cN);
	float lO = EasuLuma(cO);

	// 四个 2x2 邻域分别以 f / g / j / k 为中心，按亚像素位置 pp 做双线性加权
	vec2 dir = vec2(0.0);
	float len = 0.0;
	EasuSetF(dir, len, (1.0 - pp.x) * (1.0 - pp.y), lB, lE, lF, lG, lJ);
	EasuSetF(dir, len,         pp.x * (1.0 - pp.y), lC, lF, lG, lH, lK);
	EasuSetF(dir, len, (1.0 - pp.x) *         pp.y, lF, lI, lJ, lK, lN);
	EasuSetF(dir, len,         pp.x *         pp.y, lG, lJ, lK, lL, lO);

	vec2 len2;
	float lob;
	float clp;
	EasuKernel(dir, len, false, len2, lob, clp);

	vec3 aC = vec3(0.0);
	float aW = 0.0;
	EasuTap(aC, aW, vec2( 0.0, -1.0) - pp, dir, len2, lob, clp, cB);
	EasuTap(aC, aW, vec2( 1.0, -1.0) - pp, dir, len2, lob, clp, cC);
	EasuTap(aC, aW, vec2(-1.0,  0.0) - pp, dir, len2, lob, clp, cE);
	EasuTap(aC, aW, vec2( 0.0,  0.0) - pp, dir, len2, lob, clp, cF);
	EasuTap(aC, aW, vec2( 1.0,  0.0) - pp, dir, len2, lob, clp, cG);
	EasuTap(aC, aW, vec2( 2.0,  0.0) - pp, dir, len2, lob, clp, cH);
	EasuTap(aC, aW, vec2(-1.0,  1.0) - pp, dir, len2, lob, clp, cI);
	EasuTap(aC, aW, vec2( 0.0,  1.0) - pp, dir, len2, lob, clp, cJ);
	EasuTap(aC, aW, vec2( 1.0,  1.0) - pp, dir, len2, lob, clp, cK);
	EasuTap(aC, aW, vec2( 2.0,  1.0) - pp, dir, len2, lob, clp, cL);
	EasuTap(aC, aW, vec2( 0.0,  2.0) - pp, dir, len2, lob, clp, cN);
	EasuTap(aC, aW, vec2( 1.0,  2.0) - pp, dir, len2, lob, clp, cO);

	// Dering：负瓣在高对比边旁会过冲，钳到中心四像素的极值
	vec3 min4 = min(min(cF, cG), min(cJ, cK));
	vec3 max4 = max(max(cF, cG), max(cJ, cK));
	return clamp(aC / max(aW, 1e-4), min4, max4);
}

// Mobile Friendly EASU：只取内圈 6 点
//         b
//     e   f   g
//         j   k
vec3 EasuMobile(vec2 fp, vec2 pp)
{
	vec3 cB = EasuTexel(fp, vec2( 0.0, -1.0));
	vec3 cE = EasuTexel(fp, vec2(-1.0,  0.0));
	vec3 cF = EasuTexel(fp, vec2( 0.0,  0.0));
	vec3 cG = EasuTexel(fp, vec2( 1.0,  0.0));
	vec3 cJ = EasuTexel(fp, vec2( 0.0,  1.0));
	vec3 cK = EasuTexel(fp, vec2( 1.0,  1.0));

	// 只以 f 为中心估一次边，等权，不做四邻域双线性混合；k 只参与滤波
	vec2 dir = vec2(0.0);
	float len = 0.0;
	EasuSetF(dir, len, 1.0,
	         EasuLuma(cB), EasuLuma(cE), EasuLuma(cF), EasuLuma(cG), EasuLuma(cJ));

	vec2 len2;
	float lob;
	float clp;
	EasuKernel(dir, len, true, len2, lob, clp);

	vec3 aC = vec3(0.0);
	float aW = 0.0;
	EasuTap(aC, aW, vec2( 0.0, -1.0) - pp, dir, len2, lob, clp, cB);
	EasuTap(aC, aW, vec2(-1.0,  0.0) - pp, dir, len2, lob, clp, cE);
	EasuTap(aC, aW, vec2( 0.0,  0.0) - pp, dir, len2, lob, clp, cF);
	EasuTap(aC, aW, vec2( 1.0,  0.0) - pp, dir, len2, lob, clp, cG);
	EasuTap(aC, aW, vec2( 0.0,  1.0) - pp, dir, len2, lob, clp, cJ);
	EasuTap(aC, aW, vec2( 1.0,  1.0) - pp, dir, len2, lob, clp, cK);

	// 无 dering clamp
	return aC / max(aW, 1e-4);
}

void main()
{
	if (u_Mode.x < 0.5)
	{
		Color_ = vec4(textureLod(u_InputColor, v2f_TexCoords, 0.0).rgb, 1.0);
		return;
	}

	// 输出整数像素 ip → 输入连续坐标 P，拆成整数格 fp 与格内亚像素偏移 pp
	vec2 ip = v2f_TexCoords * u_OutputSize.xy - 0.5;
	vec2 pp = ip * u_Con0.xy + u_Con0.zw;
	vec2 fp = floor(pp);
	pp -= fp;

	vec3 color = (u_Mode.x > 1.5) ? EasuMobile(fp, pp) : EasuStandard(fp, pp);
	Color_ = vec4(color, 1.0);
}
