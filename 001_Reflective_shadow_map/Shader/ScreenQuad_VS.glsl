#version 430 core

// 任务 8：使用 gl_VertexID（GL）/ gl_VertexIndex（VK）生成全屏三角形。
// glslang 编译 Vulkan 时 gl_VertexID 自动映射到 gl_VertexIndex，无需额外宏切换。

// 大三角形覆盖全屏（适配 cmd.Draw(3)，无需 VBO 输入）。
//   id=0 → (-1, -1)，UV (0, 0)
//   id=1 → ( 3, -1)，UV (2, 0)
//   id=2 → (-1,  3)，UV (0, 2)
// 三角形覆盖 NDC [-1,1] x [-1,1] 区域，UV 在 [0,1] 范围内可正常采样。
// 任务 10：Vulkan GLSL 用 gl_VertexIndex，OpenGL 用 gl_VertexID。
#ifdef VULKAN
#define VERTEX_ID gl_VertexIndex
#else
#define VERTEX_ID gl_VertexID
#endif

// 任务 10：v2f 输出必须显式 layout(location=N)
layout(location = 0) out vec2 v2f_TexCoords;

void main()
{
	vec2 pos;
	vec2 uv;
	if (VERTEX_ID == 0)      { pos = vec2(-1.0, -1.0); uv = vec2(0.0, 0.0); }
	else if (VERTEX_ID == 1) { pos = vec2( 3.0, -1.0); uv = vec2(2.0, 0.0); }
	else                       { pos = vec2(-1.0,  3.0); uv = vec2(0.0, 2.0); }

	gl_Position   = vec4(pos, 0.0, 1.0);
	v2f_TexCoords = uv;
}