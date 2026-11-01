// Derived from pbrt-v4 src/pbrt/util/mesh.cpp at b4ce9687e6c695f5582997c61b0c66cf064bdb4a.
// Copyright (c) 1998-2020 Matt Pharr, Wenzel Jakob, and Greg Humphreys.
// SPDX-License-Identifier: Apache-2.0
#include "asset/pbrt/PbrtPly.h"
#include "core/Logger.h"
#include "rply/rply.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

namespace pbrt_import
{
namespace
{
using Float = double;
static_assert(sizeof(glm::dvec3) == 3 * sizeof(Float));
static_assert(sizeof(glm::dvec2) == 2 * sizeof(Float));
const char* formatArgument(const std::string& value) { return value.c_str(); }
template <class T> const T& formatArgument(const T& value) { return value; }
template <class... Args>
std::string formatMessage(const char* format, const Args&... args)
{
    const int length = std::snprintf(nullptr, 0, format, formatArgument(args)...);
    CHECK(length >= 0);
    std::string message(static_cast<size_t>(length) + 1, '\0');
    std::snprintf(message.data(), message.size(), format, formatArgument(args)...);
    message.resize(static_cast<size_t>(length));
    return message;
}
template <class... Args>
[[noreturn]] void ErrorExit(const char* format, const Args&... args)
{
    LOG_FATAL("{}", formatMessage(format, args...));
}
template <class... Args>
void Warning(const char* format, const Args&... args)
{
    LOG_WARNING("{}", formatMessage(format, args...));
}
}  // namespace
#define CHECK_EQ(left, right) CHECK((left) == (right))

struct FaceCallbackContext {
    int face[4];
    std::vector<int> triIndices, quadIndices;
    std::vector<int> faceIndices;
    std::vector<long> triFaces, quadFaces;
    std::string filename;
};

void rply_message_callback(p_ply ply, const char *message) {
    Warning("rply: %s", message);
}

/* Callback to handle vertex data from RPly */
int rply_vertex_callback(p_ply_argument argument) {
    Float *buffer;
    long index, flags;

    ply_get_argument_user_data(argument, (void **)&buffer, &flags);
    ply_get_argument_element(argument, nullptr, &index);

    int stride = (flags & 0x0F0) >> 4;
    int offset = flags & 0x00F;

    buffer[index * stride + offset] = ply_get_argument_value(argument);

    return 1;
}

/* Callback to handle face data from RPly */
int rply_face_callback(p_ply_argument argument) {
    FaceCallbackContext *context;
    long flags;
    ply_get_argument_user_data(argument, (void **)&context, &flags);

    long length, value_index;
    ply_get_argument_property(argument, nullptr, &length, &value_index);

    CHECK(length == 3 || length == 4, "unsupported PLY face with {} vertices: {}", length, context->filename);
    if (value_index < 0) {
        return 1;
    }

    if (value_index >= 0) {
        const double value = ply_get_argument_value(argument);
        CHECK(std::isfinite(value) && value >= 0 && value <= std::numeric_limits<int>::max() &&
              value == std::floor(value), "invalid PLY vertex index {}: {}", value, context->filename);
        context->face[value_index] = static_cast<int>(value);
    }

    if (value_index == length - 1) {
        long faceIndex;
        ply_get_argument_element(argument, nullptr, &faceIndex);
        if (length == 3)
        {
            context->triFaces.push_back(faceIndex);
            for (int i = 0; i < 3; ++i)
                context->triIndices.push_back(context->face[i]);
        }
        else {
            context->quadFaces.push_back(faceIndex);
            CHECK_EQ(length, 4);

            // Note: modify order since we're specifying it as a blp...
            context->quadIndices.push_back(context->face[0]);
            context->quadIndices.push_back(context->face[1]);
            context->quadIndices.push_back(context->face[3]);
            context->quadIndices.push_back(context->face[2]);
        }
    }

    return 1;
}

int rply_faceindex_callback(p_ply_argument argument) {
    FaceCallbackContext *context;
    long flags;
    ply_get_argument_user_data(argument, (void **)&context, &flags);

    const double value = ply_get_argument_value(argument);
    CHECK(std::isfinite(value) && value >= std::numeric_limits<int>::min() &&
          value <= std::numeric_limits<int>::max() && value == std::floor(value),
          "invalid PLY face index {}: {}", value, context->filename);
    context->faceIndices.push_back(static_cast<int>(value));

    return 1;
}

PlyMesh PlyMesh::read(const std::string &filename) {
    PlyMesh mesh;

    p_ply ply = ply_open(filename.c_str(), rply_message_callback, 0, nullptr);
    if (!ply)
        ErrorExit("Couldn't open PLY file \"%s\"", filename);

    if (ply_read_header(ply) == 0)
        ErrorExit("Unable to read the header of PLY file \"%s\"", filename);

    p_ply_element element = nullptr;
    size_t vertexCount = 0, faceCount = 0;

    /* Inspect the structure of the PLY file */
    while ((element = ply_get_next_element(ply, element)) != nullptr) {
        const char *name;
        long nInstances;

        ply_get_element_info(element, &name, &nInstances);
        CHECK(nInstances >= 0, "invalid PLY element count: {}", filename);
        if (strcmp(name, "vertex") == 0)
            vertexCount = nInstances;
        else if (strcmp(name, "face") == 0)
            faceCount = nInstances;
    }

    if (vertexCount == 0 || faceCount == 0)
        ErrorExit("%s: PLY file is invalid! No face/vertex elements found!", filename);

    mesh.p.resize(vertexCount);
    if (ply_set_read_cb(ply, "vertex", "x", rply_vertex_callback, mesh.p.data(), 0x30) ==
            0 ||
        ply_set_read_cb(ply, "vertex", "y", rply_vertex_callback, mesh.p.data(), 0x31) ==
            0 ||
        ply_set_read_cb(ply, "vertex", "z", rply_vertex_callback, mesh.p.data(), 0x32) ==
            0) {
        ErrorExit("%s: Vertex coordinate property not found!", filename);
    }

    mesh.n.resize(vertexCount);
    if (ply_set_read_cb(ply, "vertex", "nx", rply_vertex_callback, mesh.n.data(), 0x30) ==
            0 ||
        ply_set_read_cb(ply, "vertex", "ny", rply_vertex_callback, mesh.n.data(), 0x31) ==
            0 ||
        ply_set_read_cb(ply, "vertex", "nz", rply_vertex_callback, mesh.n.data(), 0x32) ==
            0)
        mesh.n.resize(0);

    /* There seem to be lots of different conventions regarding UV coordinate
     * names */
    mesh.uv.resize(vertexCount);
    if (((ply_set_read_cb(ply, "vertex", "u", rply_vertex_callback, mesh.uv.data(),
                          0x20) != 0) &&
         (ply_set_read_cb(ply, "vertex", "v", rply_vertex_callback, mesh.uv.data(),
                          0x21) != 0)) ||
        ((ply_set_read_cb(ply, "vertex", "s", rply_vertex_callback, mesh.uv.data(),
                          0x20) != 0) &&
         (ply_set_read_cb(ply, "vertex", "t", rply_vertex_callback, mesh.uv.data(),
                          0x21) != 0)) ||
        ((ply_set_read_cb(ply, "vertex", "texture_u", rply_vertex_callback,
                          mesh.uv.data(), 0x20) != 0) &&
         (ply_set_read_cb(ply, "vertex", "texture_v", rply_vertex_callback,
                          mesh.uv.data(), 0x21) != 0)) ||
        ((ply_set_read_cb(ply, "vertex", "texture_s", rply_vertex_callback,
                          mesh.uv.data(), 0x20) != 0) &&
         (ply_set_read_cb(ply, "vertex", "texture_t", rply_vertex_callback,
                          mesh.uv.data(), 0x21) != 0)))
        ;
    else
        mesh.uv.resize(0);

    FaceCallbackContext context;
    context.filename = filename;
    context.triIndices.reserve(faceCount * 3);
    context.quadIndices.reserve(faceCount * 4);
    if (ply_set_read_cb(ply, "face", "vertex_indices", rply_face_callback, &context, 0) ==
        0)
        ErrorExit("%s: vertex indices not found in PLY file", filename);

    if (ply_set_read_cb(ply, "face", "face_indices", rply_faceindex_callback,
                        &context, 0) != 0)
        context.faceIndices.reserve(faceCount);

    if (ply_read(ply) == 0)
        ErrorExit("%s: unable to read the contents of PLY file", filename);

    mesh.triIndices = std::move(context.triIndices);
    mesh.quadIndices = std::move(context.quadIndices);
    CHECK(context.faceIndices.empty() || context.faceIndices.size() == faceCount,
          "invalid PLY face-index count: {}", filename);
    for (const auto face : context.triFaces)
        mesh.faceIndices.push_back(context.faceIndices.empty() ? static_cast<int>(face) : context.faceIndices[face]);
    for (const auto face : context.quadFaces)
        mesh.quadFaceIndices.push_back(context.faceIndices.empty() ? static_cast<int>(face) : context.faceIndices[face]);

    ply_close(ply);

    for (int idx : mesh.triIndices)
        if (idx < 0 || idx >= mesh.p.size())
            ErrorExit("plymesh: Vertex index %i is out of bounds! "
                      "Valid range is [0..%i)",
                      idx, int(mesh.p.size()));
    for (int idx : mesh.quadIndices)
        if (idx < 0 || idx >= mesh.p.size())
            ErrorExit("plymesh: Vertex index %i is out of bounds! "
                      "Valid range is [0..%i)",
                      idx, int(mesh.p.size()));

    return mesh;
}

void PlyMesh::triangulate() {
    if (quadIndices.empty())
        return;

    triIndices.reserve(triIndices.size() + 3 * quadIndices.size() / 2);

    for (size_t i = 0; i < quadIndices.size(); i += 4) {
        faceIndices.push_back(quadFaceIndices[i / 4]);
        faceIndices.push_back(quadFaceIndices[i / 4]);
        triIndices.push_back(quadIndices[i]);  // 0, 1, 2 of original
        triIndices.push_back(quadIndices[i + 1]);
        triIndices.push_back(quadIndices[i + 3]);

        triIndices.push_back(quadIndices[i]);  // 0, 2, 3 of original
        triIndices.push_back(quadIndices[i + 3]);
        triIndices.push_back(quadIndices[i + 2]);
    }

    quadIndices.clear();
    quadFaceIndices.clear();
}


#undef CHECK_EQ
}  // namespace pbrt_import
