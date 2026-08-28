#version 430 core
// 背面外扩。沿平滑法线挤 xy，外扩量在屏幕空间（像素）里等宽，随视距收细。
layout(location = 0) in vec3 _Position;
layout(location = 1) in vec3 _Normal;        // 硬边法线，Cel 着色用，这里不读
layout(location = 2) in vec2 _TexCoord;
layout(location = 3) in vec3 _SmoothNormal;  // OutlineBake 把平滑法线烘在 tangent 槽
layout(location = 4) in vec3 _OutlineAttr;   // x=partIndex，yz 空闲

#ifdef VULKAN
#define LAYOUT_BIND(s, b) layout(set = s, binding = b)
#else
#define LAYOUT_BIND(s, b) layout(binding = b)
#endif

LAYOUT_BIND(0, 0) layout(std140) uniform u_Outline
{
	mat4 u_ProjectionMatrix;
	mat4 u_ViewMatrix;
	vec4 u_OutlineParams;    // x=basePx  y=zBias  zw=viewportSize
	vec4 u_OutlineWidth;     // x=minPx   y=maxPx  z=refDistance  w=falloffPower
	vec4 u_OutlineFade;      // x=fadeStartZ  y=fadeEndZ  z=fadeStrength
	vec4 u_OutlineFadeColor; // rgb=远处描边目标色
	vec4 u_OutlinePart[4];   // 按 Part 索引：rgb=描边色 a=线宽倍率
};

#ifdef VULKAN
layout(push_constant) uniform PC {
	layout(offset = 0) mat4 u_ModelMatrix;
} pc;
#define u_ModelMatrix pc.u_ModelMatrix
#else
uniform mat4 u_ModelMatrix;
#endif

// 逐顶点插值：同一三角形的三个顶点必属同一 SubMesh，partIndex 一致，
// 只有 fade 在变，插值比 flat 更平滑。
layout(location = 0) out vec3 v2f_OutlineColor;

void main()
{
	mat4 mv = u_ViewMatrix * u_ModelMatrix;
	vec4 posVs = mv * vec4(_Position, 1.0);
	// 用平滑法线而非 _Normal：模型有 1/3 的位置带分裂法线，按硬边法线外扩
	// 会把壳体在硬边和 UV 缝处撕开。
	vec3 nVs = normalize(mat3(transpose(inverse(mv))) * _SmoothNormal);

	float viewZ = max(-posVs.z, 1e-3);

	// 沿视空间往相机挪一点，让轮廓处的深度比较不再落在临界值上。
	// 偏移放在视空间按米算，而不是改 NDC z：near 0.05 / far 40 下整个角色只
	// 占约 0.0013 的 NDC 深度，同样的 NDC 偏移在近处能盖住整个角色、在远处
	// 又完全失效，根本调不出一个通用值。
	posVs.z += u_OutlineParams.y;

	vec4 posCs = u_ProjectionMatrix * posVs;
	vec4 nCs = u_ProjectionMatrix * vec4(nVs, 0.0);

	// ndc 的一个单位 = halfVp 个像素。方向先换算到屏幕空间再归一化，
	// 否则宽高比会让上下轮廓比左右轮廓细（NDC 等宽 ≠ 屏幕等宽）。
	vec2 halfVp = max(u_OutlineParams.zw * 0.5, vec2(1.0));
	vec2 nSs = nCs.xy * halfVp;
	float nLenSs = length(nSs);
	// 法线正对/背对相机时投影退化到零长，改为沿屏幕中心向外推，
	// 否则这些顶点外扩量为 0，壳体在那里塌陷。
	vec2 fallbackSs = posCs.xy * halfVp;
	vec2 dirSs = nLenSs > 1e-5
		? (nSs / nLenSs)
		: normalize(fallbackSs + vec2(1e-6));

	int partIdx = clamp(int(_OutlineAttr.x + 0.5), 0, 3);
	vec4 part = u_OutlinePart[partIdx];

	// 屏幕空间等宽 + 远处收细：viewZ 近于 refDistance 时 clamp 到 1，
	// 保证推近相机线不变粗；超过后按 falloffPower 衰减。
	float distScale = pow(clamp(u_OutlineWidth.z / viewZ, 0.0, 1.0),
	                      max(u_OutlineWidth.w, 1e-3));
	float px = clamp(u_OutlineParams.x * part.a * distScale,
	                 u_OutlineWidth.x, u_OutlineWidth.y);
	// 线宽倍率为 0 表示该部件不描边，不能被 minPx 抬回去。
	px = part.a > 0.0 ? px : 0.0;

	// ndc += offsetNdc  ⟺  posCs.xy += offsetNdc * posCs.w
	// w <= 0 的顶点在近平面之后，直接乘 w 会让偏移翻向甚至发散。
	vec2 offsetNdc = dirSs * px / halfVp;
	posCs.xy += offsetNdc * max(posCs.w, 1e-4);
	gl_Position = posCs;

	float fade = smoothstep(u_OutlineFade.x, u_OutlineFade.y, viewZ) * u_OutlineFade.z;
	v2f_OutlineColor = mix(part.rgb, u_OutlineFadeColor.rgb, fade);
}
