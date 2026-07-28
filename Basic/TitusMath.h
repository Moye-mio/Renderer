// =============================================================================
// Basic/TitusMath.h
// 引擎统一数学类型与运算门面（方案 B）。
//
// 公共头不依赖 glm；外部 / 门面 / 资产 IR 只使用本 API。
// 部分变换函数（lookAt / perspective / ortho / inverse）在 .cpp 内由 glm 实现，
// 并保持 GLM_FORCE_DEPTH_ZERO_TO_ONE（Z∈[0,1]）语义。
//
// 布局约定：
//   - Vec* 为紧密打包 float 分量（与常见 GPU 交错顶点一致）
//   - Mat4 为列主序 float[16]，m[col * 4 + row]
//   - 与当前工程所用 glm::vec*/mat4 二进制兼容（由 TitusMath.cpp static_assert 校验）
// =============================================================================
#pragma once

#include <cmath>
#include <cstddef>

namespace TitusMath
{
    // ------------------------------------------------------------------------
    // 类型
    // ------------------------------------------------------------------------
    struct Vec2
    {
        float x = 0.0f;
        float y = 0.0f;

        constexpr Vec2() noexcept = default;
        explicit constexpr Vec2(float s) noexcept : x(s), y(s) {}
        constexpr Vec2(float x_, float y_) noexcept : x(x_), y(y_) {}
    };

    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        constexpr Vec3() noexcept = default;
        explicit constexpr Vec3(float s) noexcept : x(s), y(s), z(s) {}
        constexpr Vec3(float x_, float y_, float z_) noexcept : x(x_), y(y_), z(z_) {}

        // 从 Vec4 取 xyz（对应 glm::vec3(vec4)）
        explicit constexpr Vec3(const struct Vec4& v) noexcept;
    };

    struct Vec4
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 0.0f;

        constexpr Vec4() noexcept = default;
        explicit constexpr Vec4(float s) noexcept : x(s), y(s), z(s), w(s) {}
        constexpr Vec4(float x_, float y_, float z_, float w_) noexcept
            : x(x_), y(y_), z(z_), w(w_)
        {
        }
        constexpr Vec4(const Vec3& v, float w_) noexcept : x(v.x), y(v.y), z(v.z), w(w_) {}
    };

    constexpr Vec3::Vec3(const Vec4& v) noexcept : x(v.x), y(v.y), z(v.z) {}

    struct IVec4
    {
        int x = 0;
        int y = 0;
        int z = 0;
        int w = 0;

        constexpr IVec4() noexcept = default;
        explicit constexpr IVec4(int s) noexcept : x(s), y(s), z(s), w(s) {}
        constexpr IVec4(int x_, int y_, int z_, int w_) noexcept
            : x(x_), y(y_), z(z_), w(w_)
        {
        }
    };

    // 列主序 4x4；默认单位矩阵；Mat4(s) 为对角缩放（对齐 glm::mat4(s)）
    struct Mat4
    {
        float m[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
        };

        constexpr Mat4() noexcept = default;

        explicit constexpr Mat4(float s) noexcept
            : m{s, 0, 0, 0, 0, s, 0, 0, 0, 0, s, 0, 0, 0, 0, s}
        {
        }

        // mat[col][row] 访问（与 glm::mat4 一致）
        float*       operator[](int col) noexcept { return m + col * 4; }
        const float* operator[](int col) const noexcept { return m + col * 4; }
    };

    // ------------------------------------------------------------------------
    // 向量运算符
    // ------------------------------------------------------------------------
    inline constexpr Vec2 operator+(const Vec2& a, const Vec2& b) noexcept
    {
        return {a.x + b.x, a.y + b.y};
    }
    inline constexpr Vec2 operator-(const Vec2& a, const Vec2& b) noexcept
    {
        return {a.x - b.x, a.y - b.y};
    }
    inline constexpr Vec2 operator*(const Vec2& a, float s) noexcept
    {
        return {a.x * s, a.y * s};
    }
    inline constexpr Vec2 operator*(float s, const Vec2& a) noexcept { return a * s; }

    inline constexpr Vec3 operator+(const Vec3& a, const Vec3& b) noexcept
    {
        return {a.x + b.x, a.y + b.y, a.z + b.z};
    }
    inline constexpr Vec3 operator-(const Vec3& a, const Vec3& b) noexcept
    {
        return {a.x - b.x, a.y - b.y, a.z - b.z};
    }
    inline constexpr Vec3 operator-(const Vec3& a) noexcept { return {-a.x, -a.y, -a.z}; }
    inline constexpr Vec3 operator*(const Vec3& a, float s) noexcept
    {
        return {a.x * s, a.y * s, a.z * s};
    }
    inline constexpr Vec3 operator*(float s, const Vec3& a) noexcept { return a * s; }
    inline constexpr Vec3& operator+=(Vec3& a, const Vec3& b) noexcept
    {
        a = a + b;
        return a;
    }
    inline constexpr Vec3& operator-=(Vec3& a, const Vec3& b) noexcept
    {
        a = a - b;
        return a;
    }

    inline constexpr Vec4 operator+(const Vec4& a, const Vec4& b) noexcept
    {
        return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
    }
    inline constexpr Vec4 operator-(const Vec4& a, const Vec4& b) noexcept
    {
        return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w};
    }
    inline constexpr Vec4 operator*(const Vec4& a, float s) noexcept
    {
        return {a.x * s, a.y * s, a.z * s, a.w * s};
    }
    inline constexpr Vec4 operator*(float s, const Vec4& a) noexcept { return a * s; }

    // ------------------------------------------------------------------------
    // 基础运算（头文件内联，无第三方依赖）
    // ------------------------------------------------------------------------
    inline constexpr float radians(float deg) noexcept
    {
        return deg * 0.017453292519943295769f; // pi/180
    }

    inline constexpr float degrees(float rad) noexcept
    {
        return rad * 57.295779513082320877f; // 180/pi
    }

    inline constexpr float dot(const Vec3& a, const Vec3& b) noexcept
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    inline constexpr Vec3 cross(const Vec3& a, const Vec3& b) noexcept
    {
        return {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x,
        };
    }

    inline float length(const Vec3& v) noexcept
    {
        return std::sqrt(dot(v, v));
    }

    inline Vec3 normalize(const Vec3& v) noexcept
    {
        const float len = length(v);
        if (len <= 0.0f) return Vec3{0.0f};
        return v * (1.0f / len);
    }

    inline constexpr Vec3 min(const Vec3& a, const Vec3& b) noexcept
    {
        return {
            a.x < b.x ? a.x : b.x,
            a.y < b.y ? a.y : b.y,
            a.z < b.z ? a.z : b.z,
        };
    }

    inline constexpr Vec3 max(const Vec3& a, const Vec3& b) noexcept
    {
        return {
            a.x > b.x ? a.x : b.x,
            a.y > b.y ? a.y : b.y,
            a.z > b.z ? a.z : b.z,
        };
    }

    inline constexpr const float* value_ptr(const Mat4& mat) noexcept { return mat.m; }
    inline float*                 value_ptr(Mat4& mat) noexcept { return mat.m; }
    inline constexpr const float* value_ptr(const Vec4& v) noexcept { return &v.x; }
    inline constexpr const float* value_ptr(const Vec3& v) noexcept { return &v.x; }

    inline Mat4 operator*(const Mat4& a, const Mat4& b) noexcept
    {
        Mat4 r(0.0f);
        for (int col = 0; col < 4; ++col)
        {
            for (int row = 0; row < 4; ++row)
            {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k)
                    sum += a[k][row] * b[col][k];
                r[col][row] = sum;
            }
        }
        return r;
    }

    inline Vec4 operator*(const Mat4& a, const Vec4& v) noexcept
    {
        return {
            a[0][0] * v.x + a[1][0] * v.y + a[2][0] * v.z + a[3][0] * v.w,
            a[0][1] * v.x + a[1][1] * v.y + a[2][1] * v.z + a[3][1] * v.w,
            a[0][2] * v.x + a[1][2] * v.y + a[2][2] * v.z + a[3][2] * v.w,
            a[0][3] * v.x + a[1][3] * v.y + a[2][3] * v.z + a[3][3] * v.w,
        };
    }

    inline Mat4 transpose(const Mat4& a) noexcept
    {
        Mat4 r(0.0f);
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row)
                r[col][row] = a[row][col];
        return r;
    }

    // ------------------------------------------------------------------------
    // 变换（.cpp 内 glm 实现；需链接 Basic）
    // ------------------------------------------------------------------------
    Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up);
    Mat4 perspective(float fovyRadians, float aspect, float nearPlane, float farPlane);
    Mat4 ortho(float left, float right, float bottom, float top, float nearPlane, float farPlane);
    Mat4 inverse(const Mat4& m);
} // namespace TitusMath
