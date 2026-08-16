#version 430 core

// Clustered Forward 分块剔灯：每个 workgroup 对应一个 16×16 XY tile，
// 内部循环 Z_SLICES 个预定义指数深度 slice。
//   1) 用 invProj 把 tile 四角反投影
//   2) 每个 slice 用 z(s)..z(s+1) 建视空间 AABB
//   3) 点光包围球 vs AABB，按灯 index 稳定写入每 cluster 灯表（SSBO）
// 不读预通道深度。切片公式必须与 ForwardPlus_FS.glsl 的 ClusterSlice 对齐：
//   z(s) = -near * (far/near)^(s / Z_SLICES)
// TILE_* / Z_SLICES / CLUSTER_* 必须与 ForwardPlusParams / ForwardPlus_FS.glsl 对齐。

#define TILE_SIZE 16
#define MAX_LIGHTS 1000
#define Z_SLICES 16
#define MAX_LIGHTS_PER_CLUSTER 256
#define CLUSTER_STRIDE (1 + MAX_LIGHTS_PER_CLUSTER)

layout(local_size_x = TILE_SIZE, local_size_y = TILE_SIZE) in;

#ifdef VULKAN
#define LAYOUT_BIND(s, b) layout(set = s, binding = b)
#else
#define LAYOUT_BIND(s, b) layout(binding = b)
#endif

struct PointLight
{
	vec4 positionVSAndRadius;
	vec4 colorAndIntensity;
};

LAYOUT_BIND(0, 0) layout(std140) uniform u_LightBlock
{
	PointLight u_Lights[MAX_LIGHTS];
	ivec4      u_LightCount;
};

LAYOUT_BIND(0, 1) layout(std430) buffer u_TileLightList
{
	uint u_TileData[];
};

LAYOUT_BIND(0, 2) layout(std140) uniform u_CullParams
{
	mat4  u_InvProj;
	ivec4 u_ScreenAndTiles; // x=width, y=height, z=tilesX, w=tilesY
	vec4  u_ClusterZ;       // x=near, y=far, z=zSlices, w=log(far/near)
};

shared vec3  s_AabbMin;
shared vec3  s_AabbMax;
shared uint  s_Hit[MAX_LIGHTS];
shared uint  s_Indices[MAX_LIGHTS_PER_CLUSTER];

vec3 Unproject(vec2 ndcXY, float depth01)
{
	vec4 clip = vec4(ndcXY, depth01, 1.0);
	vec4 view = u_InvProj * clip;
	return view.xyz / max(view.w, 1e-6);
}

vec3 ViewPosAtZ(vec2 ndcXY, float viewZ)
{
	vec3 p0 = Unproject(ndcXY, 0.0);
	vec3 p1 = Unproject(ndcXY, 1.0);
	float denom = p1.z - p0.z;
	float t = abs(denom) > 1e-6 ? (viewZ - p0.z) / denom : 0.0;
	return mix(p0, p1, t);
}

// 与 ForwardPlus_FS.glsl ClusterSlice 同一套指数划分（视空间看向 -Z）。
float SliceViewZ(int s)
{
	float nearZ = max(u_ClusterZ.x, 1e-4);
	float logR  = max(u_ClusterZ.w, 1e-6);
	float slices = max(u_ClusterZ.z, 1.0);
	float t = float(s) / slices;
	return -nearZ * exp(t * logR);
}

bool SphereAabb(vec3 c, float r, vec3 bmin, vec3 bmax)
{
	vec3 q = clamp(c, bmin, bmax);
	vec3 d = c - q;
	return dot(d, d) <= r * r;
}

void main()
{
	const int screenW = u_ScreenAndTiles.x;
	const int screenH = u_ScreenAndTiles.y;
	const int tilesX  = u_ScreenAndTiles.z;
	const int tilesY  = u_ScreenAndTiles.w;

	const uint tileX = gl_WorkGroupID.x;
	const uint tileY = gl_WorkGroupID.y;
	if (int(tileX) >= tilesX || int(tileY) >= tilesY)
		return;

	const uint lid = gl_LocalInvocationIndex;
	const int lightCount = min(u_LightCount.x, MAX_LIGHTS);
	const int zSlices = clamp(int(u_ClusterZ.z + 0.5), 1, Z_SLICES);

	const vec2 invScreen = vec2(1.0 / float(max(screenW, 1)), 1.0 / float(max(screenH, 1)));
	const float x0 = float(tileX * uint(TILE_SIZE));
	const float y0 = float(tileY * uint(TILE_SIZE));
	const float x1 = min(x0 + float(TILE_SIZE), float(screenW));
	const float y1 = min(y0 + float(TILE_SIZE), float(screenH));

	vec2 ndc[4];
	ndc[0] = vec2(x0, y0) * invScreen * 2.0 - 1.0;
	ndc[1] = vec2(x1, y0) * invScreen * 2.0 - 1.0;
	ndc[2] = vec2(x0, y1) * invScreen * 2.0 - 1.0;
	ndc[3] = vec2(x1, y1) * invScreen * 2.0 - 1.0;

	const uint tileIndex = tileY * uint(tilesX) + tileX;

	for (int s = 0; s < zSlices; ++s)
	{
		if (lid == 0u)
		{
			const float zN = SliceViewZ(s);
			const float zF = SliceViewZ(s + 1);
			vec3 bmin = vec3( 1e20);
			vec3 bmax = vec3(-1e20);
			for (int i = 0; i < 4; ++i)
			{
				vec3 pN = ViewPosAtZ(ndc[i], zN);
				vec3 pF = ViewPosAtZ(ndc[i], zF);
				bmin = min(bmin, min(pN, pF));
				bmax = max(bmax, max(pN, pF));
			}
			s_AabbMin = bmin;
			s_AabbMax = bmax;
		}
		barrier();

		const vec3 bmin = s_AabbMin;
		const vec3 bmax = s_AabbMax;

		for (int i = int(lid); i < lightCount; i += TILE_SIZE * TILE_SIZE)
		{
			vec3  c = u_Lights[i].positionVSAndRadius.xyz;
			float r = u_Lights[i].positionVSAndRadius.w;
			s_Hit[i] = SphereAabb(c, r, bmin, bmax) ? 1u : 0u;
		}
		barrier();

		if (lid == 0u)
		{
			uint count = 0u;
			for (int i = 0; i < lightCount && count < uint(MAX_LIGHTS_PER_CLUSTER); ++i)
			{
				if (s_Hit[i] != 0u)
					s_Indices[count++] = uint(i);
			}

			const uint clusterIndex = tileIndex * uint(zSlices) + uint(s);
			const uint base = clusterIndex * uint(CLUSTER_STRIDE);
			u_TileData[base] = count;
			for (uint i = 0u; i < count; ++i)
				u_TileData[base + 1u + i] = s_Indices[i];
		}
		barrier();
	}
}
