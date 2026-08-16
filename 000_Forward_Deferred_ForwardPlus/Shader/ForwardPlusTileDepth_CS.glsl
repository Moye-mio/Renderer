#version 430 core

// Clustered Forward 第一步：把深度预通道的 R32F 视空间 Z 归约成每个 16×16 tile 的
// [minDist, maxDist]（正的视距，= -viewZ）。ForwardPlusCull_CS 用它把 cluster 的
// Z 范围收紧到该 tile 真正有几何的那一段，并整片跳过没有几何的 slice。
//
// 正浮点数的 IEEE754 位模式与数值同序，所以可以直接对 uint 做 atomicMin/atomicMax。
// 空 tile 会留下 min=0xFFFFFFFF、max=0，Cull 侧靠 min > max 识别。
// TILE_SIZE 必须与 ForwardPlusParams / 其余 Forward+ shader 对齐。

#define TILE_SIZE 16

layout(local_size_x = TILE_SIZE, local_size_y = TILE_SIZE) in;

#ifdef VULKAN
#define LAYOUT_BIND(s, b) layout(set = s, binding = b)
#else
#define LAYOUT_BIND(s, b) layout(binding = b)
#endif

LAYOUT_BIND(0, 0) uniform sampler2D u_DepthVS;

LAYOUT_BIND(0, 1) layout(std430) buffer u_TileDepthRange
{
	uint u_TileDepth[]; // [tile*2] = minDist bits, [tile*2+1] = maxDist bits
};

LAYOUT_BIND(0, 2) layout(std140) uniform u_CullParams
{
	mat4  u_InvProj;
	ivec4 u_ScreenAndTiles; // x=width, y=height, z=tilesX, w=tilesY
	vec4  u_ClusterZ;       // x=near, y=far, z=zSlices, w=log(far/near)
};

shared uint s_Min;
shared uint s_Max;

void main()
{
	if (gl_LocalInvocationIndex == 0u)
	{
		s_Min = 0xFFFFFFFFu;
		s_Max = 0u;
	}
	barrier();

	const ivec2 px = ivec2(gl_GlobalInvocationID.xy);
	if (px.x < u_ScreenAndTiles.x && px.y < u_ScreenAndTiles.y)
	{
		// 预通道 Clear=0，有几何的地方 viewZ < 0。
		const float viewZ = texelFetch(u_DepthVS, px, 0).r;
		if (viewZ < 0.0)
		{
			const uint bits = floatBitsToUint(max(-viewZ, u_ClusterZ.x));
			atomicMin(s_Min, bits);
			atomicMax(s_Max, bits);
		}
	}
	barrier();

	if (gl_LocalInvocationIndex == 0u)
	{
		const uint tile = gl_WorkGroupID.y * uint(u_ScreenAndTiles.z) + gl_WorkGroupID.x;
		u_TileDepth[tile * 2u]      = s_Min;
		u_TileDepth[tile * 2u + 1u] = s_Max;
	}
}
