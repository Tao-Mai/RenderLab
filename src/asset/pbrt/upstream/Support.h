#pragma once

// CPU data-only host bindings for the pinned PBRT parser and scene builder.
#include "asset/pbrt/PbrtFrontend.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pstd
{
using std::optional;
using std::span;
template <class T>
using vector = std::vector<T>;
template <class T>
auto MakeSpan(T* data, size_t size)
{
    return std::span<T>(data, size);
}
}  // namespace pstd

namespace pbrt
{
using Float          = double;
using InternedString = std::string;
using Point3f        = glm::dvec3;
using Vector3f       = glm::dvec3;
struct FileLoc
{
    std::string filename;
    int         line = 1, column = 0;
    FileLoc() = default;
    explicit FileLoc(std::string file) : filename(std::move(file)) {}
    std::string ToString() const { return filename + ":" + std::to_string(line) + ":" + std::to_string(column); }
};

template <class T>
std::string PrintValue(const T& value)
{
    if constexpr (requires { value.ToString(); })
        return value.ToString();
    else if constexpr (requires(std::ostringstream& stream) { stream << value; })
    {
        std::ostringstream stream;
        stream << value;
        return stream.str();
    }
    else if constexpr (requires {
                           value.begin();
                           value.end();
                       })
    {
        std::string result = "[";
        for (const auto& item : value)
            result += PrintValue(item) + " ";
        return result + "]";
    }
    else if constexpr (requires {
                           value.first;
                           value.second;
                       })
    {
        return PrintValue(value.first) + ":" + PrintValue(value.second);
    }
    else if constexpr (requires {
                           value.x;
                           value.y;
                           value.z;
                       })
    {
        return StringPrintf("%s %s %s", value.x, value.y, value.z);
    }
}
template <class... Args>
std::string StringPrintf(const char* format, const Args&... args)
{
    std::vector<std::string> values{PrintValue(args)...};
    std::string              result;
    size_t                   index = 0;
    for (const char* cursor = format; *cursor; ++cursor)
    {
        if (*cursor != '%')
        {
            result += *cursor;
            continue;
        }
        if (cursor[1] == '%')
        {
            result += '%';
            ++cursor;
            continue;
        }
        ++cursor;
        while (*cursor && !std::isalpha(static_cast<unsigned char>(*cursor)))
            ++cursor;
        if (index < values.size())
            result += values[index++];
        if (!*cursor)
            break;
    }
    return result;
}
void Report(bool fatal, const FileLoc* location, const std::string& message);
template <class... Args>
[[noreturn]] void ErrorExit(const FileLoc* location, const char* format, const Args&... args)
{
    Report(true, location, StringPrintf(format, args...));
    std::abort();
}
template <class... Args>
[[noreturn]] void ErrorExit(const char* format, const Args&... args)
{
    ErrorExit(static_cast<const FileLoc*>(nullptr), format, args...);
}
template <class... Args>
void Error(const FileLoc* location, const char* format, const Args&... args)
{
    Report(true, location, StringPrintf(format, args...));
}
template <class... Args>
void Error(const char* format, const Args&... args)
{
    Error(static_cast<const FileLoc*>(nullptr), format, args...);
}
template <class... Args>
void Warning(const FileLoc* location, const char* format, const Args&... args)
{
    Report(false, location, StringPrintf(format, args...));
}
template <class... Args>
void Warning(const char* format, const Args&... args)
{
    Warning(static_cast<const FileLoc*>(nullptr), format, args...);
}

struct RGBColorSpace
{
    std::string                 name;
    static const RGBColorSpace* sRGB;
    static const RGBColorSpace* GetNamed(const std::string& name);
};
class ParsedParameter
{
public:
    explicit ParsedParameter(FileLoc loc);
    void                         AddFloat(Float value);
    void                         AddInt(int value);
    void                         AddString(std::string_view value);
    void                         AddBool(bool value);
    std::string                  ToString() const;
    std::string                  type, name;
    FileLoc                      loc;
    std::vector<Float>           floats;
    std::vector<int>             ints;
    std::vector<std::string>     strings;
    std::vector<uint8_t>         bools;
    mutable const RGBColorSpace* colorSpace  = nullptr;
    bool                         mayBeUnused = false;
};
using ParsedParameterVector = std::vector<ParsedParameter*>;
class ParameterDictionary
{
public:
    ParameterDictionary() = default;
    ParameterDictionary(ParsedParameterVector parameters, const RGBColorSpace* colorSpace);
    ParameterDictionary(
        ParsedParameterVector parameters, const ParsedParameterVector& inherited, const RGBColorSpace* colorSpace
    );
    const ParsedParameterVector& GetParameterVector() const { return parameters; }
    const RGBColorSpace*         ColorSpace() const { return colorSpace; }
    std::string                  ToString() const { return "parameters"; }

private:
    ParsedParameterVector parameters;
    const RGBColorSpace*  colorSpace = nullptr;
};

template <int N>
struct SquareMatrix
{
    glm::dmat4 value{1.0};
    explicit SquareMatrix(std::span<Float> numbers)
    {
        for (int row = 0; row < N; ++row)
            for (int column = 0; column < N; ++column)
                value[column][row] = numbers[row * N + column];
    }
};
class Transform
{
public:
    glm::dmat4 matrix{1.0};
    Transform() = default;
    explicit Transform(glm::dmat4 value) : matrix(value) {}
    explicit Transform(const SquareMatrix<4>& value) : matrix(value.value) {}
    const glm::dmat4& GetMatrix() const { return matrix; }
    Point3f           operator()(Point3f point) const
    {
        auto value = matrix * glm::dvec4(point, 1.0);
        return glm::dvec3(value) / value.w;
    }
    Transform   operator*(const Transform& other) const { return Transform(matrix * other.matrix); }
    bool        operator==(const Transform&) const = default;
    std::string ToString() const { return "transform"; }
};
inline Transform Inverse(const Transform& value) { return Transform(glm::inverse(value.matrix)); }
inline Transform Transpose(const Transform& value) { return Transform(glm::transpose(value.matrix)); }
inline Transform Translate(Vector3f value) { return Transform(glm::translate(glm::dmat4(1.0), value)); }
inline Transform Scale(Float x, Float y, Float z) { return Transform(glm::scale(glm::dmat4(1.0), {x, y, z})); }
inline Transform Rotate(Float degrees, Vector3f axis)
{
    return Transform(glm::rotate(glm::dmat4(1.0), glm::radians(degrees), glm::normalize(axis)));
}
inline Transform LookAt(Point3f eye, Point3f target, Vector3f up) { return Transform(glm::lookAtLH(eye, target, up)); }
struct AnimatedTransform
{
    Transform startTransform, endTransform;
    Float     startTime = 0, endTime = 1;
    AnimatedTransform() = default;
    AnimatedTransform(Transform start, Float t0, Transform end, Float t1) :
        startTransform(start), endTransform(end), startTime(t0), endTime(t1)
    {
    }
    bool        IsAnimated() const { return startTransform != endTransform; }
    Transform   Interpolate(Float time) const;
    Point3f     operator()(Point3f point, Float time) const { return Interpolate(time)(point); }
    std::string ToString() const { return "animated transform"; }
};
enum class RenderingCoordinateSystem
{
    Camera,
    CameraWorld,
    World
};
struct OptionsData
{
    bool        disablePixelJitter = false, disableTextureFiltering = false, disableWavelengthJitter = false;
    Float       displacementEdgeScale = 1;
    std::string mseReferenceImage, mseReferenceOutput;
    RenderingCoordinateSystem renderingSpace = RenderingCoordinateSystem::CameraWorld;
    int                       seed           = 0;
    bool                      forceDiffuse = false, recordPixelStatistics = false, wavefront = false;
};
extern thread_local OptionsData* Options;
struct CameraTransform
{
    AnimatedTransform worldFromCamera;
    Transform         renderFromWorld;
    CameraTransform() = default;
    explicit CameraTransform(const AnimatedTransform& value);
    Transform   RenderFromWorld() const { return renderFromWorld; }
    std::string ToString() const { return "camera transform"; }
};
struct Allocator
{
};
template <class T>
class InternCache
{
public:
    const T* Lookup(const T& value)
        requires(!std::is_same_v<T, std::string>)
    {
        std::array<uint64_t, 16> key;
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row)
                key[column * 4 + row] = std::bit_cast<uint64_t>(value.matrix[column][row]);
        auto [found, inserted] = storage.try_emplace(key);
        if (inserted)
            found->second = std::make_unique<T>(value);
        return found->second.get();
    }
    const T& Lookup(const T& value)
        requires std::is_same_v<T, std::string>
    {
        return value;
    }

private:
    std::map<std::array<uint64_t, 16>, std::unique_ptr<T>> storage;
};

std::string        NormalizeUTF8(const std::string& value);
std::string        ResolveFilename(const std::string& filename);
void               SetSearchDirectory(const std::string& filename);
std::string        ReadFileContents(const std::string& filename);
std::string        ReadDecompressedFileContents(const std::string& filename);
bool               HasExtension(const std::string& filename, const std::string& extension);
inline std::string normalizeArg(const std::string& str)
{
    std::string ret;
    for (unsigned char c : str)
        if (c != '_' && c != '-')
            ret += std::tolower(c);
    return ret;
}
inline bool Atof(const std::string& value, Float* result)
{
    char* end;
    *result = std::strtod(value.c_str(), &end);
    return end == value.c_str() + value.size() && end != value.c_str();
}
struct Session
{
    pbrt_import::Diagnostic            diagnostic;
    std::filesystem::path              searchDirectory;
    std::vector<std::filesystem::path> sourceFiles;
    std::vector<ParsedParameter*>      parameters;
    OptionsData                        options;
    ~Session();
};
extern thread_local Session* session;
}  // namespace pbrt

#define CHECK(condition) ((condition) ? void() : ::pbrt::ErrorExit("Check failed: %s", #condition))
#define CHECK_EQ(a, b) CHECK((a) == (b))
#define CHECK_GE(a, b) CHECK((a) >= (b))
#define CHECK_LT(a, b) CHECK((a) < (b))
#define CHECK_RARE(frequency, condition) ((void)0)
#define STAT_MEMORY_COUNTER(name, variable) static int64_t variable = 0
#define STAT_COUNTER(name, variable) static int64_t variable = 0
#define LOG_VERBOSE(...) ((void)0)
#define LOG_FATAL(...) ::pbrt::ErrorExit(__VA_ARGS__)
