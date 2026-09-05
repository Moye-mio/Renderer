#pragma once
// ============================================================================
// 005_Software_Path_Tracing - CornellBoxScene
//
// 空的 Cornell Box（六面闭合，相机在盒内）+ 两个互不相交的球。盒壁与两球
// 共用同一份白色漫反射材质，唯一光源是贴着天花板朝下的矩形面光源。
//
// 全部是解析几何：不建加速结构、不上传任何 mesh，求交在 RayTrace_FS.glsl
// 里做，所以 GL / VK 两个后端跑的是同一份代码。
// ============================================================================
#include "Basic/TitusMath.h"

class CornellBoxScene
{
public:
    // std140 布局，与 RayTrace_FS.glsl 的 u_Scene 块逐字段对应。
    struct SceneBlock
    {
        TitusMath::Vec4 boxMin;        // xyz: 盒内壁最小角
        TitusMath::Vec4 boxMax;        // xyz: 盒内壁最大角
        TitusMath::Vec4 sphere0;       // xyz: 球心, w: 半径
        TitusMath::Vec4 sphere1;       // xyz: 球心, w: 半径
        TitusMath::Vec4 lightCenter;   // xyz: 面光源中心（法线固定朝 -Y）
        TitusMath::Vec4 lightHalfSize; // xy: x / z 方向的半边长
        TitusMath::Vec4 lightEmission; // rgb: 面光源辐射亮度
        TitusMath::Vec4 albedo;        // rgb: 盒壁与两球共用的白色漫反射率
    };

    SceneBlock Pack() const;

    // 面光源中心：贴在天花板下方 lightGap 处，法线朝下。
    TitusMath::Vec3 LightCenter() const;

    // 相机初始机位：盒外、正对开口往里看，和 Cornell Box 原始机位一致。
    TitusMath::Vec3 DefaultCameraPosition() const;

    // overlay 拖动球心 / 半径后用来兜底提示：两球是否还互不相交、是否还在盒内。
    bool SpheresDisjoint() const;
    bool SpheresInsideBox() const;

    // ---- 盒子（4×4×4 内壁；y = boxMin.y 是地板，y = boxMax.y 是天花板）----
    // 只有五面墙，+Z 那面是开口，相机从那边看进来。
    TitusMath::Vec3 boxMin{-2.0f, 0.0f, -2.0f};
    TitusMath::Vec3 boxMax{ 2.0f, 4.0f,  2.0f};

    // ---- 两个白球：都落在地板上（球心高度 = 半径），彼此留出约 0.9 的间隙 ----
    TitusMath::Vec3 sphere0Center{-0.90f, 0.80f, -0.60f};
    float           sphere0Radius = 0.80f;
    TitusMath::Vec3 sphere1Center{ 0.95f, 0.60f,  0.80f};
    float           sphere1Radius = 0.60f;

    // ---- 天花板面光源 ----
    TitusMath::Vec2 lightCenterXZ{0.0f, 0.0f};
    TitusMath::Vec2 lightHalfSize{0.75f, 0.75f};
    float           lightGap      = 0.02f; // 离天花板的距离，避免和天花板共面
    float           lightEmission = 4.0f; // 白光，rgb 同值

    // Cornell Box 原始数据里白漆的反射率约 0.73；盒壁与两球都用它。
    // 拉到 1.0 就是理想白盒，能量守恒下多次弹射几乎不衰减，可用来检查
    // 路径追踪是否漏乘/多乘了 throughput。
    float albedo = 0.73f;
};
