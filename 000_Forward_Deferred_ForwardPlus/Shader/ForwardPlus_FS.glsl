#version 430 core

// Clustered Forward 着色 FS：按 tile + 指数 Z slice 查本 cluster 灯表。
// 本 Pass 复用深度预通道的 D32（LessOrEqual + 不写深度），overdraw 恒为 1，
// 所以这里的每像素灯循环只会跑一遍。灯表由 ForwardPlusCull_CS 保证 index 合法。
// BRDF 与 Forward / Deferred 对齐。切片公式必须与 ForwardPlusCull_CS.glsl 对齐：
//   t  = log((-viewZ) / near) / log(far / near)
//   tz = clamp(int(t * Z_SLICES), 0, Z_SLICES-1)
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
#define Z_SLICES 16
#define MAX_LIGHTS_PER_CLUSTER 256
#define CLUSTER_STRIDE (1 + MAX_LIGHTS_PER_CLUSTER)

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
	vec4  u_ClusterZ;       // x=near, y=far, z=zSlices, w=log(far/near)
};

#ifdef VULKAN
layout(push_constant) uniform PC {
	layout(offset = 64) int u_DebugView;
} pc;
#define u_DebugView pc.u_DebugView
#else
uniform int u_DebugView;
#endif

// 与 ForwardPlusCull_CS.glsl SliceViewZ 同一套指数划分（视空间看向 -Z）。
int ClusterSlice(float viewZ)
{
	float nearZ  = max(u_ClusterZ.x, 1e-4);
	float logR   = max(u_ClusterZ.w, 1e-6);
	int   slices = clamp(int(u_ClusterZ.z + 0.5), 1, Z_SLICES);
	float z = max(-viewZ, nearZ);
	float t = log(z / nearZ) / logR;
	return clamp(int(t * float(slices)), 0, slices - 1);
}

void main()
{
	vec3 albedo    = texture(u_DiffuseTexture, v2f_TexCoords).rgb;
	vec3 N         = normalize(v2f_Normal);
	vec3 fragPosVS = v2f_FragPosInViewSpace;
	vec3 V         = normalize(-fragPosVS);

	const int tilesX = max(u_ScreenAndTiles.z, 1);
	const int zSlices = clamp(int(u_ClusterZ.z + 0.5), 1, Z_SLICES);
	const uint tx = uint(gl_FragCoord.x) / uint(TILE_SIZE);
	const uint ty = uint(gl_FragCoord.y) / uint(TILE_SIZE);
	const uint tz = uint(ClusterSlice(fragPosVS.z));
	const uint clusterIndex = (ty * uint(tilesX) + tx) * uint(zSlices) + tz;
	const uint base = clusterIndex * uint(CLUSTER_STRIDE);
	const uint count = min(u_TileData[base], uint(MAX_LIGHTS_PER_CLUSTER));

	if (u_DebugView == 1)
	{
		float t = float(count) / float(MAX_LIGHTS_PER_CLUSTER);
		Color_ = vec4(t, 1.0 - t, 0.15, 1.0);
		return;
	}
	if (u_DebugView == 2)
	{
		float t = float(tz) / float(max(zSlices - 1, 1));
		Color_ = vec4(t, 0.2, 1.0 - t, 1.0);
		return;
	}

	vec3 result = albedo * 0.08;

	for (uint n = 0u; n < count; ++n)
	{
		uint i = u_TileData[base + 1u + n];

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
