// =============================================================================
// Basic/LogMath.h
// 可选的 TitusMath <-> Logger 流式适配。
// 仅在需要把 Vec*/Mat4 直接 << 进 LOG_STREAM_* 的源文件中包含本头。
// =============================================================================
#pragma once

#include "Logger.h"
#include "TitusMath.h"

namespace TitusBasic
{
    inline LogStreamHelper& operator<<(LogStreamHelper& s, const TitusMath::Mat4& mat)
    {
        auto& os = s.Stream();
        os << "[\n";
        for (int row = 0; row < 4; ++row)
        {
            os << "  [ ";
            for (int col = 0; col < 4; ++col)
            {
                os << mat[col][row];
                if (col < 3) os << ", ";
            }
            os << " ]";
            if (row < 3) os << ",\n";
        }
        os << "\n]";
        return s;
    }

    inline LogStreamHelper& operator<<(LogStreamHelper& s, const TitusMath::Vec2& v)
    {
        s.Stream() << "(" << v.x << ", " << v.y << ")";
        return s;
    }
    inline LogStreamHelper& operator<<(LogStreamHelper& s, const TitusMath::Vec3& v)
    {
        s.Stream() << "(" << v.x << ", " << v.y << ", " << v.z << ")";
        return s;
    }
    inline LogStreamHelper& operator<<(LogStreamHelper& s, const TitusMath::Vec4& v)
    {
        s.Stream() << "(" << v.x << ", " << v.y << ", " << v.z << ", " << v.w << ")";
        return s;
    }
} // namespace TitusBasic
