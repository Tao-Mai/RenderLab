/***************************************************************************
 # Copyright (c) 2015-23, NVIDIA CORPORATION. All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions
 # are met:
 #  * Redistributions of source code must retain the above copyright
 #    notice, this list of conditions and the following disclaimer.
 #  * Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #  * Neither the name of NVIDIA CORPORATION nor the names of its
 #    contributors may be used to endorse or promote products derived
 #    from this software without specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/
#pragma once

#include <cstdint>

namespace Falcor::math
{
template <typename T, int N>
struct vector;

template <typename T>
struct vector<T, 2>
{
    static constexpr int dimension = 2;
    using value_type               = T;

    union
    {
        struct
        {
            T x, y;
        };
        struct
        {
            T r, g;
        };
        struct
        {
            T s, t;
        };
    };

    constexpr vector() noexcept              = default;
    constexpr vector(const vector&) noexcept = default;
    constexpr vector(T x, T y) noexcept : x{x}, y{y} {}
    explicit constexpr vector(T scalar) noexcept : x{scalar}, y{scalar} {}

    template <typename U>
    explicit constexpr vector(U scalar) noexcept : vector(static_cast<T>(scalar))
    {
    }

    template <typename X, typename Y>
    constexpr vector(X x, Y y) noexcept : vector(static_cast<T>(x), static_cast<T>(y))
    {
    }

    template <typename U>
    constexpr vector(const vector<U, 2>& other) noexcept : vector(other.x, other.y)
    {
    }

    [[nodiscard]] constexpr T&       operator[](int index) noexcept { return (&x)[index]; }
    [[nodiscard]] constexpr const T& operator[](int index) const noexcept { return (&x)[index]; }

    [[nodiscard]] static constexpr int length() noexcept { return dimension; }
};

template <typename T>
struct vector<T, 4>
{
    static constexpr int dimension = 4;
    using value_type               = T;

    union
    {
        struct
        {
            T x, y, z, w;
        };
        struct
        {
            T r, g, b, a;
        };
        struct
        {
            T s, t, p, q;
        };
    };

    constexpr vector() noexcept              = default;
    constexpr vector(const vector&) noexcept = default;
    constexpr vector(T x, T y, T z, T w) noexcept : x{x}, y{y}, z{z}, w{w} {}
    explicit constexpr vector(T scalar) noexcept : vector(scalar, scalar, scalar, scalar) {}

    template <typename U>
    explicit constexpr vector(U scalar) noexcept : vector(static_cast<T>(scalar))
    {
    }

    template <typename X, typename Y, typename Z, typename W>
    constexpr vector(X x, Y y, Z z, W w) noexcept :
        vector(static_cast<T>(x), static_cast<T>(y), static_cast<T>(z), static_cast<T>(w))
    {
    }

    template <typename U>
    constexpr vector(const vector<U, 4>& other) noexcept : vector(other.x, other.y, other.z, other.w)
    {
    }

    [[nodiscard]] constexpr T&       operator[](int index) noexcept { return (&x)[index]; }
    [[nodiscard]] constexpr const T& operator[](int index) const noexcept { return (&x)[index]; }

    [[nodiscard]] static constexpr int length() noexcept { return dimension; }
};

using bool2  = vector<bool, 2>;
using uint2  = vector<uint32_t, 2>;
using uint4  = vector<uint32_t, 4>;
using float2 = vector<float, 2>;
using float4 = vector<float, 4>;

template <typename T>
[[nodiscard]] constexpr vector<T, 2> operator*(const vector<T, 2>& lhs, const vector<T, 2>& rhs)
{
    return {lhs.x * rhs.x, lhs.y * rhs.y};
}

template <typename T>
[[nodiscard]] constexpr vector<T, 2> operator/(const vector<T, 2>& lhs, const vector<T, 2>& rhs)
{
    return {lhs.x / rhs.x, lhs.y / rhs.y};
}

template <typename T>
[[nodiscard]] constexpr bool2 operator==(const vector<T, 2>& lhs, const vector<T, 2>& rhs)
{
    return {lhs.x == rhs.x, lhs.y == rhs.y};
}

[[nodiscard]] constexpr bool all(bool2 value) noexcept { return value.x && value.y; }
}  // namespace Falcor::math

namespace Falcor
{
using bool2  = math::bool2;
using uint2  = math::uint2;
using uint4  = math::uint4;
using float2 = math::float2;
using float4 = math::float4;
}  // namespace Falcor
