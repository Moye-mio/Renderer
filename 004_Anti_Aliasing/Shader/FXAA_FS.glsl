#version 430 core

// FXAA 3.11 Quality：LDR 颜色上算 luma、找边、沿边搜索端点后混合。
// 无历史帧。u_Params 对应 Lottes 的 subpix / edgeThreshold / edgeThresholdMin。
layout(location = 0) in vec2 v2f_TexCoords;
layout(location = 0) out vec4 Color_;

#ifdef VULKAN
#define LAYOUT_BIND(s, b) layout(set = s, binding = b)
#else
#define LAYOUT_BIND(s, b) layout(binding = b)
#endif

LAYOUT_BIND(0, 0) uniform sampler2D u_InputColor;

LAYOUT_BIND(0, 1) layout(std140) uniform u_FXAAParams
{
	vec4 u_Params; // x: subpix, y: edgeThreshold, z: edgeThresholdMin
};

float Luma(vec3 rgb)
{
	return dot(rgb, vec3(0.299, 0.587, 0.114));
}

void main()
{
	vec2 texel = 1.0 / vec2(textureSize(u_InputColor, 0));
	vec2 uv = v2f_TexCoords;

	float subpix = u_Params.x;
	float edgeThreshold = u_Params.y;
	float edgeThresholdMin = u_Params.z;

	vec3 rgbM = textureLod(u_InputColor, uv, 0.0).rgb;
	float lumaM = Luma(rgbM);

	float lumaS = Luma(textureLod(u_InputColor, uv + vec2( 0.0,  1.0) * texel, 0.0).rgb);
	float lumaE = Luma(textureLod(u_InputColor, uv + vec2( 1.0,  0.0) * texel, 0.0).rgb);
	float lumaN = Luma(textureLod(u_InputColor, uv + vec2( 0.0, -1.0) * texel, 0.0).rgb);
	float lumaW = Luma(textureLod(u_InputColor, uv + vec2(-1.0,  0.0) * texel, 0.0).rgb);

	float lumaMin = min(lumaM, min(min(lumaN, lumaW), min(lumaS, lumaE)));
	float lumaMax = max(lumaM, max(max(lumaN, lumaW), max(lumaS, lumaE)));
	float lumaRange = lumaMax - lumaMin;
	if (lumaRange < max(edgeThresholdMin, lumaMax * edgeThreshold))
	{
		Color_ = vec4(rgbM, 1.0);
		return;
	}

	float lumaNW = Luma(textureLod(u_InputColor, uv + vec2(-1.0, -1.0) * texel, 0.0).rgb);
	float lumaNE = Luma(textureLod(u_InputColor, uv + vec2( 1.0, -1.0) * texel, 0.0).rgb);
	float lumaSW = Luma(textureLod(u_InputColor, uv + vec2(-1.0,  1.0) * texel, 0.0).rgb);
	float lumaSE = Luma(textureLod(u_InputColor, uv + vec2( 1.0,  1.0) * texel, 0.0).rgb);

	float lumaNS = lumaN + lumaS;
	float lumaWE = lumaW + lumaE;
	float lumaNESE = lumaNE + lumaSE;
	float lumaNWNE = lumaNW + lumaNE;
	float lumaNWSW = lumaNW + lumaSW;
	float lumaSWSE = lumaSW + lumaSE;

	float edgeHorz = abs(-2.0 * lumaW + lumaNWSW)
		+ abs(-2.0 * lumaM + lumaNS) * 2.0
		+ abs(-2.0 * lumaE + lumaNESE);
	float edgeVert = abs(-2.0 * lumaN + lumaNWNE)
		+ abs(-2.0 * lumaM + lumaWE) * 2.0
		+ abs(-2.0 * lumaS + lumaSWSE);
	bool horzSpan = edgeHorz >= edgeVert;

	float luma1 = horzSpan ? lumaN : lumaW;
	float luma2 = horzSpan ? lumaS : lumaE;
	float gradient1 = luma1 - lumaM;
	float gradient2 = luma2 - lumaM;
	bool pair1Steeper = abs(gradient1) >= abs(gradient2);
	float gradientScaled = 0.25 * max(abs(gradient1), abs(gradient2));

	float stepLength = horzSpan ? texel.y : texel.x;
	if (!pair1Steeper)
		stepLength = -stepLength;

	float lumaLocalAvg = pair1Steeper
		? 0.5 * (luma1 + lumaM)
		: 0.5 * (luma2 + lumaM);

	vec2 uvEdge = uv;
	if (horzSpan)
		uvEdge.y += stepLength * 0.5;
	else
		uvEdge.x += stepLength * 0.5;

	vec2 offset = horzSpan ? vec2(texel.x, 0.0) : vec2(0.0, texel.y);
	vec2 uvP = uvEdge + offset;
	vec2 uvN = uvEdge - offset;

	float lumaEndP = Luma(textureLod(u_InputColor, uvP, 0.0).rgb) - lumaLocalAvg;
	float lumaEndN = Luma(textureLod(u_InputColor, uvN, 0.0).rgb) - lumaLocalAvg;
	bool doneP = abs(lumaEndP) >= gradientScaled;
	bool doneN = abs(lumaEndN) >= gradientScaled;
	if (!doneP) uvP += offset;
	if (!doneN) uvN -= offset;

	// Quality 12：1 / 1.5 / 2 / 4 / 12
	const float kSpan[5] = {1.0, 1.5, 2.0, 4.0, 12.0};
	if (!doneP || !doneN)
	{
		for (int i = 0; i < 5; ++i)
		{
			if (!doneP)
			{
				lumaEndP = Luma(textureLod(u_InputColor, uvP, 0.0).rgb) - lumaLocalAvg;
				doneP = abs(lumaEndP) >= gradientScaled;
			}
			if (!doneN)
			{
				lumaEndN = Luma(textureLod(u_InputColor, uvN, 0.0).rgb) - lumaLocalAvg;
				doneN = abs(lumaEndN) >= gradientScaled;
			}
			if (doneP && doneN)
				break;
			if (!doneP) uvP += offset * kSpan[i];
			if (!doneN) uvN -= offset * kSpan[i];
		}
	}

	float distP = horzSpan ? (uvP.x - uv.x) : (uvP.y - uv.y);
	float distN = horzSpan ? (uv.x - uvN.x) : (uv.y - uvN.y);
	bool dstPCloser = distP < distN;
	float distMin = min(distP, distN);
	float spanLength = distP + distN;

	bool lumaMLT = lumaM < lumaLocalAvg;
	bool goodSpan = ((dstPCloser ? lumaEndP : lumaEndN) < 0.0) != lumaMLT;
	float pixelOffset = (distMin < 0.0 || spanLength <= 0.0)
		? 0.0
		: (-distMin / spanLength + 0.5);
	if (!goodSpan)
		pixelOffset = 0.0;

	float lumaAvg = (2.0 * (lumaNS + lumaWE) + lumaNWSW + lumaNESE) * (1.0 / 12.0);
	float subpixA = clamp(abs(lumaAvg - lumaM) / max(lumaRange, 1e-5), 0.0, 1.0);
	float subpixB = (-2.0 * subpixA + 3.0) * subpixA * subpixA;
	float subpixC = subpixB * subpixB * subpix;
	float finalOffset = max(pixelOffset, subpixC);

	vec2 uvOut = uv;
	if (horzSpan)
		uvOut.y += finalOffset * stepLength;
	else
		uvOut.x += finalOffset * stepLength;

	Color_ = vec4(textureLod(u_InputColor, uvOut, 0.0).rgb, 1.0);
}
