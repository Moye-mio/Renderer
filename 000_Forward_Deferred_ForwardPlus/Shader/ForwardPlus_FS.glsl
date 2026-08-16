#version 430 core

// Forward+ 着色 FS：只遍历本 tile 的灯表，BRDF 与 Forward / Deferred 对齐。
layout(location = 0) in vec2 v2f_TexCoords;
layout(location = 1) in vec3 v2f_Normal;
layout(location = 2) in vec3 v2f_FragPosInViewSpace;
layout(location = 0) out vec4 Color_;

#ifdef VULKAN
#define LAYOUT_BIND(s, b) layout(set = s, binding = b)
#else
#define LAYOUT_BIND(s, b) layout(binding = b)
#endif

#define MAX_LIGHTS 1000
#define TILE_SIZE 16
#define MAX_LIGHTS_PER_TILE 256
#define TILE_STRIDE (1 + MAX_LIGHTS_PER_TILE)

struct PointLight
{
	vec4 positionVSAndRadius;
	vec4 colorAndIntensity;
};

LAYOUT_BIND(0, 2) layout(std140) uniform u_LightBlock
{
	PointLight u_Lights[MAX_LIGHTS];
	ivec4      u_LightCount;
};

LAYOUT_BIND(0, 1) uniform sampler2D u_DiffuseTexture;

LAYOUT_BIND(0, 3) layout(std430) readonly buffer u_TileLightList
{
	uint u_TileData[];
};

LAYOUT_BIND(0, 4) layout(std140) uniform u_CullParams
{
	mat4  u_InvProj;
	ivec4 u_ScreenAndTiles; // x=width, y=height, z=tilesX, w=tilesY
};

#ifdef VULKAN
layout(push_constant) uniform PC {
	layout(offset = 64) int u_DebugView;
} pc;
#define u_DebugView pc.u_DebugView
#else
uniform int u_DebugView;
#endif

void main()
{
	vec3 albedo    = texture(u_DiffuseTexture, v2f_TexCoords).rgb;
	vec3 N         = normalize(v2f_Normal);
	vec3 fragPosVS = v2f_FragPosInViewSpace;
	vec3 V         = normalize(-fragPosVS);

	const int tilesX = max(u_ScreenAndTiles.z, 1);
	const uint tx = uint(gl_FragCoord.x) / uint(TILE_SIZE);
	const uint ty = uint(gl_FragCoord.y) / uint(TILE_SIZE);
	const uint base = (ty * uint(tilesX) + tx) * uint(TILE_STRIDE);
	const uint count = min(u_TileData[base], uint(MAX_LIGHTS_PER_TILE));

	if (u_DebugView == 1)
	{
		float t = float(count) / float(MAX_LIGHTS_PER_TILE);
		Color_ = vec4(t, 1.0 - t, 0.15, 1.0);
		return;
	}

	vec3 result = albedo * 0.08;

	for (uint n = 0u; n < count; ++n)
	{
		uint i = u_TileData[base + 1u + n];
		if (i >= uint(MAX_LIGHTS))
			continue;

		vec3  Lpos      = u_Lights[i].positionVSAndRadius.xyz;
		float radius    = u_Lights[i].positionVSAndRadius.w;
		vec3  Lcol      = u_Lights[i].colorAndIntensity.rgb;
		float intensity = u_Lights[i].colorAndIntensity.w;

		vec3  Lvec = Lpos - fragPosVS;
		float dist = length(Lvec);
		vec3  L    = Lvec / max(dist, 1e-4);

		float att = clamp(1.0 - dist / max(radius, 1e-4), 0.0, 1.0);
		att = att * att;

		float diff = max(dot(N, L), 0.0);
		vec3  H    = normalize(L + V);
		float spec = pow(max(dot(N, H), 0.0), 32.0);

		result += (albedo * diff + vec3(0.25) * spec) * Lcol * intensity * att;
	}

	Color_ = vec4(result, 1.0);
}
