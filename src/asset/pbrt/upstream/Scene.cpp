// pbrt is Copyright(c) 1998-2020 Matt Pharr, Wenzel Jakob, and Greg Humphreys.
// The pbrt source code is licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0
// Derived from pbrt-v4 scene.cpp; RenderLab modifications are listed in UPSTREAM.md.
#include "Scene.h"
namespace pbrt {

InternCache<std::string> SceneEntity::internedStrings;

template <typename T, typename U>
static std::string ToString(const std::map<T, U> &m) {
    std::string s = "[ ";
    for (const auto &iter : m)
        s += StringPrintf("%s:%s ", iter.first, iter.second);
    s += "]";
    return s;
}

template <typename T, typename U>
static std::string ToString(const std::pair<T, U> &p) {
    return StringPrintf("[ std::pair first: %s second: %s ]", p.first, p.second);
}

std::string BasicSceneBuilder::ToString() const {
    return StringPrintf(
        "[ BasicSceneBuilder camera: %s film: %s sampler: %s integrator: %s "
        "filter: %s accelerator: %s ]",
        camera, film, sampler, integrator, filter, accelerator);
}

BasicSceneBuilder::GraphicsState::GraphicsState() {
    currentMaterialIndex = 0;
}

// API State Macros
#define VERIFY_OPTIONS(func)                                   \
    if (currentBlock == BlockState::WorldBlock) {              \
        ErrorExit(&loc,                                        \
                  "Options cannot be set inside world block; " \
                  "\"%s\" is not allowed.",                    \
                  func);                                       \
        return;                                                \
    } else /* swallow trailing semicolon */
#define VERIFY_WORLD(func)                                         \
    if (currentBlock == BlockState::OptionsBlock) {                \
        ErrorExit(&loc,                                            \
                  "Scene description must be inside world block; " \
                  "\"%s\" is not allowed.",                        \
                  func);                                           \
        return;                                                    \
    } else /* swallow trailing semicolon */

STAT_COUNTER("Scene/Object instances created", nObjectInstancesCreated);
STAT_COUNTER("Scene/Object instances used", nObjectInstancesUsed);

// BasicSceneBuilder Method Definitions
BasicSceneBuilder::BasicSceneBuilder(BasicScene *scene)
    : scene(scene)
{
    // Set scene defaults
    camera.name = SceneEntity::internedStrings.Lookup("perspective");
    sampler.name = SceneEntity::internedStrings.Lookup("zsobol");
    filter.name = SceneEntity::internedStrings.Lookup("gaussian");
    integrator.name = SceneEntity::internedStrings.Lookup("volpath");
    accelerator.name = SceneEntity::internedStrings.Lookup("bvh");

    film.name = SceneEntity::internedStrings.Lookup("rgb");
    film.parameters = ParameterDictionary({}, RGBColorSpace::sRGB);

    ParameterDictionary dict({}, RGBColorSpace::sRGB);
    currentMaterialIndex = scene->AddMaterial(SceneEntity("diffuse", dict, {}));
}

void BasicSceneBuilder::ReverseOrientation(FileLoc loc) {
    VERIFY_WORLD("ReverseOrientation");
    graphicsState.reverseOrientation = !graphicsState.reverseOrientation;
}

void BasicSceneBuilder::ColorSpace(const std::string &name, FileLoc loc) {
    if (const RGBColorSpace *cs = RGBColorSpace::GetNamed(name))
        graphicsState.colorSpace = cs;
    else
        Error(&loc, "%s: color space unknown", name);
}

void BasicSceneBuilder::Identity(FileLoc loc) {
    graphicsState.ForActiveTransforms([](auto t) { return pbrt::Transform(); });
}

void BasicSceneBuilder::Translate(Float dx, Float dy, Float dz, FileLoc loc) {
    graphicsState.ForActiveTransforms(
        [=](auto t) { return t * pbrt::Translate(Vector3f(dx, dy, dz)); });
}

void BasicSceneBuilder::CoordinateSystem(const std::string &origName, FileLoc loc) {
    std::string name = NormalizeUTF8(origName);
    namedCoordinateSystems[name] = graphicsState.ctm;
}

void BasicSceneBuilder::CoordSysTransform(const std::string &origName, FileLoc loc) {
    std::string name = NormalizeUTF8(origName);
    if (namedCoordinateSystems.find(name) != namedCoordinateSystems.end())
        graphicsState.ctm = namedCoordinateSystems[name];
    else
        Warning(&loc, "Couldn't find named coordinate system \"%s\"", name);
}

void BasicSceneBuilder::Camera(const std::string &name, ParsedParameterVector params,
                               FileLoc loc) {
    ParameterDictionary dict(std::move(params), graphicsState.colorSpace);

    VERIFY_OPTIONS("Camera");

    TransformSet cameraFromWorld = graphicsState.ctm;
    TransformSet worldFromCamera = Inverse(graphicsState.ctm);
    namedCoordinateSystems["camera"] = Inverse(cameraFromWorld);

    CameraTransform cameraTransform(
        AnimatedTransform(worldFromCamera[0], graphicsState.transformStartTime,
                          worldFromCamera[1], graphicsState.transformEndTime));
    renderFromWorld = cameraTransform.RenderFromWorld();

    camera = CameraSceneEntity(name, std::move(dict), loc, cameraTransform,
                               graphicsState.currentOutsideMedium);
}

void BasicSceneBuilder::AttributeBegin(FileLoc loc) {
    VERIFY_WORLD("AttributeBegin");
    pushedGraphicsStates.push_back(graphicsState);
    pushStack.push_back(std::make_pair('a', loc));
}

void BasicSceneBuilder::AttributeEnd(FileLoc loc) {
    VERIFY_WORLD("AttributeEnd");
    // Issue error on unmatched _AttributeEnd_
    if (pushedGraphicsStates.empty()) {
        Error(&loc, "Unmatched AttributeEnd encountered. Ignoring it.");
        return;
    }

    // NOTE: must keep the following consistent with code in ObjectEnd
    graphicsState = std::move(pushedGraphicsStates.back());
    pushedGraphicsStates.pop_back();

    if (pushStack.back().first == 'o')
        ErrorExitDeferred(&loc,
                          "Mismatched nesting: open ObjectBegin from %s at AttributeEnd",
                          pushStack.back().second);
    else
        CHECK_EQ(pushStack.back().first, 'a');
    pushStack.pop_back();
}

void BasicSceneBuilder::Attribute(const std::string &target, ParsedParameterVector attrib,
                                  FileLoc loc) {
    ParsedParameterVector *currentAttributes = nullptr;
    if (target == "shape") {
        currentAttributes = &graphicsState.shapeAttributes;
    } else if (target == "light") {
        currentAttributes = &graphicsState.lightAttributes;
    } else if (target == "material") {
        currentAttributes = &graphicsState.materialAttributes;
    } else if (target == "medium") {
        currentAttributes = &graphicsState.mediumAttributes;
    } else if (target == "texture") {
        currentAttributes = &graphicsState.textureAttributes;
    } else {
        ErrorExitDeferred(
            &loc,
            "Unknown attribute target \"%s\". Must be \"shape\", \"light\", "
            "\"material\", \"medium\", or \"texture\".",
            target);
        return;
    }

    // Note that we hold on to the current color space and associate it
    // with the parameters...
    for (ParsedParameter *p : attrib) {
        p->mayBeUnused = true;
        p->colorSpace = graphicsState.colorSpace;
        currentAttributes->push_back(p);
    }
}

void BasicSceneBuilder::Sampler(const std::string &name, ParsedParameterVector params,
                                FileLoc loc) {
    ParameterDictionary dict(std::move(params), graphicsState.colorSpace);
    VERIFY_OPTIONS("Sampler");
    sampler = SceneEntity(name, std::move(dict), loc);
}

void BasicSceneBuilder::WorldBegin(FileLoc loc) {
    VERIFY_OPTIONS("WorldBegin");
    // Reset graphics state for _WorldBegin_
    currentBlock = BlockState::WorldBlock;
    for (int i = 0; i < MaxTransforms; ++i)
        graphicsState.ctm[i] = pbrt::Transform();
    graphicsState.activeTransformBits = AllTransformsBits;
    namedCoordinateSystems["world"] = graphicsState.ctm;

    // Pass pre-_WorldBegin_ entities to _scene_
    scene->SetOptions(filter, film, camera, sampler, integrator, accelerator);
}

void BasicSceneBuilder::MakeNamedMedium(const std::string &origName,
                                        ParsedParameterVector params, FileLoc loc) {
    std::string name = NormalizeUTF8(origName);
    // Issue error if medium _name_ is multiply defined
    if (mediumNames.find(name) != mediumNames.end()) {
        ErrorExitDeferred(&loc, "Named medium \"%s\" redefined.", name);
        return;
    }
    mediumNames.insert(name);

    // Create _ParameterDictionary_ for medium and call _AddMedium()_
    ParameterDictionary dict(std::move(params), graphicsState.mediumAttributes,
                             graphicsState.colorSpace);
    scene->AddMedium(MediumSceneEntity(name, std::move(dict), loc, RenderFromObject()));
}

void BasicSceneBuilder::LightSource(const std::string &name, ParsedParameterVector params,
                                    FileLoc loc) {
    VERIFY_WORLD("LightSource");
    ParameterDictionary dict(std::move(params), graphicsState.lightAttributes,
                             graphicsState.colorSpace);
    scene->AddLight(LightSceneEntity(name, std::move(dict), loc, RenderFromObject(),
                                     graphicsState.currentOutsideMedium));
}

void BasicSceneBuilder::Shape(const std::string &name, ParsedParameterVector params,
                              FileLoc loc) {
    VERIFY_WORLD("Shape");

    ParameterDictionary dict(std::move(params), graphicsState.shapeAttributes,
                             graphicsState.colorSpace);

    int areaLightIndex = -1;
    if (!graphicsState.areaLightName.empty()) {
        areaLightIndex = scene->AddAreaLight(SceneEntity(graphicsState.areaLightName,
                                                         graphicsState.areaLightParams,
                                                         graphicsState.areaLightLoc));
        if (activeInstanceDefinition)
            Warning(&loc, "Area lights not supported with object instancing");
    }

    if (CTMIsAnimated()) {
        AnimatedTransform renderFromShape = RenderFromObject();
        const class Transform *identity = transformCache.Lookup(pbrt::Transform());

        AnimatedShapeSceneEntity entity(
            {name, std::move(dict), loc, renderFromShape, identity,
             graphicsState.reverseOrientation, graphicsState.currentMaterialIndex,
             graphicsState.currentMaterialName, areaLightIndex,
             graphicsState.currentInsideMedium, graphicsState.currentOutsideMedium});

        if (activeInstanceDefinition)
            activeInstanceDefinition->entity.animatedShapes.push_back(std::move(entity));
        else
            scene->AddAnimatedShape(std::move(entity));
    } else {
        const class Transform *renderFromObject =
            transformCache.Lookup(RenderFromObject(0));
        const class Transform *objectFromRender =
            transformCache.Lookup(Inverse(*renderFromObject));

        ShapeSceneEntity entity(
            {name, std::move(dict), loc, renderFromObject, objectFromRender,
             graphicsState.reverseOrientation, graphicsState.currentMaterialIndex,
             graphicsState.currentMaterialName, areaLightIndex,
             graphicsState.currentInsideMedium, graphicsState.currentOutsideMedium});
        if (activeInstanceDefinition)
            activeInstanceDefinition->entity.shapes.push_back(std::move(entity));
        else
            shapes.push_back(std::move(entity));
    }
}

void BasicSceneBuilder::ObjectBegin(const std::string &origName, FileLoc loc) {
    std::string name = NormalizeUTF8(origName);

    VERIFY_WORLD("ObjectBegin");
    pushedGraphicsStates.push_back(graphicsState);

    pushStack.push_back(std::make_pair('o', loc));

    if (activeInstanceDefinition) {
        ErrorExitDeferred(&loc, "ObjectBegin called inside of instance definition");
        return;
    }

    if (instanceNames.find(name) != instanceNames.end()) {
        ErrorExitDeferred(&loc, "%s: trying to redefine an object instance", name);
        return;
    }
    instanceNames.insert(name);

    activeInstanceDefinition = new ActiveInstanceDefinition(name, loc);
}

void BasicSceneBuilder::ObjectEnd(FileLoc loc) {
    VERIFY_WORLD("ObjectEnd");
    if (!activeInstanceDefinition) {
        ErrorExitDeferred(&loc, "ObjectEnd called outside of instance definition");
        return;
    }
    if (activeInstanceDefinition->parent) {
        ErrorExitDeferred(&loc, "ObjectEnd called inside Import for instance definition");
        return;
    }

    // NOTE: Must keep the following consistent with AttributeEnd
    graphicsState = std::move(pushedGraphicsStates.back());
    pushedGraphicsStates.pop_back();

    ++nObjectInstancesCreated;

    if (pushStack.back().first == 'a')
        ErrorExitDeferred(&loc,
                          "Mismatched nesting: open AttributeBegin from %s at ObjectEnd",
                          pushStack.back().second);
    else
        CHECK_EQ(pushStack.back().first, 'o');
    pushStack.pop_back();

    // Otherwise it will be taken care of in MergeImported()
    if (--activeInstanceDefinition->activeImports == 0) {
        scene->AddInstanceDefinition(std::move(activeInstanceDefinition->entity));
        delete activeInstanceDefinition;
    }

    activeInstanceDefinition = nullptr;
}

void BasicSceneBuilder::ObjectInstance(const std::string &origName, FileLoc loc) {
    std::string name = NormalizeUTF8(origName);
    VERIFY_WORLD("ObjectInstance");

    if (activeInstanceDefinition) {
        ErrorExitDeferred(&loc,
                          "ObjectInstance can't be called inside instance definition");
        return;
    }

    class Transform worldFromRender = Inverse(renderFromWorld);

    if (CTMIsAnimated()) {
        AnimatedTransform animatedRenderFromInstance(
            RenderFromObject(0) * worldFromRender, graphicsState.transformStartTime,
            RenderFromObject(1) * worldFromRender, graphicsState.transformEndTime);

        // For very small changes, animatedRenderFromInstance may have both
        // xforms equal even if CTMIsAnimated() has returned true. Fall
        // through to create a regular non-animated instance in that case.
        if (animatedRenderFromInstance.IsAnimated()) {
            instanceUses.push_back(
                InstanceSceneEntity(name, loc, animatedRenderFromInstance));
            return;
        }
    }

    const class Transform *renderFromInstance =
        transformCache.Lookup(RenderFromObject(0) * worldFromRender);
    instanceUses.push_back(InstanceSceneEntity(name, loc, renderFromInstance));
}

void BasicSceneBuilder::EndOfFiles() {
    if (currentBlock != BlockState::WorldBlock)
        ErrorExitDeferred("End of files before \"WorldBegin\".");

    // Ensure there are no pushed graphics states
    while (!pushedGraphicsStates.empty()) {
        ErrorExitDeferred("Missing end to AttributeBegin");
        pushedGraphicsStates.pop_back();
    }

    if (errorExit)
        ErrorExit("Fatal errors during scene construction");

    if (!shapes.empty())
        scene->AddShapes(shapes);
    if (!instanceUses.empty())
        scene->AddInstanceUses(instanceUses);

    scene->Done();
}

BasicSceneBuilder *BasicSceneBuilder::CopyForImport() {
    BasicSceneBuilder *importBuilder = new BasicSceneBuilder(scene);
    scene->importedBuilders.emplace_back(importBuilder);
    importBuilder->renderFromWorld = renderFromWorld;
    importBuilder->graphicsState = graphicsState;
    importBuilder->currentBlock = currentBlock;
    if (activeInstanceDefinition) {
        importBuilder->activeInstanceDefinition = new ActiveInstanceDefinition(
            activeInstanceDefinition->entity.name, activeInstanceDefinition->entity.loc);

        // In case of nested imports, go up to the true root parent since
        // that's where we need to merge our shapes and that's where the
        // refcount is.
        ActiveInstanceDefinition *parent = activeInstanceDefinition;
        while (parent->parent)
            parent = parent->parent;
        importBuilder->activeInstanceDefinition->parent = parent;
        ++parent->activeImports;
    }
    return importBuilder;
}

void BasicSceneBuilder::MergeImported(BasicSceneBuilder *imported) {
    while (!imported->pushedGraphicsStates.empty()) {
        ErrorExitDeferred("Missing end to AttributeBegin");
        imported->pushedGraphicsStates.pop_back();
    }

    errorExit |= imported->errorExit;

    if (!imported->shapes.empty())
        scene->AddShapes(imported->shapes);
    if (!imported->instanceUses.empty())
        scene->AddInstanceUses(imported->instanceUses);

    auto mergeVector = [](auto &base, auto &imported) {
        if (base.empty())
            base = std::move(imported);
        else {
            base.reserve(base.size() + imported.size());
            std::move(std::begin(imported), std::end(imported), std::back_inserter(base));
            imported.clear();
            imported.shrink_to_fit();
        }
    };
    if (imported->activeInstanceDefinition) {
        ActiveInstanceDefinition *current = imported->activeInstanceDefinition;
        ActiveInstanceDefinition *parent = current->parent;
        CHECK(parent != nullptr);

        std::lock_guard<std::mutex> lock(parent->mutex);
        mergeVector(parent->entity.shapes, current->entity.shapes);
        mergeVector(parent->entity.animatedShapes, current->entity.animatedShapes);

        delete current;
        imported->activeInstanceDefinition = nullptr;

        if (--parent->activeImports == 0)
            scene->AddInstanceDefinition(std::move(parent->entity));


    }

    auto mergeSet = [this](auto &base, auto &imported, const char *name) {
        for (const auto &item : imported) {
            if (base.find(item) != base.end())
                ErrorExitDeferred("%s: multiply defined %s.", item, name);
            base.insert(std::move(item));
        }
        imported.clear();
    };
    mergeSet(namedMaterialNames, imported->namedMaterialNames, "named material");
    mergeSet(floatTextureNames, imported->floatTextureNames, "texture");
    mergeSet(spectrumTextureNames, imported->spectrumTextureNames, "texture");
}

void BasicSceneBuilder::Option(const std::string &name, const std::string &value,
                               FileLoc loc) {
    scene->options.push_back({name, value, {loc.filename, loc.line, loc.column}});
    std::string nName = normalizeArg(name);

    if (nName == "disablepixeljitter") {
        if (value == "true")
            Options->disablePixelJitter = true;
        else if (value == "false")
            Options->disablePixelJitter = false;
        else
            ErrorExitDeferred(&loc, "%s: expected \"true\" or \"false\" for option value",
                              value);
    } else if (nName == "disabletexturefiltering") {
        if (value == "true")
            Options->disableTextureFiltering = true;
        else if (value == "false")
            Options->disableTextureFiltering = false;
        else
            ErrorExitDeferred(&loc, "%s: expected \"true\" or \"false\" for option value",
                              value);
    } else if (nName == "disablewavelengthjitter") {
        if (value == "true")
            Options->disableWavelengthJitter = true;
        else if (value == "false")
            Options->disableWavelengthJitter = false;
        else
            ErrorExitDeferred(&loc, "%s: expected \"true\" or \"false\" for option value",
                              value);
    } else if (nName == "displacementedgescale") {
        if (!Atof(value, &Options->displacementEdgeScale))
            ErrorExitDeferred(&loc, "%s: expected floating-point option value", value);
    } else if (nName == "msereferenceimage") {
        if (value.size() < 3 || value.front() != '"' || value.back() != '"')
            ErrorExitDeferred(&loc, "%s: expected quoted string for option value", value);
        Options->mseReferenceImage = value.substr(1, value.size() - 2);
    } else if (nName == "msereferenceout") {
        if (value.size() < 3 || value.front() != '"' || value.back() != '"')
            ErrorExitDeferred(&loc, "%s: expected quoted string for option value", value);
        Options->mseReferenceOutput = value.substr(1, value.size() - 2);
    } else if (nName == "rendercoordsys") {
        if (value.size() < 3 || value.front() != '"' || value.back() != '"')
            ErrorExitDeferred(&loc, "%s: expected quoted string for option value", value);
        std::string renderCoordSys = value.substr(1, value.size() - 2);
        if (renderCoordSys == "camera")
            Options->renderingSpace = RenderingCoordinateSystem::Camera;
        else if (renderCoordSys == "cameraworld")
            Options->renderingSpace = RenderingCoordinateSystem::CameraWorld;
        else if (renderCoordSys == "world")
            Options->renderingSpace = RenderingCoordinateSystem::World;
        else
            ErrorExit("%s: unknown rendering coordinate system.", renderCoordSys);
    } else if (nName == "seed") {
        Options->seed = std::atoi(value.c_str());
    } else if (nName == "forcediffuse") {
        if (value == "true")
            Options->forceDiffuse = true;
        else if (value == "false")
            Options->forceDiffuse = false;
        else
            ErrorExitDeferred(&loc, "%s: expected \"true\" or \"false\" for option value",
                              value);
    } else if (nName == "pixelstats") {
        if (value == "true")
            Options->recordPixelStatistics = true;
        else if (value == "false")
            Options->recordPixelStatistics = false;
        else
            ErrorExitDeferred(&loc, "%s: expected \"true\" or \"false\" for option value",
                              value);
    } else if (nName == "wavefront") {
        if (value == "true")
            Options->wavefront = true;
        else if (value == "false")
            Options->wavefront = false;
        else
            ErrorExitDeferred(&loc, "%s: expected \"true\" or \"false\" for option value",
                              value);
    } else
        ErrorExitDeferred(&loc, "%s: unknown option", name);

}

void BasicSceneBuilder::Transform(Float tr[16], FileLoc loc) {
    graphicsState.ForActiveTransforms([=](auto t) {
        return Transpose(pbrt::Transform(SquareMatrix<4>(pstd::MakeSpan(tr, 16))));
    });
}

void BasicSceneBuilder::ConcatTransform(Float tr[16], FileLoc loc) {
    graphicsState.ForActiveTransforms([=](auto t) {
        return t * Transpose(pbrt::Transform(SquareMatrix<4>(pstd::MakeSpan(tr, 16))));
    });
}

void BasicSceneBuilder::Rotate(Float angle, Float dx, Float dy, Float dz, FileLoc loc) {
    graphicsState.ForActiveTransforms(
        [=](auto t) { return t * pbrt::Rotate(angle, Vector3f(dx, dy, dz)); });
}

void BasicSceneBuilder::Scale(Float sx, Float sy, Float sz, FileLoc loc) {
    graphicsState.ForActiveTransforms(
        [=](auto t) { return t * pbrt::Scale(sx, sy, sz); });
}

void BasicSceneBuilder::LookAt(Float ex, Float ey, Float ez, Float lx, Float ly, Float lz,
                               Float ux, Float uy, Float uz, FileLoc loc) {
    class Transform lookAt =
        pbrt::LookAt(Point3f(ex, ey, ez), Point3f(lx, ly, lz), Vector3f(ux, uy, uz));
    graphicsState.ForActiveTransforms([=](auto t) { return t * lookAt; });
}

void BasicSceneBuilder::ActiveTransformAll(FileLoc loc) {
    graphicsState.activeTransformBits = AllTransformsBits;
}

void BasicSceneBuilder::ActiveTransformEndTime(FileLoc loc) {
    graphicsState.activeTransformBits = EndTransformBits;
}

void BasicSceneBuilder::ActiveTransformStartTime(FileLoc loc) {
    graphicsState.activeTransformBits = StartTransformBits;
}

void BasicSceneBuilder::TransformTimes(Float start, Float end, FileLoc loc) {
    VERIFY_OPTIONS("TransformTimes");
    graphicsState.transformStartTime = start;
    graphicsState.transformEndTime = end;
}

void BasicSceneBuilder::PixelFilter(const std::string &name, ParsedParameterVector params,
                                    FileLoc loc) {
    ParameterDictionary dict(std::move(params), graphicsState.colorSpace);
    VERIFY_OPTIONS("PixelFilter");
    filter = SceneEntity(name, std::move(dict), loc);
}

void BasicSceneBuilder::Film(const std::string &type, ParsedParameterVector params,
                             FileLoc loc) {
    ParameterDictionary dict(std::move(params), graphicsState.colorSpace);
    VERIFY_OPTIONS("Film");
    film = SceneEntity(type, std::move(dict), loc);
}

void BasicSceneBuilder::Accelerator(const std::string &name, ParsedParameterVector params,
                                    FileLoc loc) {
    ParameterDictionary dict(std::move(params), graphicsState.colorSpace);
    VERIFY_OPTIONS("Accelerator");
    accelerator = SceneEntity(name, std::move(dict), loc);
}

void BasicSceneBuilder::Integrator(const std::string &name, ParsedParameterVector params,
                                   FileLoc loc) {
    ParameterDictionary dict(std::move(params), graphicsState.colorSpace);

    VERIFY_OPTIONS("Integrator");
    integrator = SceneEntity(name, std::move(dict), loc);
}

void BasicSceneBuilder::MediumInterface(const std::string &origInsideName,
                                        const std::string &origOutsideName, FileLoc loc) {
    std::string insideName = NormalizeUTF8(origInsideName);
    std::string outsideName = NormalizeUTF8(origOutsideName);

    graphicsState.currentInsideMedium = insideName;
    graphicsState.currentOutsideMedium = outsideName;
}

void BasicSceneBuilder::Texture(const std::string &origName, const std::string &type,
                                const std::string &texname, ParsedParameterVector params,
                                FileLoc loc) {
    std::string name = NormalizeUTF8(origName);
    VERIFY_WORLD("Texture");

    ParameterDictionary dict(std::move(params), graphicsState.textureAttributes,
                             graphicsState.colorSpace);

    if (type != "float" && type != "spectrum") {
        ErrorExitDeferred(
            &loc, "%s: texture type unknown. Must be \"float\" or \"spectrum\".", type);
        return;
    }

    std::set<std::string> &names =
        (type == "float") ? floatTextureNames : spectrumTextureNames;
    if (names.find(name) != names.end()) {
        ErrorExitDeferred(&loc, "Redefining texture \"%s\".", name);
        return;
    }
    names.insert(name);

    if (type == "float")
        scene->AddFloatTexture(
            name, TextureSceneEntity(texname, std::move(dict), loc, RenderFromObject()));
    else
        scene->AddSpectrumTexture(
            name, TextureSceneEntity(texname, std::move(dict), loc, RenderFromObject()));
}

void BasicSceneBuilder::Material(const std::string &name, ParsedParameterVector params,
                                 FileLoc loc) {
    VERIFY_WORLD("Material");

    ParameterDictionary dict(std::move(params), graphicsState.materialAttributes,
                             graphicsState.colorSpace);

    graphicsState.currentMaterialIndex =
        scene->AddMaterial(SceneEntity(name, std::move(dict), loc));
    graphicsState.currentMaterialName.clear();
}

void BasicSceneBuilder::MakeNamedMaterial(const std::string &origName,
                                          ParsedParameterVector params, FileLoc loc) {
    std::string name = NormalizeUTF8(origName);
    VERIFY_WORLD("MakeNamedMaterial");

    ParameterDictionary dict(std::move(params), graphicsState.materialAttributes,
                             graphicsState.colorSpace);

    if (namedMaterialNames.find(name) != namedMaterialNames.end()) {
        ErrorExitDeferred(&loc, "%s: named material redefined.", name);
        return;
    }
    namedMaterialNames.insert(name);

    scene->AddNamedMaterial(name, SceneEntity("", std::move(dict), loc));
}

void BasicSceneBuilder::NamedMaterial(const std::string &origName, FileLoc loc) {
    std::string name = NormalizeUTF8(origName);
    VERIFY_WORLD("NamedMaterial");
    graphicsState.currentMaterialName = name;
    graphicsState.currentMaterialIndex = -1;
}

void BasicSceneBuilder::AreaLightSource(const std::string &name,
                                        ParsedParameterVector params, FileLoc loc) {
    VERIFY_WORLD("AreaLightSource");
    graphicsState.areaLightName = name;
    graphicsState.areaLightParams = ParameterDictionary(
        std::move(params), graphicsState.lightAttributes, graphicsState.colorSpace);
    graphicsState.areaLightLoc = loc;
}


}
