#include "asset/AssetDataManager.h"
#include "asset/AssetImporter.h"
#include "asset/AssetManager.h"
#include "asset/pbrt/PbrtData.h"
#include "asset/pbrt/PbrtMaterial.h"
#include "core/Context.h"

#include <algorithm>
#include <fstream>
#include <gtest/gtest.h>
#include <iomanip>
#include <map>
#include <sstream>
#include <zlib.h>

namespace
{
const PbrtParameter& parameter(const PbrtEntity& entity, std::string_view name)
{
    const auto found = std::ranges::find(entity.parameters, name, &PbrtParameter::name);
    CHECK(found != entity.parameters.end(), "parameter missing in test: {}", name);
    return *found;
}

class PbrtImportTest : public testing::Test
{
protected:
    std::filesystem::path folder;
    std::filesystem::path source;
    std::filesystem::path assetsRoot;
    AssetManager          assets;
    AssetDataManager      data;
    Context               previous;

    void SetUp() override
    {
        const auto* info = testing::UnitTest::GetInstance()->current_test_info();
        folder           = std::filesystem::path(RENDERLAB_SOURCE_DIR) / "test" / "pbrt-import-tmp" / info->name();
        source           = folder / "source";
        assetsRoot       = folder / "assets";
        removeFolder();
        std::filesystem::create_directories(source);
        previous                   = context();
        context().assetManager     = &assets;
        context().assetDataManager = &data;
        assets.init(assetsRoot);
        data.init(assetsRoot);
    }

    void TearDown() override
    {
        assets.shutdown();
        data.shutdown();
        context() = previous;
        removeFolder();
    }

    void removeFolder()
    {
        const auto expected =
            (std::filesystem::path(RENDERLAB_SOURCE_DIR) / "test" / "pbrt-import-tmp").lexically_normal();
        CHECK(folder.lexically_normal().parent_path() == expected);
        std::filesystem::remove_all(folder);
    }

    void write(const std::string& name, const std::string& contents)
    {
        const auto file = source / name;
        std::filesystem::create_directories(file.parent_path());
        std::ofstream output(file, std::ios::binary);
        output.write(contents.data(), contents.size());
        CHECK(output);
    }

    PbrtSceneAsset::ID import(const std::string& contents, const std::string& id = "scene")
    {
        write("scene.pbrt", contents);
        return AssetImporter::importPbrtScene({.source = source / "scene.pbrt", .id = id});
    }

    void reload()
    {
        assets.shutdown();
        assets.init(assetsRoot);
    }
};

TEST_F(PbrtImportTest, PreservesSceneSemanticsAndNativeRoundTrip)
{
    const auto id       = import(R"pbrt(
Option "rendercoordsys" "world"
MakeNamedMedium "fog" "string type" "homogeneous" "rgb sigma_s" [0.1 0.2 0.3]
MediumInterface "" "fog"
ColorSpace "dci-p3"
TransformTimes 0.25 0.75
Translate 0 0 -8
Camera "perspective" "float fov" 37
Film "rgb" "integer xresolution" 320 "string filename" "output.exr"
Sampler "independent" "integer pixelsamples" 11
Integrator "path" "integer maxdepth" 9
PixelFilter "mitchell" "float xradius" 1.75
Accelerator "bvh" "string splitmethod" "sah"
WorldBegin
MediumInterface "" ""
Texture "strength" "float" "constant" "float value" 0.25
Texture "base" "spectrum" "constant" "blackbody value" 4500
Texture "paint" "spectrum" "scale" "texture tex" "base" "texture scale" "strength"
ColorSpace "rec2020"
MakeNamedMaterial "painted" "string type" "diffuse" "texture reflectance" "paint"
MakeNamedMaterial "metal" "string type" "conductor" "spectrum eta" "metal-Cu-eta" "spectrum k" [400 2 700 3] "bool remaproughness" false
MakeNamedMaterial "blend" "string type" "mix" "string materials" ["painted" "metal"] "texture amount" "strength"
NamedMaterial "blend"
Attribute "shape" "float alpha" 0.8
ObjectBegin "unit"
    ReverseOrientation
    MediumInterface "fog" ""
    AreaLightSource "diffuse" "blackbody L" 6500
    Transform [1 0 0 0 0.25 1 0 0 0 0 1 0 2 3 4 1]
    Shape "sphere" "float radius" 2
ObjectEnd
Translate 4 0 0
ObjectInstance "unit"
ObjectInstance "unit"
AttributeBegin
    ActiveTransform StartTime
    Translate 0 1 0
    ActiveTransform EndTime
    Translate 0 3 0
    ActiveTransform All
    Shape "disk" "float radius" 3
AttributeEnd
MediumInterface "fog" "fog"
LightSource "spot" "rgb I" [1 2 3] "float coneangle" 25
)pbrt");
    const auto original = assets.get(id);
    EXPECT_EQ(original.readerRevision, "b4ce9687e6c695f5582997c61b0c66cf064bdb4a");
    EXPECT_EQ(original.renderingSpace, "world");
    EXPECT_EQ(original.renderFromWorld, glm::dmat4(1));
    EXPECT_DOUBLE_EQ(original.worldFromCamera.start[3].z, 8);
    EXPECT_EQ(original.camera.type, "perspective");
    EXPECT_EQ(original.film.type, "rgb");
    EXPECT_EQ(original.film.colorSpace, "dci-p3");
    EXPECT_EQ(original.sampler.type, "independent");
    EXPECT_EQ(original.integrator.type, "path");
    EXPECT_EQ(original.pixelFilter.type, "mitchell");
    EXPECT_EQ(original.accelerator.type, "bvh");
    ASSERT_TRUE(original.cameraMedium);
    EXPECT_EQ(original.media[*original.cameraMedium].name, "fog");
    ASSERT_EQ(original.lights.size(), 1);
    ASSERT_TRUE(original.lights[0].medium);
    EXPECT_EQ(*original.lights[0].medium, *original.cameraMedium);
    ASSERT_EQ(original.objects.size(), 1);
    ASSERT_EQ(original.instances.size(), 2);
    EXPECT_EQ(original.instances[0].definition, original.instances[1].definition);
    const auto& shape = original.shapes[original.objects[0].shapes[0]];
    EXPECT_TRUE(shape.reverseOrientation);
    ASSERT_TRUE(shape.insideMedium);
    EXPECT_EQ(original.media[*shape.insideMedium].name, "fog");
    ASSERT_TRUE(shape.areaLight);
    EXPECT_EQ(parameter(original.areaLights[*shape.areaLight], "L").type, "blackbody");
    EXPECT_DOUBLE_EQ(shape.renderFromObject.start[1][0], 0.25);
    EXPECT_DOUBLE_EQ(shape.renderFromObject.start[3].x, 2);
    const auto& animated = original.shapes[original.worldShapes[0]];
    EXPECT_DOUBLE_EQ(animated.renderFromObject.start[3].y, 1);
    EXPECT_DOUBLE_EQ(animated.renderFromObject.end[3].y, 3);
    EXPECT_DOUBLE_EQ(animated.renderFromObject.startTime, 0.25);
    EXPECT_DOUBLE_EQ(animated.renderFromObject.endTime, 0.75);
    EXPECT_DOUBLE_EQ(std::get<PbrtFloatValues>(parameter(animated.entity, "alpha").value).values[0], 0.8);
    const auto& blend = assets.get(*shape.material).as<PbrtMaterial>();
    ASSERT_EQ(blend.entity.type, "mix");
    const auto& references = parameter(blend.entity, "materials").references;
    ASSERT_EQ(references.size(), 2);
    const auto& painted = assets.get(std::get<PbrtMaterialReference>(references[0].target).id).as<PbrtMaterial>();
    const auto& metal   = assets.get(std::get<PbrtMaterialReference>(references[1].target).id).as<PbrtMaterial>();
    EXPECT_EQ(
        std::get<PbrtBoolValues>(parameter(metal.entity, "remaproughness").value).values,
        std::vector<uint8_t>{0}
    );
    EXPECT_TRUE(parameter(metal.entity, "eta").references.empty());
    const auto& reflectance = parameter(painted.entity, "reflectance");
    EXPECT_EQ(reflectance.colorSpace, "rec2020");
    const auto textureId = std::get<PbrtTextureReference>(reflectance.references[0].target).id;
    EXPECT_EQ(assets.get(textureId).entity.type, "scale");
    EXPECT_EQ(parameter(assets.get(textureId).entity, "scale").references.size(), 1);
    reload();
    EXPECT_EQ(Serialize(assets.get(id)), Serialize(original));
    EXPECT_EQ(assets.get(textureId).entity.type, "scale");
}

TEST_F(PbrtImportTest, IncludesImportsAndGzipKeepOfficialStateRules)
{
    write("sub/included.pbrt", "Translate 2 0 0\nMaterial \"diffuse\"\n");
    write("imported.pbrt", "Translate 3 0 0\nShape \"sphere\"\n");
    write("inside-object.pbrt", "Shape \"disk\"\n");
    const std::string compressedSource = "Shape \"cylinder\"\n";
    const auto        compressedPath   = source / "geometry.pbrt.gz";
    gzFile            compressed       = gzopen(compressedPath.string().c_str(), "wb");
    ASSERT_NE(compressed, nullptr);
    ASSERT_EQ(gzwrite(compressed, compressedSource.data(), compressedSource.size()), compressedSource.size());
    ASSERT_EQ(gzclose(compressed), Z_OK);
    const auto  id    = import(R"pbrt(
WorldBegin
Include "sub/included.pbrt"
Import "imported.pbrt"
Shape "sphere"
Include "geometry.pbrt.gz"
ObjectBegin "part"
Import "inside-object.pbrt"
ObjectEnd
ObjectInstance "part"
)pbrt");
    const auto& scene = assets.get(id);
    ASSERT_EQ(scene.worldShapes.size(), 3);
    EXPECT_DOUBLE_EQ(scene.shapes[scene.worldShapes[0]].renderFromObject.start[3].x, 5);
    EXPECT_DOUBLE_EQ(scene.shapes[scene.worldShapes[1]].renderFromObject.start[3].x, 2);
    EXPECT_DOUBLE_EQ(scene.shapes[scene.worldShapes[2]].renderFromObject.start[3].x, 2);
    ASSERT_EQ(scene.objects.size(), 1);
    ASSERT_EQ(scene.objects[0].shapes.size(), 1);
    EXPECT_EQ(scene.files.size(), 5);
    EXPECT_EQ(scene.sampler.type, "zsobol");
    EXPECT_EQ(scene.integrator.type, "volpath");
    EXPECT_EQ(scene.pixelFilter.type, "gaussian");
    EXPECT_EQ(scene.accelerator.type, "bvh");
}

TEST_F(PbrtImportTest, PacksEveryInputRoleAndReloadsFromMovedAssetDirectory)
{
    const std::map<std::string, std::string> inputs{
        {"image.exr", std::string("image\0bytes", 11)},
        {"model.ply", "ply bytes"},
        {"spectrum.spd", "400 1\n700 2\n"},
        {"surface.ptx", "ptex bytes"},
        {"measured.bsdf", "brdf bytes"},
        {"lens.dat", "lens bytes"},
        {"density.nvdb", "volume bytes"},
        {"normal.png", "normal bytes"},
        {"reference.exr", "reference bytes"},
        {"aperture.png", "aperture bytes"}
    };
    for (const auto& [name, bytes] : inputs)
        write(name, bytes);
    write("sub/resources.pbrt", R"pbrt(
Texture "image" "spectrum" "imagemap" "string filename" "image.exr"
Texture "surface" "float" "ptex" "string filename" "surface.ptx"
MakeNamedMaterial "measured" "string type" "measured" "string filename" "measured.bsdf" "string normalmap" "normal.png"
MakeNamedMedium "volume" "string type" "nanovdb" "string filename" "density.nvdb"
LightSource "infinite" "string filename" "image.exr"
LightSource "point" "spectrum I" "spectrum.spd"
AreaLightSource "diffuse" "string filename" "image.exr"
NamedMaterial "measured"
Shape "plymesh" "string filename" "model.ply" "texture alpha" "surface"
)pbrt");
    const auto id       = import(R"pbrt(
Option "msereferenceimage" "reference.exr"
Option "msereferenceout" "not-an-input.txt"
Camera "realistic" "string lensfile" "lens.dat" "string aperture" "aperture.png"
Film "rgb" "string filename" "not-an-input.exr"
WorldBegin
Include "sub/resources.pbrt"
)pbrt");
    const auto original = assets.get(id);
    ASSERT_EQ(original.files.size(), inputs.size() + 2);
    for (const auto& file : original.files)
    {
        const auto name = std::filesystem::path(file.originalName).filename().string();
        if (const auto found = inputs.find(name); found != inputs.end())
            EXPECT_EQ(data.readBlob(file.path), std::vector<uint8_t>(found->second.begin(), found->second.end()));
    }
    assets.shutdown();
    data.shutdown();
    std::filesystem::remove_all(source);
    const auto movedRoot = folder / "moved-assets";
    std::filesystem::rename(assetsRoot, movedRoot);
    assetsRoot = movedRoot;
    assets.init(assetsRoot);
    data.init(assetsRoot);
    EXPECT_EQ(Serialize(assets.get(id)), Serialize(original));
    for (const auto& file : assets.get(id).files)
        EXPECT_EQ(data.readBlob(file.path).size(), file.size);
    const auto& material  = assets.get(*original.shapes[0].material).as<PbrtMaterial>();
    const auto& reference = std::get<PbrtFileReference>(parameter(material.entity, "filename").references[0].target);
    EXPECT_EQ(data.readBlob(reference.path), std::vector<uint8_t>({'b', 'r', 'd', 'f', ' ', 'b', 'y', 't', 'e', 's'}));
}

TEST_F(PbrtImportTest, LargeTypedArraysAndDoubleMatricesRoundTrip)
{
    std::ostringstream text;
    text << std::setprecision(17);
    text << "WorldBegin\nShape \"trianglemesh\" \"point3 P\" [";
    std::vector<double> expectedPoints;
    for (int index = 0; index < 9000; ++index)
    {
        expectedPoints.push_back(index * 0.125);
        text << index * 0.125 << ' ';
    }
    text << "] \"integer indices\" [";
    std::vector<int32_t> expectedIndices;
    for (int index = 0; index < 18000; ++index)
    {
        expectedIndices.push_back(index % 3000);
        text << index % 3000 << ' ';
    }
    text << "]\n";
    const auto id = import(text.str());
    reload();
    const auto& entity  = assets.get(id).shapes[0].entity;
    const auto& points  = std::get<PbrtArrayReference>(parameter(entity, "P").value);
    const auto& indices = std::get<PbrtArrayReference>(parameter(entity, "indices").value);
    EXPECT_EQ(points.type, PbrtArrayType::Float64);
    EXPECT_EQ(indices.type, PbrtArrayType::Int32);
    EXPECT_EQ(std::get<PbrtFloatValues>(readPbrtArray(data, points)).values, expectedPoints);
    EXPECT_EQ(std::get<PbrtIntegerValues>(readPbrtArray(data, indices)).values, expectedIndices);
    glm::dmat4 matrix(1);
    matrix[1][0] = 0.12345678901234567;
    matrix[3][2] = 123456789.125;
    glm::dmat4 restored;
    Deserialize(json::parse(Serialize(matrix).dump()), restored);
    EXPECT_EQ(restored, matrix);
}

TEST_F(PbrtImportTest, TextureSlotsChooseTypedNamesAndKeepNumericPrecision)
{
    // The accented texture name is declared in NFC and referenced in NFD.
    const auto  id       = import(R"pbrt(
WorldBegin
Texture "shared" "float" "constant" "float value" 0.25
Texture "shared" "spectrum" "constant" "rgb value" [1 0 0]
Texture "café" "spectrum" "constant" "rgb value" [0 1 0]
Texture "scaled" "spectrum" "scale" "texture tex" "café" "texture scale" "shared"
Material "coatedconductor" "texture conductor.roughness" "shared" "texture conductor.eta" "shared"
Shape "sphere" "float radius" 1234567890123 "integer faceIndices" [-2147483648 2147483647]
)pbrt");
    const auto& scene    = assets.get(id);
    const auto& shape    = scene.shapes[0];
    const auto& material = assets.get(*shape.material).as<PbrtMaterial>();
    const auto& scalar   = parameter(material.entity, "conductor.roughness");
    const auto& spectrum = parameter(material.entity, "conductor.eta");
    const auto  scaled   = std::ranges::find_if(
        scene.textures,
        [&](const auto& textureId) { return assets.get(textureId).name == "scaled"; }
    );
    ASSERT_NE(scaled, scene.textures.end());
    const auto& normalized = parameter(assets.get(*scaled).entity, "tex");
    EXPECT_EQ(assets.get(std::get<PbrtTextureReference>(normalized.references[0].target).id).name, "café");
    EXPECT_EQ(
        assets.get(std::get<PbrtTextureReference>(scalar.references[0].target).id).valueType,
        PbrtTextureType::Float
    );
    EXPECT_EQ(
        assets.get(std::get<PbrtTextureReference>(spectrum.references[0].target).id).valueType,
        PbrtTextureType::Spectrum
    );
    EXPECT_DOUBLE_EQ(std::get<PbrtFloatValues>(parameter(shape.entity, "radius").value).values[0], 1234567890123.0);
    EXPECT_EQ(
        std::get<PbrtIntegerValues>(parameter(shape.entity, "faceIndices").value).values,
        (std::vector<int32_t>{-2147483647 - 1, 2147483647})
    );
}

TEST_F(PbrtImportTest, RepeatedImportsResetParserSession)
{
    const auto  first  = import(R"pbrt(
Option "rendercoordsys" "camera"
ColorSpace "rec2020"
TransformTimes 0 2
ActiveTransform StartTime
Translate 0 0 -2
ActiveTransform EndTime
Translate 0 0 -6
Rotate 90 0 0 1
ActiveTransform All
Camera "orthographic"
Film "rgb"
WorldBegin
Translate 1 2 3
CoordinateSystem "saved"
Translate 5 0 0
CoordSysTransform "saved"
Material "conductor"
Shape "sphere"
)pbrt");
    const auto& moving = assets.get(first);
    EXPECT_EQ(moving.film.colorSpace, "rec2020");
    const auto sine = std::sqrt(0.5);
    EXPECT_NEAR(moving.renderFromWorld[0][0], sine, 1e-12);
    EXPECT_NEAR(moving.renderFromWorld[0][1], sine, 1e-12);
    EXPECT_DOUBLE_EQ(moving.renderFromWorld[3].z, -4);
    EXPECT_NEAR(moving.shapes[0].renderFromObject.start[3].x, -sine, 1e-12);
    EXPECT_DOUBLE_EQ(moving.shapes[0].renderFromObject.start[3].z, -1);
    const auto second = import("WorldBegin\nShape \"sphere\"\n");
    EXPECT_NE(first, second);
    const auto& scene = assets.get(second);
    EXPECT_EQ(scene.camera.type, "perspective");
    EXPECT_EQ(scene.renderingSpace, "cameraworld");
    EXPECT_EQ(scene.film.colorSpace, "srgb");
    EXPECT_EQ(scene.renderFromWorld, glm::dmat4(1));
    EXPECT_TRUE(scene.options.empty());
    EXPECT_EQ(assets.get(*scene.shapes[0].material).as<PbrtMaterial>().entity.type, "diffuse");
}

TEST_F(PbrtImportTest, RejectsMissingDependenciesAndBrokenResourceGraphs)
{
    EXPECT_DEATH(
        import("WorldBegin\nTexture \"a\" \"spectrum\" \"imagemap\" \"string filename\" \"missing.exr\"\n"),
        "PBRT dependency"
    );
    EXPECT_DEATH(
        import("WorldBegin\nMaterial \"diffuse\" \"texture reflectance\" \"missing\"\nShape \"sphere\"\n"),
        "undefined PBRT texture"
    );
    EXPECT_DEATH(
        import(
            "WorldBegin\nTexture \"a\" \"float\" \"scale\" \"texture tex\" \"b\"\nTexture \"b\" \"float\" \"scale\" "
            "\"texture tex\" \"a\"\n"
        ),
        "cyclic PBRT"
    );
    EXPECT_DEATH(import("WorldBegin\nShape \"sphere\" \"float radius\" \"wrong\"\n"), "invalid parameter");
    EXPECT_DEATH(import("WorldBegin\nRotate 20 0 0 0\nShape \"sphere\"\n"), "non-finite transform");
}
}  // namespace
