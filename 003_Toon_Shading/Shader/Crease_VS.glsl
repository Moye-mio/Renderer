#version 430 core
// 覆盖全屏的大三角形，配合 cmd.Draw(3)，无需 VBO。
#ifdef VULKAN
#define VERTEX_ID gl_VertexIndex
#else
#define VERTEX_ID gl_VertexID
#endif

void main()
{
	vec2 pos = vec2(-1.0, -1.0);
	if (VERTEX_ID == 1)      pos = vec2( 3.0, -1.0);
	else if (VERTEX_ID == 2) pos = vec2(-1.0,  3.0);
	gl_Position = vec4(pos, 0.0, 1.0);
}
