#version 430 core

// Clustered Forward 分块剔灯：一个 workgroup 负责一个 cluster，
// dispatch = (tilesX, tilesY, Z_SLICES)。全流程无单线程串行段：
//   1) 每个线程各自反投影 tile 四角、算 cluster 的视空间 AABB（重算比走 shared 便宜）
//   2) 256 个线程分摊点光包围球 vs AABB，命中的用 atomicAdd 抢 shared 槽位
//   3) 256 个线程合并写 SSBO 灯表
//
// cluster 的 Z 范围会被 ForwardPlusTileDepth_CS 归约出的 tile 实际视距收紧：
// near=0.1/far=100 的 16 段指数切分里，装着几何的那几片本身有 4~6m 厚，
// 收紧后 AABB 小一个量级；完全没有几何的 slice 直接写 count=0 返回。
//
// 切片公式必须与 ForwardPlus_FS.glsl 的 ClusterSlice 对齐（这里用正视距）：
//   dist(s) = near * (far/near)^(s / Z_SLICES)
// TILE_* / Z_SLICES / CLUSTER_* 必须与 ForwardPlusParams / ForwardPlus_FS.glsl 对齐。

#define TILE_SIZE 16
#define MAX_LIGHTS 1000
#define Z_SLICES 16
#define MAX_LIGHTS_PER_CLUSTER 256
#define CLUSTER_STRIDE (1 + MAX_LIGHTS_PER_CLUSTER)
#define CULL_GROUP_SIZE 256

layout(local_size_x = CULL_GROUP_SIZE) in;

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

LAYOUT_BIND(0, 3) layout(std430) readonly buffer u_TileDepthRange
{
	uint u_TileDepth[]; // [tile*2] = minDist bits, [tile*2+1] = maxDist bits
};

shared uint s_Count;
shared uint s_Indices[MAX_LIGHTS_PER_CLUSTER];

vec3 Unproject(vec2 ndcXY, float depth01)
{
	vec4 clip = vec4(ndcXY, depth01, 1.0);
	vec4 view = u_InvProj * clip;
	return view.xyz / max(view.w, 1e-6);
}

// p0/p1 是同一条视线上 depth01=0 / depth01=1 的两点，沿线插到指定 viewZ。
vec3 ViewPosAtZ(vec3 p0, vec3 p1, float viewZ)
{
	float denom = p1.z - p0.z;
	float t = abs(denom) > 1e-6 ? (viewZ - p0.z) / denom : 0.0;
	return mix(p0, p1, t);
}

// 与 ForwardPlus_FS.glsl ClusterSlice 同一套指数划分，返回正视距。
float SliceDist(int s)
{
	float nearZ = max(u_ClusterZ.x, 1e-4);
	float logR  = max(u_ClusterZ.w, 1e-6);
	float slices = max(u_ClusterZ.z, 1.0);
	return nearZ * exp((float(s) / slices) * logR);
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
	const int zSlices = clamp(int(u_ClusterZ.z + 0.5), 1, Z_SLICES);

	const uint tileX = gl_WorkGroupID.x;
	const uint tileY = gl_WorkGroupID.y;
	const int  slice = int(gl_WorkGroupID.z);
	if (int(tileX) >= tilesX || int(tileY) >= tilesY || slice >= zSlices)
		return;

	const uint lid = gl_LocalInvocationIndex;
	const uint tileIndex = tileY * uint(tilesX) + tileX;
	const uint base = (tileIndex * uint(zSlices) + uint(slice)) * uint(CLUSTER_STRIDE);

	// --- 用 tile 的实际深度范围收紧本 slice；下面所有分支都是 workgroup 一致的 ---
	const uint minBits = u_TileDepth[tileIndex * 2u];
	const uint maxBits = u_TileDepth[tileIndex * 2u + 1u];
	if (minBits > maxBits) // 空 tile（预通道没有几何）
	{
		if (lid == 0u) u_TileData[base] = 0u;
		return;
	}

	const float zNear = max(SliceDist(slice),     uintBitsToFloat(minBits));
	const float zFar  = min(SliceDist(slice + 1), uintBitsToFloat(maxBits));
	if (zNear > zFar) // 本 slice 落在几何之外
	{
		if (lid == 0u) u_TileData[base] = 0u;
		return;
	}

	// --- cluster 视空间 AABB：每个线程各算一份，省掉一次 barrier ---
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

	vec3 bmin = vec3( 1e20);
	vec3 bmax = vec3(-1e20);
	for (int i = 0; i < 4; ++i)
	{
		vec3 p0 = Unproject(ndc[i], 0.0);
		vec3 p1 = Unproject(ndc[i], 1.0);
		vec3 pN = ViewPosAtZ(p0, p1, -zNear);
		vec3 pF = ViewPosAtZ(p0, p1, -zFar);
		bmin = min(bmin, min(pN, pF));
		bmax = max(bmax, max(pN, pF));
	}

	// --- 并行剔灯 + atomicAdd 压缩 ---
	if (lid == 0u)
		s_Count = 0u;
	barrier();

	const uint lightCount = uint(min(u_LightCount.x, MAX_LIGHTS));
	for (uint i = lid; i < lightCount; i += uint(CULL_GROUP_SIZE))
	{
		vec3  c = u_Lights[i].positionVSAndRadius.xyz;
		float r = u_Lights[i].positionVSAndRadius.w;
		if (SphereAabb(c, r, bmin, bmax))
		{
			uint slot = atomicAdd(s_Count, 1u);
			if (slot < uint(MAX_LIGHTS_PER_CLUSTER))
				s_Indices[slot] = i;
		}
	}
	barrier();

	// --- 并行合并写回 ---
	const uint count = min(s_Count, uint(MAX_LIGHTS_PER_CLUSTER));
	if (lid == 0u)
		u_TileData[base] = count;
	for (uint i = lid; i < count; i += uint(CULL_GROUP_SIZE))
		u_TileData[base + 1u + i] = s_Indices[i];
}
