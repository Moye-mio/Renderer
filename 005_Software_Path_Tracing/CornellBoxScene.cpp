// ============================================================================
// 005_Software_Path_Tracing - CornellBoxScene.cpp
// ============================================================================
#include "CornellBoxScene.h"

namespace
{
    // 球心落在盒面上（贴地）时不该判成越界，留一点容差。
    constexpr float kBoundsSlack = 1e-3f;

    bool SphereInsideBox(const TitusMath::Vec3& center, float radius,
                         const TitusMath::Vec3& boxMin, const TitusMath::Vec3& boxMax)
    {
        return center.x - radius >= boxMin.x - kBoundsSlack
            && center.y - radius >= boxMin.y - kBoundsSlack
            && center.z - radius >= boxMin.z - kBoundsSlack
            && center.x + radius <= boxMax.x + kBoundsSlack
            && center.y + radius <= boxMax.y + kBoundsSlack
            && center.z + radius <= boxMax.z + kBoundsSlack;
    }
}

TitusMath::Vec3 CornellBoxScene::LightCenter() const
{
    return TitusMath::Vec3(lightCenterXZ.x, boxMax.y - lightGap, lightCenterXZ.y);
}

CornellBoxScene::SceneBlock CornellBoxScene::Pack() const
{
    SceneBlock block{};
    block.boxMin        = TitusMath::Vec4(boxMin, 0.0f);
    block.boxMax        = TitusMath::Vec4(boxMax, 0.0f);
    block.sphere0       = TitusMath::Vec4(sphere0Center, sphere0Radius);
    block.sphere1       = TitusMath::Vec4(sphere1Center, sphere1Radius);
    block.lightCenter   = TitusMath::Vec4(LightCenter(), 0.0f);
    block.lightHalfSize = TitusMath::Vec4(lightHalfSize.x, lightHalfSize.y, 0.0f, 0.0f);
    block.lightEmission = TitusMath::Vec4(lightEmission, lightEmission, lightEmission, 0.0f);
    block.albedo        = TitusMath::Vec4(albedo, albedo, albedo, 0.0f);
    return block;
}

TitusMath::Vec3 CornellBoxScene::DefaultCameraPosition() const
{
    // 站在开口外 4.5 米处、高度取盒子中线，视线朝 -Z。
    // 配合 40° 的 fovY，视锥在开口处比开口本身窄，所以看不到开口的边框，
    // 到后墙时又比盒子宽，五面墙连同两个球正好铺满画面。
    return TitusMath::Vec3(
        0.5f * (boxMin.x + boxMax.x),
        0.5f * (boxMin.y + boxMax.y),
        boxMax.z + 4.5f);
}

bool CornellBoxScene::SpheresDisjoint() const
{
    const float gap = TitusMath::length(sphere1Center - sphere0Center)
                    - (sphere0Radius + sphere1Radius);
    return gap > 0.0f;
}

bool CornellBoxScene::SpheresInsideBox() const
{
    return SphereInsideBox(sphere0Center, sphere0Radius, boxMin, boxMax)
        && SphereInsideBox(sphere1Center, sphere1Radius, boxMin, boxMax);
}
