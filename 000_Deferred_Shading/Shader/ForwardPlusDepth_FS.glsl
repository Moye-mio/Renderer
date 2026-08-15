#version 430 core

// Forward+ 深度预通道 FS：把视空间 Z 写入 R32F（Clear=0 表示无几何）。
// 不采样深度附件本身——VK RT 的 depth finalLayout 不能直接给 Compute 采样。
layout(location = 0) in vec3 v2f_FragPosInViewSpace;
layout(location = 0) out float DepthVS_;

void main()
{
	DepthVS_ = v2f_FragPosInViewSpace.z;
}
