#version 430 core

#ifdef VULKAN
#define VERTEX_ID gl_VertexIndex
#else
#define VERTEX_ID gl_VertexID
#endif

layout(location = 0) out vec2 v2f_TexCoords;

void main()
{
	vec2 pos;
	vec2 uv;
	if (VERTEX_ID == 0)      { pos = vec2(-1.0, -1.0); uv = vec2(0.0, 0.0); }
	else if (VERTEX_ID == 1) { pos = vec2( 3.0, -1.0); uv = vec2(2.0, 0.0); }
	else                     { pos = vec2(-1.0,  3.0); uv = vec2(0.0, 2.0); }

	gl_Position   = vec4(pos, 0.0, 1.0);
	v2f_TexCoords = uv;
}
