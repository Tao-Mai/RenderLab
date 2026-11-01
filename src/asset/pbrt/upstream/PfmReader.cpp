// Adapted from pbrt-v4 src/pbrt/util/image.cpp at b4ce9687e6c695f5582997c61b0c66cf064bdb4a.
// PFM reader courtesy Jiawen "Kevin" Chen. SPDX-License-Identifier: Apache-2.0
#include "asset/AssetImporter.h"
#include "core/Logger.h"

#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

AssetImporter::LoadedImage readPbrtPfm(const std::filesystem::path& path)
{
    const auto filename = path.string();
    std::unique_ptr<FILE, decltype(&std::fclose)> file(std::fopen(filename.c_str(), "rb"), &std::fclose);
    CHECK(file, "cannot open PFM image: {}", filename);
    const auto word = [&]
    {
        std::string text;
        int character;
        do { character = std::fgetc(file.get()); }
        while (character == ' ' || character == '\t' || character == '\r' || character == '\n');
        CHECK(character != EOF, "incomplete PFM header: {}", filename);
        do
        {
            text.push_back(static_cast<char>(character));
            character = std::fgetc(file.get());
        } while (character != EOF && character != ' ' && character != '\t' && character != '\r' && character != '\n');
        if (character == '\r') CHECK(std::fgetc(file.get()) == '\n', "invalid PFM line ending: {}", filename);
        return text;
    };
    const auto magic = word();
    CHECK(magic == "PF" || magic == "Pf", "invalid PFM magic: {}", filename);
    const auto parse = [&]<class T>()
    {
        const auto text = word();
        T value;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        CHECK(error == std::errc{} && end == text.data() + text.size(), "invalid PFM header: {}", filename);
        return value;
    };
    const int width = parse.template operator()<int>();
    const int height = parse.template operator()<int>();
    const float scale = parse.template operator()<float>();
    CHECK(width > 0 && height > 0 && std::isfinite(scale) && scale != 0, "invalid PFM dimensions or scale: {}", filename);
    const size_t channels = magic == "PF" ? 3 : 1;
    std::vector<float> source(size_t(width) * height * channels);
    for (int row = height - 1; row >= 0; --row)
        CHECK(std::fread(source.data() + size_t(row) * width * channels, sizeof(float), width * channels, file.get()) == width * channels,
              "incomplete PFM pixels: {}", filename);
    const bool swap = (std::endian::native == std::endian::little) != (scale < 0);
    for (auto& value : source)
    {
        if (swap) value = std::bit_cast<float>(std::byteswap(std::bit_cast<uint32_t>(value)));
        value *= std::abs(scale);
    }
    std::vector<float> pixels(size_t(width) * height * 4, 1.0f);
    for (size_t pixel = 0; pixel < size_t(width) * height; ++pixel)
        for (size_t channel = 0; channel < 3; ++channel)
            pixels[pixel * 4 + channel] = source[pixel * channels + (channels == 1 ? 0 : channel)];
    return {.format = ImageFormat::RGBA32F, .fileFormat = AssetImporter::LoadedImage::FileFormat::Pfm,
        .width = static_cast<uint32_t>(width), .height = static_cast<uint32_t>(height), .pixels = std::move(pixels)};
}
