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

#include <utility>

#if defined(_WIN32)
#define RENDERLAB_CORE_API_EXPORT __declspec(dllexport)
#define RENDERLAB_CORE_API_IMPORT __declspec(dllimport)
#else
#define RENDERLAB_CORE_API_EXPORT __attribute__((visibility("default")))
#define RENDERLAB_CORE_API_IMPORT
#endif

#ifdef RENDERLAB_CORE_EXPORTS
#define RENDERLAB_CORE_API RENDERLAB_CORE_API_EXPORT
#else
#define RENDERLAB_CORE_API RENDERLAB_CORE_API_IMPORT
#endif

#define RL_ENUM_CLASS_OPERATORS(Enum)                                                                                  \
    constexpr Enum operator&(Enum left, Enum right)                                                                    \
    {                                                                                                                  \
        return static_cast<Enum>(std::to_underlying(left) & std::to_underlying(right));                                \
    }                                                                                                                  \
    constexpr Enum operator|(Enum left, Enum right)                                                                    \
    {                                                                                                                  \
        return static_cast<Enum>(std::to_underlying(left) | std::to_underlying(right));                                \
    }                                                                                                                  \
    constexpr Enum& operator|=(Enum& left, Enum right) { return left = left | right; }                                 \
    constexpr Enum& operator&=(Enum& left, Enum right) { return left = left & right; }                                 \
    constexpr Enum  operator~(Enum value) { return static_cast<Enum>(~std::to_underlying(value)); }                    \
    constexpr bool  is_set(Enum value, Enum mask) { return (value & mask) != Enum{}; }                                 \
    constexpr void  flip_bit(Enum& value, Enum mask) { value = is_set(value, mask) ? (value & ~mask) : (value | mask); }
