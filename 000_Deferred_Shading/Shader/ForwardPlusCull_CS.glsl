#version 430 core

// Forward+ 分块剔灯：每个 workgroup 对应一个 16×16 tile。
//   1) 从预通道 R32F 读视空间 Z，归约出 tile 近/远平面
//   2) 用 invProj 把 tile 四角反投影成视空间 AABB
//   3) 点光包围球 vs AABB，写入每 tile 的灯索引表（SSBO）
// TILE_* 必须与 ForwardPlusParams / ForwardPlus_FS.glsl 对齐。

#define TILE_SIZE 16
#define MAX_LIGHTS 1000
#define MAX_LIGHTS_PER_TILE 256
#define TILE_STRIDE (1 + MAX_LIGHTS_PER_TILE)

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

LAYOUT_BIND(0, 2) uniform sampler2D u_DepthVS;

LAYOUT_BIND(0, 3) layout(std140) uniform u_CullParams
{
	mat4  u_InvProj;
	ivec4 u_ScreenAndTiles; // x=width, y=height, z=tilesX, w=tilesY
};

shared float s_FarZ[TILE_SIZE * TILE_SIZE];
shared float s_NearZ[TILE_SIZE * TILE_SIZE];
shared vec3  s_AabbMin;
shared vec3  s_AabbMax;
shared uint  s_Count;
shared uint  s_Indices[MAX_LIGHTS_PER_TILE];

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
	const ivec2 px = ivec2(gl_GlobalInvocationID.xy);

	float z = 0.0;
	if (px.x < screenW && px.y < screenH)
		z = texelFetch(u_DepthVS, px, 0).r;

	// 视空间：相机看向 -Z，有效几何 z < 0。Clear=0 视为空像素。
	const bool valid = z < -1e-4;
	s_FarZ[lid]  = valid ? z :  1e20; // min → 最远（更负）
	s_NearZ[lid] = valid ? z : -1e20; // max → 最近（更接近 0）

	if (lid == 0u)
		s_Count = 0u;
	barrier();

	for (uint stride = (TILE_SIZE * TILE_SIZE) >> 1; stride > 0u; stride >>= 1)
	{
		if (lid < stride)
		{
			s_FarZ[lid]  = min(s_FarZ[lid],  s_FarZ[lid + stride]);
			s_NearZ[lid] = max(s_NearZ[lid], s_NearZ[lid + stride]);
		}
		barrier();
	}

	const float farZ  = s_FarZ[0];
	const float nearZ = s_NearZ[0];
	const bool hasGeo = farZ < 0.0;

	if (lid == 0u)
	{
		vec3 bmin = vec3( 1e20);
		vec3 bmax = vec3(-1e20);
		if (hasGeo)
		{
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

			for (int i = 0; i < 4; ++i)
			{
				vec3 pN = ViewPosAtZ(ndc[i], nearZ);
				vec3 pF = ViewPosAtZ(ndc[i], farZ);
				bmin = min(bmin, min(pN, pF));
				bmax = max(bmax, max(pN, pF));
			}
		}
		s_AabbMin = bmin;
		s_AabbMax = bmax;
	}
	barrier();

	const vec3 bmin = s_AabbMin;
	const vec3 bmax = s_AabbMax;

	const int lightCount = min(u_LightCount.x, MAX_LIGHTS);
	if (hasGeo)
	{
		for (int i = int(lid); i < lightCount; i += TILE_SIZE * TILE_SIZE)
		{
			vec3  c = u_Lights[i].positionVSAndRadius.xyz;
			float r = u_Lights[i].positionVSAndRadius.w;
			if (SphereAabb(c, r, bmin, bmax))
			{
				uint slot = atomicAdd(s_Count, 1u);
				if (slot < uint(MAX_LIGHTS_PER_TILE))
					s_Indices[slot] = uint(i);
			}
		}
	}
	barrier();

	if (lid == 0u)
	{
		const uint tileIndex = tileY * uint(tilesX) + tileX;
		const uint base = tileIndex * uint(TILE_STRIDE);
		const uint count = min(s_Count, uint(MAX_LIGHTS_PER_TILE));
		u_TileData[base] = count;
		for (uint i = 0u; i < count; ++i)
			u_TileData[base + 1u + i] = s_Indices[i];
	}
}
