// pbrt is Copyright(c) 1998-2020 Matt Pharr, Wenzel Jakob, and Greg Humphreys.
// The pbrt source code is licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0
// Derived from pbrt-v4 scene.h; see UPSTREAM.md.
#pragma once
#include "Parser.h"
namespace pbrt {
// SceneEntity Definition
struct SceneEntity {
    // SceneEntity Public Methods
    SceneEntity() = default;
    SceneEntity(const std::string &name, ParameterDictionary parameters, FileLoc loc)
        : name(internedStrings.Lookup(name)), parameters(parameters), loc(loc) {}

    std::string ToString() const {
        return StringPrintf("[ SceneEntity name: %s parameters: %s loc: %s ]", name,
                            parameters, loc);
    }

    // SceneEntity Public Members
    InternedString name;
    FileLoc loc;
    ParameterDictionary parameters;
    static InternCache<std::string> internedStrings;
};

struct TransformedSceneEntity : public SceneEntity {
    TransformedSceneEntity() = default;
    TransformedSceneEntity(const std::string &name, ParameterDictionary parameters,
                           FileLoc loc, const AnimatedTransform &renderFromObject)
        : SceneEntity(name, parameters, loc), renderFromObject(renderFromObject) {}

    std::string ToString() const {
        return StringPrintf("[ TransformedSeneEntity name: %s parameters: %s loc: %s "
                            "renderFromObject: %s ]",
                            name, parameters, loc, renderFromObject);
    }

    AnimatedTransform renderFromObject;
};

// CameraSceneEntity Definition
struct CameraSceneEntity : public SceneEntity {
    // CameraSceneEntity Public Methods
    CameraSceneEntity() = default;
    CameraSceneEntity(const std::string &name, ParameterDictionary parameters,
                      FileLoc loc, const CameraTransform &cameraTransform,
                      const std::string &medium)
        : SceneEntity(name, parameters, loc),
          cameraTransform(cameraTransform),
          medium(medium) {}

    std::string ToString() const {
        return StringPrintf("[ CameraSeneEntity name: %s parameters: %s loc: %s "
                            "cameraTransform: %s medium: %s ]",
                            name, parameters, loc, cameraTransform, medium);
    }

    CameraTransform cameraTransform;
    std::string medium;
};

struct ShapeSceneEntity : public SceneEntity {
    ShapeSceneEntity() = default;
    ShapeSceneEntity(const std::string &name, ParameterDictionary parameters, FileLoc loc,
                     const Transform *renderFromObject, const Transform *objectFromRender,
                     bool reverseOrientation, int materialIndex,
                     const std::string &materialName, int lightIndex,
                     const std::string &insideMedium, const std::string &outsideMedium)
        : SceneEntity(name, parameters, loc),
          renderFromObject(renderFromObject),
          objectFromRender(objectFromRender),
          reverseOrientation(reverseOrientation),
          materialIndex(materialIndex),
          materialName(materialName),
          lightIndex(lightIndex),
          insideMedium(insideMedium),
          outsideMedium(outsideMedium) {}

    std::string ToString() const {
        return StringPrintf(
            "[ ShapeSeneEntity name: %s parameters: %s loc: %s "
            "renderFromObject: %s objectFromRender: %s reverseOrientation: %s "
            "materialIndex: %d materialName: %s lightIndex: %d "
            "insideMedium: %s outsideMedium: %s]",
            name, parameters, loc, *renderFromObject, *objectFromRender,
            reverseOrientation, materialIndex, materialName, lightIndex, insideMedium,
            outsideMedium);
    }

    const Transform *renderFromObject = nullptr, *objectFromRender = nullptr;
    bool reverseOrientation = false;
    int materialIndex;  // one of these two...  std::variant?
    std::string materialName;
    int lightIndex = -1;
    std::string insideMedium, outsideMedium;
};

struct AnimatedShapeSceneEntity : public TransformedSceneEntity {
    AnimatedShapeSceneEntity() = default;
    AnimatedShapeSceneEntity(const std::string &name, ParameterDictionary parameters,
                             FileLoc loc, const AnimatedTransform &renderFromObject,
                             const Transform *identity, bool reverseOrientation,
                             int materialIndex, const std::string &materialName,
                             int lightIndex, const std::string &insideMedium,
                             const std::string &outsideMedium)
        : TransformedSceneEntity(name, parameters, loc, renderFromObject),
          identity(identity),
          reverseOrientation(reverseOrientation),
          materialIndex(materialIndex),
          materialName(materialName),
          lightIndex(lightIndex),
          insideMedium(insideMedium),
          outsideMedium(outsideMedium) {}

    std::string ToString() const {
        return StringPrintf(
            "[ AnimatedShapeSeneEntity name: %s parameters: %s loc: %s "
            "renderFromObject: %s reverseOrientation: %s materialIndex: %d "
            "materialName: %s insideMedium: %s outsideMedium: %s]",
            name, parameters, loc, renderFromObject, reverseOrientation, materialIndex,
            materialName, insideMedium, outsideMedium);
    }

    const Transform *identity = nullptr;
    bool reverseOrientation = false;
    int materialIndex;  // one of these two...  std::variant?
    std::string materialName;
    int lightIndex = -1;
    std::string insideMedium, outsideMedium;
};

struct InstanceDefinitionSceneEntity {
    InstanceDefinitionSceneEntity() = default;
    InstanceDefinitionSceneEntity(const std::string &name, FileLoc loc)
        : name(SceneEntity::internedStrings.Lookup(name)), loc(loc) {}

    std::string ToString() const {
        return StringPrintf("[ InstanceDefinitionSceneEntity name: %s loc: %s "
                            " shapes: %s animatedShapes: %s ]",
                            name, loc, shapes, animatedShapes);
    }

    InternedString name;
    FileLoc loc;
    std::vector<ShapeSceneEntity> shapes;
    std::vector<AnimatedShapeSceneEntity> animatedShapes;
};

using MediumSceneEntity = TransformedSceneEntity;
using TextureSceneEntity = TransformedSceneEntity;

struct LightSceneEntity : public TransformedSceneEntity {
    LightSceneEntity() = default;
    LightSceneEntity(const std::string &name, ParameterDictionary parameters, FileLoc loc,
                     const AnimatedTransform &renderFromLight, const std::string &medium)
        : TransformedSceneEntity(name, parameters, loc, renderFromLight),
          medium(medium) {}

    std::string ToString() const {
        return StringPrintf("[ LightSeneEntity name: %s parameters: %s loc: %s "
                            "renderFromObject: %s medium: %s ]",
                            name, parameters, loc, renderFromObject, medium);
    }

    std::string medium;
};

struct InstanceSceneEntity {
    InstanceSceneEntity() = default;
    InstanceSceneEntity(const std::string &n, FileLoc loc,
                        const AnimatedTransform &renderFromInstanceAnim)
        : name(SceneEntity::internedStrings.Lookup(n)),
          loc(loc),
          renderFromInstanceAnim(new AnimatedTransform(renderFromInstanceAnim)) {
        CHECK(this->renderFromInstanceAnim->IsAnimated());
    }
    InstanceSceneEntity(const std::string &n, FileLoc loc,
                        const Transform *renderFromInstance)
        : name(SceneEntity::internedStrings.Lookup(n)),
          loc(loc),
          renderFromInstance(renderFromInstance) {}

    std::string ToString() const {
        return StringPrintf(
            "[ InstanceSeneEntity name: %s loc: %s "
            "renderFromInstanceAnim: %s renderFromInstance: %s ]",
            name, loc,
            renderFromInstanceAnim ? renderFromInstanceAnim->ToString()
                                   : std::string("nullptr"),
            renderFromInstance ? renderFromInstance->ToString() : std::string("nullptr"));
    }

    InternedString name;
    FileLoc loc;
    AnimatedTransform *renderFromInstanceAnim = nullptr;
    const Transform *renderFromInstance = nullptr;
};

// MaxTransforms Definition
constexpr int MaxTransforms = 2;

// TransformSet Definition
struct TransformSet {
    // TransformSet Public Methods
    Transform &operator[](int i) {
        CHECK_GE(i, 0);
        CHECK_LT(i, MaxTransforms);
        return t[i];
    }
    const Transform &operator[](int i) const {
        CHECK_GE(i, 0);
        CHECK_LT(i, MaxTransforms);
        return t[i];
    }
    friend TransformSet Inverse(const TransformSet &ts) {
        TransformSet tInv;
        for (int i = 0; i < MaxTransforms; ++i)
            tInv.t[i] = Inverse(ts.t[i]);
        return tInv;
    }
    bool IsAnimated() const {
        for (int i = 0; i < MaxTransforms - 1; ++i)
            if (t[i] != t[i + 1])
                return true;
        return false;
    }

  private:
    Transform t[MaxTransforms];
};


class BasicSceneBuilder;

class BasicScene {
public:
    SceneEntity filter, film, sampler, integrator, accelerator;
    CameraSceneEntity camera;
    std::vector<std::pair<std::string, SceneEntity>> materials;
    std::vector<std::pair<std::string, TextureSceneEntity>> floatTextures, spectrumTextures;
    std::vector<MediumSceneEntity> media;
    std::vector<LightSceneEntity> lights;
    std::vector<SceneEntity> areaLights;
    std::vector<ShapeSceneEntity> shapes;
    std::vector<AnimatedShapeSceneEntity> animatedShapes;
    std::vector<InstanceDefinitionSceneEntity> definitions;
    std::vector<InstanceSceneEntity> instances;
    std::vector<PbrtOption> options;
    std::vector<std::unique_ptr<BasicSceneBuilder>> importedBuilders;
    void SetOptions(SceneEntity f, SceneEntity fi, CameraSceneEntity c, SceneEntity s, SceneEntity i, SceneEntity a) {
        filter=std::move(f); film=std::move(fi); camera=std::move(c); sampler=std::move(s); integrator=std::move(i); accelerator=std::move(a);
    }
    int AddMaterial(SceneEntity e) { int index=int(materials.size()); materials.emplace_back("", std::move(e)); return index; }
    void AddNamedMaterial(std::string n, SceneEntity e) { materials.emplace_back(std::move(n), std::move(e)); }
    void AddMedium(MediumSceneEntity e) { media.push_back(std::move(e)); }
    void AddFloatTexture(std::string n, TextureSceneEntity e) { floatTextures.emplace_back(std::move(n), std::move(e)); }
    void AddSpectrumTexture(std::string n, TextureSceneEntity e) { spectrumTextures.emplace_back(std::move(n), std::move(e)); }
    void AddLight(LightSceneEntity e) { lights.push_back(std::move(e)); }
    int AddAreaLight(SceneEntity e) { areaLights.push_back(std::move(e)); return int(areaLights.size()-1); }
    void AddShapes(pstd::span<ShapeSceneEntity> s) { for(auto &e:s) shapes.push_back(e); }
    void AddAnimatedShape(AnimatedShapeSceneEntity e) { animatedShapes.push_back(std::move(e)); }
    void AddInstanceDefinition(InstanceDefinitionSceneEntity e) { definitions.push_back(std::move(e)); }
    void AddInstanceUses(pstd::span<InstanceSceneEntity> s) { for(auto &e:s) instances.push_back(e); }
    void Done() {}
};
// BasicSceneBuilder Definition
class BasicSceneBuilder : public ParserTarget {
  public:
    // BasicSceneBuilder Public Methods
    BasicSceneBuilder(BasicScene *scene);
    void Option(const std::string &name, const std::string &value, FileLoc loc);
    void Identity(FileLoc loc);
    void Translate(Float dx, Float dy, Float dz, FileLoc loc);
    void Rotate(Float angle, Float ax, Float ay, Float az, FileLoc loc);
    void Scale(Float sx, Float sy, Float sz, FileLoc loc);
    void LookAt(Float ex, Float ey, Float ez, Float lx, Float ly, Float lz, Float ux,
                Float uy, Float uz, FileLoc loc);
    void ConcatTransform(Float transform[16], FileLoc loc);
    void Transform(Float transform[16], FileLoc loc);
    void CoordinateSystem(const std::string &, FileLoc loc);
    void CoordSysTransform(const std::string &, FileLoc loc);
    void ActiveTransformAll(FileLoc loc);
    void ActiveTransformEndTime(FileLoc loc);
    void ActiveTransformStartTime(FileLoc loc);
    void TransformTimes(Float start, Float end, FileLoc loc);
    void ColorSpace(const std::string &n, FileLoc loc);
    void PixelFilter(const std::string &name, ParsedParameterVector params, FileLoc loc);
    void Film(const std::string &type, ParsedParameterVector params, FileLoc loc);
    void Sampler(const std::string &name, ParsedParameterVector params, FileLoc loc);
    void Accelerator(const std::string &name, ParsedParameterVector params, FileLoc loc);
    void Integrator(const std::string &name, ParsedParameterVector params, FileLoc loc);
    void Camera(const std::string &, ParsedParameterVector params, FileLoc loc);
    void MakeNamedMedium(const std::string &name, ParsedParameterVector params,
                         FileLoc loc);
    void MediumInterface(const std::string &insideName, const std::string &outsideName,
                         FileLoc loc);
    void WorldBegin(FileLoc loc);
    void AttributeBegin(FileLoc loc);
    void AttributeEnd(FileLoc loc);
    void Attribute(const std::string &target, ParsedParameterVector params, FileLoc loc);
    void Texture(const std::string &name, const std::string &type,
                 const std::string &texname, ParsedParameterVector params, FileLoc loc);
    void Material(const std::string &name, ParsedParameterVector params, FileLoc loc);
    void MakeNamedMaterial(const std::string &name, ParsedParameterVector params,
                           FileLoc loc);
    void NamedMaterial(const std::string &name, FileLoc loc);
    void LightSource(const std::string &name, ParsedParameterVector params, FileLoc loc);
    void AreaLightSource(const std::string &name, ParsedParameterVector params,
                         FileLoc loc);
    void Shape(const std::string &name, ParsedParameterVector params, FileLoc loc);
    void ReverseOrientation(FileLoc loc);
    void ObjectBegin(const std::string &name, FileLoc loc);
    void ObjectEnd(FileLoc loc);
    void ObjectInstance(const std::string &name, FileLoc loc);

    void EndOfFiles();

    BasicSceneBuilder *CopyForImport();
    void MergeImported(BasicSceneBuilder *);

    std::string ToString() const;
    class Transform RenderFromWorldForExport() const { return renderFromWorld; }

  private:
    // BasicSceneBuilder::GraphicsState Definition
    struct GraphicsState {
        // GraphicsState Public Methods
        GraphicsState();

        template <typename F>
        void ForActiveTransforms(F func) {
            for (int i = 0; i < MaxTransforms; ++i)
                if (activeTransformBits & (1 << i))
                    ctm[i] = func(ctm[i]);
        }

        // GraphicsState Public Members
        std::string currentInsideMedium, currentOutsideMedium;

        int currentMaterialIndex = 0;
        std::string currentMaterialName;

        std::string areaLightName;
        ParameterDictionary areaLightParams;
        FileLoc areaLightLoc;

        ParsedParameterVector shapeAttributes;
        ParsedParameterVector lightAttributes;
        ParsedParameterVector materialAttributes;
        ParsedParameterVector mediumAttributes;
        ParsedParameterVector textureAttributes;
        bool reverseOrientation = false;
        const RGBColorSpace *colorSpace = RGBColorSpace::sRGB;
        TransformSet ctm;
        uint32_t activeTransformBits = AllTransformsBits;
        Float transformStartTime = 0, transformEndTime = 1;
    };

    friend void parse(ParserTarget *scene, std::unique_ptr<Tokenizer> t);
    // BasicSceneBuilder Private Methods
    class Transform RenderFromObject(int index) const {
        return pbrt::Transform((renderFromWorld * graphicsState.ctm[index]).GetMatrix());
    }

    AnimatedTransform RenderFromObject() const {
        return {RenderFromObject(0), graphicsState.transformStartTime,
                RenderFromObject(1), graphicsState.transformEndTime};
    }

    bool CTMIsAnimated() const { return graphicsState.ctm.IsAnimated(); }

    // BasicSceneBuilder Private Members
    BasicScene *scene;
    enum class BlockState { OptionsBlock, WorldBlock };
    BlockState currentBlock = BlockState::OptionsBlock;
    GraphicsState graphicsState;
    static constexpr int StartTransformBits = 1 << 0;
    static constexpr int EndTransformBits = 1 << 1;
    static constexpr int AllTransformsBits = (1 << MaxTransforms) - 1;
    std::map<std::string, TransformSet> namedCoordinateSystems;
    class Transform renderFromWorld;
    InternCache<class Transform> transformCache;
    std::vector<GraphicsState> pushedGraphicsStates;
    std::vector<std::pair<char, FileLoc>> pushStack;  // 'a': attribute, 'o': object
    struct ActiveInstanceDefinition {
        ActiveInstanceDefinition(std::string name, FileLoc loc) : entity(name, loc) {}

        std::mutex mutex;
        std::atomic<int> activeImports{1};
        InstanceDefinitionSceneEntity entity;
        ActiveInstanceDefinition *parent = nullptr;
    };
    ActiveInstanceDefinition *activeInstanceDefinition = nullptr;

    // Buffer these both to avoid mutex contention and so that they are
    // consistently ordered across runs.
    std::vector<ShapeSceneEntity> shapes;
    std::vector<InstanceSceneEntity> instanceUses;

    std::set<std::string> namedMaterialNames, mediumNames;
    std::set<std::string> floatTextureNames, spectrumTextureNames, instanceNames;
    int currentMaterialIndex = 0, currentLightIndex = -1;
    SceneEntity sampler;
    SceneEntity film, integrator, filter, accelerator;
    CameraSceneEntity camera;
};


}
