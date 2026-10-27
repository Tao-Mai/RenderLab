// pbrt is Copyright(c) 1998-2020 Matt Pharr, Wenzel Jakob, and Greg Humphreys.
// SPDX-License-Identifier: Apache-2.0
// Host bindings include transform interpolation derived from util/transform.cpp; see UPSTREAM.md.
#include "Scene.h"
#include "utf8proc/utf8proc.h"

#include <cstring>
#include <fstream>
#include <zlib.h>

namespace pbrt
{
thread_local Session*     session = nullptr;
thread_local OptionsData* Options = nullptr;

void Report(bool fatal, const FileLoc* location, const std::string& message)
{
    PbrtSourceLocation source;
    if (location)
        source = {location->filename, location->line, location->column};
    session->diagnostic(fatal, source, message);
    if (fatal)
        std::abort();
}

namespace
{
    const RGBColorSpace kSrgb{"srgb"};
    const RGBColorSpace kDciP3{"dci-p3"};
    const RGBColorSpace kRec2020{"rec2020"};
    const RGBColorSpace kAces{"aces2065-1"};
}  // namespace
const RGBColorSpace* RGBColorSpace::sRGB = &kSrgb;
const RGBColorSpace* RGBColorSpace::GetNamed(const std::string& name)
{
    if (name == "srgb")
        return &kSrgb;
    if (name == "dci-p3")
        return &kDciP3;
    if (name == "rec2020")
        return &kRec2020;
    if (name == "aces2065-1")
        return &kAces;
    return nullptr;
}

Session::~Session()
{
    for (auto* parameter : parameters)
        delete parameter;
}
ParsedParameter::ParsedParameter(FileLoc location) : loc(std::move(location)) { session->parameters.push_back(this); }
ParameterDictionary::ParameterDictionary(ParsedParameterVector input, const RGBColorSpace* space) :
    ParameterDictionary(std::move(input), {}, space)
{
}
ParameterDictionary::ParameterDictionary(
    ParsedParameterVector input, const ParsedParameterVector& inherited, const RGBColorSpace* space
) :
    parameters(std::move(input)), colorSpace(space)
{
    std::reverse(parameters.begin(), parameters.end());
    for (auto* parameter : parameters)
        if (!parameter->colorSpace)
            parameter->colorSpace = space;
    parameters.insert(parameters.end(), inherited.rbegin(), inherited.rend());
}

std::string NormalizeUTF8(const std::string& value)
{
    auto* normalized = utf8proc_NFC(reinterpret_cast<const utf8proc_uint8_t*>(value.c_str()));
    if (!normalized)
        ErrorExit("invalid UTF-8 name");
    std::string result(reinterpret_cast<const char*>(normalized));
    std::free(normalized);
    return result;
}
void SetSearchDirectory(const std::string& filename)
{
    session->searchDirectory = std::filesystem::absolute(filename).parent_path();
}
std::string ResolveFilename(const std::string& filename)
{
    auto path = std::filesystem::path(filename);
    if (path.is_relative())
        path = session->searchDirectory / path;
    return path.lexically_normal().string();
}
bool HasExtension(const std::string& filename, const std::string& extension)
{
    std::string actual = std::filesystem::path(filename).extension().string();
    std::transform(actual.begin(), actual.end(), actual.begin(), [](unsigned char c) { return std::tolower(c); });
    return actual == extension;
}
std::string ReadFileContents(const std::string& filename)
{
    std::filesystem::path path = std::filesystem::absolute(filename).lexically_normal();
    std::ifstream         stream(path, std::ios::binary | std::ios::ate);
    if (!stream)
        ErrorExit("cannot open source file: %s", filename);
    auto size = stream.tellg();
    if (size < 0)
        ErrorExit("cannot determine source size: %s", filename);
    std::string bytes(static_cast<size_t>(size), '\0');
    stream.seekg(0);
    stream.read(bytes.data(), bytes.size());
    if (!stream)
        ErrorExit("cannot read source file: %s", filename);
    if (std::ranges::find(session->sourceFiles, path) == session->sourceFiles.end())
        session->sourceFiles.push_back(path);
    return bytes;
}
std::string ReadDecompressedFileContents(const std::string& filename)
{
    const auto compressed = ReadFileContents(filename);
    z_stream   stream{};
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK)
        ErrorExit("cannot initialize gzip reader");
    std::string output;
    size_t      consumed = 0;
    int         status   = Z_OK;
    do
    {
        if (stream.avail_in == 0 && consumed < compressed.size())
        {
            auto size       = std::min<size_t>(compressed.size() - consumed, std::numeric_limits<uInt>::max());
            stream.next_in  = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.data() + consumed));
            stream.avail_in = static_cast<uInt>(size);
            consumed += size;
        }
        char block[65536];
        stream.next_out  = reinterpret_cast<Bytef*>(block);
        stream.avail_out = sizeof(block);
        status           = inflate(&stream, Z_NO_FLUSH);
        output.append(block, sizeof(block) - stream.avail_out);
        if (status == Z_STREAM_END && (stream.avail_in || consumed < compressed.size()))
        {
            if (inflateReset(&stream) != Z_OK)
                ErrorExit("cannot reset gzip reader: %s", filename);
            status = Z_OK;
        }
        else if (status != Z_OK && status != Z_STREAM_END)
        {
            inflateEnd(&stream);
            ErrorExit("invalid gzip source: %s", filename);
        }
    }
    while (status != Z_STREAM_END);
    inflateEnd(&stream);
    return output;
}

// Polar decomposition and TRS interpolation follow util/transform.cpp at the pinned revision.
Transform AnimatedTransform::Interpolate(Float time) const
{
    if (!IsAnimated() || time <= startTime)
        return startTransform;
    if (time >= endTime)
        return endTransform;
    glm::dmat3      rotations[2], scales[2];
    const Transform transforms[]{startTransform, endTransform};
    for (int endpoint = 0; endpoint < 2; ++endpoint)
    {
        auto   matrix   = glm::dmat3(transforms[endpoint].matrix);
        auto   rotation = matrix;
        double norm;
        int    count = 0;
        do
        {
            auto next = (rotation + glm::inverse(glm::transpose(rotation))) * 0.5;
            norm      = 0;
            for (int row = 0; row < 3; ++row)
                norm = std::max(
                    norm,
                    std::abs(rotation[0][row] - next[0][row]) + std::abs(rotation[1][row] - next[1][row]) +
                        std::abs(rotation[2][row] - next[2][row])
                );
            rotation = next;
        }
        while (++count < 100 && norm > 0.0001);
        rotations[endpoint] = rotation;
        scales[endpoint]    = glm::inverse(rotation) * matrix;
    }
    auto t           = (time - startTime) / (endTime - startTime);
    auto rotation    = glm::slerp(glm::quat_cast(rotations[0]), glm::quat_cast(rotations[1]), t);
    auto translation = glm::mix(glm::dvec3(startTransform.matrix[3]), glm::dvec3(endTransform.matrix[3]), t);
    auto scale       = scales[0] * (1 - t) + scales[1] * t;
    return Transform(glm::translate(glm::dmat4(1), translation) * glm::mat4_cast(rotation) * glm::dmat4(scale));
}
CameraTransform::CameraTransform(const AnimatedTransform& value) : worldFromCamera(value)
{
    auto midpoint = (value.startTime + value.endTime) / 2;
    switch (Options->renderingSpace)
    {
        case RenderingCoordinateSystem::Camera:
            renderFromWorld = Inverse(value.Interpolate(midpoint));
            break;
        case RenderingCoordinateSystem::CameraWorld:
            renderFromWorld = Translate(-value(Point3f(0), midpoint));
            break;
        case RenderingCoordinateSystem::World:
            break;
    }
}
}  // namespace pbrt
