#version 430 core
// 描边色在 VS 里按 Part 查表并按视距淡出后传下来。
// 第二路输出必须声明：与 Cel 共用 MRT。写 0 让描边像素不参与内线检测，
// 外轮廓已经由壳体画过了。
layout(location = 0) in vec3 v2f_OutlineColor;
layout(location = 0) out vec4 Color_;
layout(location = 1) out vec4 CreaseGBuffer_;

void main()
{
	Color_ = vec4(v2f_OutlineColor, 1.0);
	CreaseGBuffer_ = vec4(0.0);
}
