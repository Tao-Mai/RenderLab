#include "base/gfx/texture_loader.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <string_view>
#include <vector>

namespace
{
bool try_preset_color(std::string_view name, glm::vec4& out)
{
    if (name == "white")
    {
        out = {1.0f, 1.0f, 1.0f, 1.0f};
        return true;
    }
    if (name == "black")
    {
        out = {0.0f, 0.0f, 0.0f, 1.0f};
        return true;
    }
    if (name == "red")
    {
        out = {1.0f, 0.0f, 0.0f, 1.0f};
        return true;
    }
    if (name == "green")
    {
        out = {0.0f, 1.0f, 0.0f, 1.0f};
        return true;
    }
    if (name == "blue")
    {
        out = {0.0f, 0.0f, 1.0f, 1.0f};
        return true;
    }
    if (name == "gray" || name == "grey")
    {
        out = {0.5f, 0.5f, 0.5f, 1.0f};
        return true;
    }
    return false;
}

bool parse_float(std::string_view s, float& out)
{
    s.remove_prefix(std::min(s.find_first_not_of(" \t"), s.size()));
    s.remove_suffix(s.size() - (s.find_last_not_of(" \t") + 1));
    if (s.empty())
        return false;
    const auto* begin = s.data();
    const auto* end   = begin + s.size();
    const auto  res   = std::from_chars(begin, end, out);
    return res.ec == std::errc{} && res.ptr == end;
}

glm::vec4 parse_color_components(std::string_view csv)
{
    glm::vec4 c{1.0f};
    std::vector<float> parts;
    parts.reserve(4);

    while (!csv.empty())
    {
        const auto comma = csv.find(',');
        const auto token = csv.substr(0, comma);
        float      v     = 0.0f;
        if (!parse_float(token, v))
            LOG(FATAL) << "Invalid builtin texture color component: " << token;
        parts.push_back(v);
        if (comma == std::string_view::npos)
            break;
        csv.remove_prefix(comma + 1);
    }

    if (parts.size() < 3 || parts.size() > 4)
        LOG(FATAL) << "builtin texture color expects 3 or 4 components, got " << parts.size();

    c.r = parts[0];
    c.g = parts[1];
    c.b = parts[2];
    c.a = parts.size() == 4 ? parts[3] : 1.0f;
    return c;
}

glm::vec4 parse_hex_color(std::string_view hex)
{
    if (hex.empty() || hex[0] != '#')
        LOG(FATAL) << "Invalid hex builtin texture: " << hex;

    hex.remove_prefix(1);
    auto hex_digit = [](char ch) -> int {
        if (ch >= '0' && ch <= '9')
            return ch - '0';
        if (ch >= 'a' && ch <= 'f')
            return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F')
            return ch - 'A' + 10;
        return -1;
    };

    auto read_byte = [&](std::size_t i) -> unsigned char {
        if (i + 1 >= hex.size())
            LOG(FATAL) << "Invalid hex builtin texture: #" << hex;
        const int hi = hex_digit(hex[i]);
        const int lo = hex_digit(hex[i + 1]);
        if (hi < 0 || lo < 0)
            LOG(FATAL) << "Invalid hex builtin texture: #" << hex;
        return static_cast<unsigned char>((hi << 4) | lo);
    };

    if (hex.size() == 6)
    {
        return {
            read_byte(0) / 255.0f,
            read_byte(2) / 255.0f,
            read_byte(4) / 255.0f,
            1.0f,
        };
    }
    if (hex.size() == 8)
    {
        return {
            read_byte(0) / 255.0f,
            read_byte(2) / 255.0f,
            read_byte(4) / 255.0f,
            read_byte(6) / 255.0f,
        };
    }
    LOG(FATAL) << "Hex builtin texture must be #rrggbb or #rrggbbaa, got: #" << hex;
}

TextureGPU load_builtin_texture(std::string_view spec)
{
    if (spec.starts_with('#'))
        return TextureGPU::create_solid(parse_hex_color(spec));

    if (spec.starts_with("color/"))
        return TextureGPU::create_solid(parse_color_components(spec.substr(6)));

    glm::vec4 preset{};
    if (try_preset_color(spec, preset))
        return TextureGPU::create_solid(preset);

    LOG(FATAL) << "Unknown builtin texture: builtin:" << spec;
}
}  // namespace

TextureGPU LoadTexture(const std::string& key)
{
    constexpr std::string_view builtin_prefix = "builtin:";
    if (!key.starts_with(builtin_prefix))
        LOG(FATAL) << "Only builtin textures are supported for now: " << key;

    return load_builtin_texture(key.substr(builtin_prefix.size()));
}
