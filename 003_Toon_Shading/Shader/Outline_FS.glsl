#version 430 core
// 描边色在 VS 里按 Part 查表并按视距淡出后传下来，这里只负责写出。
layout(location = 0) in vec3 v2f_OutlineColor;
layout(location = 0) out vec4 Color_;

void main()
{
	Color_ = vec4(v2f_OutlineColor, 1.0);
}
