// =============================================================================
// Basic/TitusMath.cpp
// TitusMath 变换函数的 glm 后端实现；公共头不暴露 glm。
// =============================================================================
#include "TitusMath.h"

#include <cstring>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace TitusMath
{
    namespace
    {
        glm::vec3 ToGlm(const Vec3& v) { return {v.x, v.y, v.z}; }

        Mat4 FromGlm(const glm::mat4& m)
        {
            Mat4 out(0.0f);
            std::memcpy(out.m, &m[0][0], sizeof(out.m));
            return out;
        }

        glm::mat4 ToGlm(const Mat4& m)
        {
            glm::mat4 out(0.0f);
            std::memcpy(&out[0][0], m.m, sizeof(m.m));
            return out;
        }

        // 布局契约：与当前 GraphicSDK glm 保持二进制兼容，供顶点/UBO 平滑迁移。
        static_assert(sizeof(Vec2) == sizeof(glm::vec2), "TitusMath::Vec2 size must match glm::vec2");
        static_assert(sizeof(Vec3) == sizeof(glm::vec3), "TitusMath::Vec3 size must match glm::vec3");
        static_assert(sizeof(Vec4) == sizeof(glm::vec4), "TitusMath::Vec4 size must match glm::vec4");
        static_assert(sizeof(IVec4) == sizeof(glm::ivec4), "TitusMath::IVec4 size must match glm::ivec4");
        static_assert(sizeof(Mat4) == sizeof(glm::mat4), "TitusMath::Mat4 size must match glm::mat4");
        static_assert(alignof(Vec3) == alignof(glm::vec3), "TitusMath::Vec3 align must match glm::vec3");
        static_assert(alignof(Vec4) == alignof(glm::vec4), "TitusMath::Vec4 align must match glm::vec4");
        static_assert(alignof(Mat4) == alignof(glm::mat4), "TitusMath::Mat4 align must match glm::mat4");
    } // namespace

    Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up)
    {
        return FromGlm(glm::lookAt(ToGlm(eye), ToGlm(center), ToGlm(up)));
    }

    Mat4 perspective(float fovyRadians, float aspect, float nearPlane, float farPlane)
    {
        // 依赖工程宏 GLM_FORCE_DEPTH_ZERO_TO_ONE → 裁剪空间 Z∈[0,1]
        return FromGlm(glm::perspective(fovyRadians, aspect, nearPlane, farPlane));
    }

    Mat4 ortho(float left, float right, float bottom, float top, float nearPlane, float farPlane)
    {
        return FromGlm(glm::ortho(left, right, bottom, top, nearPlane, farPlane));
    }

    Mat4 inverse(const Mat4& m)
    {
        return FromGlm(glm::inverse(ToGlm(m)));
    }
} // namespace TitusMath
