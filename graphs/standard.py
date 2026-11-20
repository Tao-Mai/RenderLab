def raster_scene(graph, path, feature, target, lights):
    shadow = graph.input("emptyShadow")
    if feature["shadow"]["enable"]:
        shadow = graph.add_pass("ShadowPass", "shadow", lights)

    depth = graph.add_pass("DepthPrepass", "depth").depth
    if path == "Deferred":
        gbuffer = graph.add_pass("GBufferPass", "gbuffer", depth)
        scene = graph.add_pass("DeferredLightingPass", "deferred", target, gbuffer, shadow, lights)
    else:
        scene = graph.add_pass("ScenePass", "forward", target, depth, shadow, lights)

    color = scene.color
    if feature["ibl"]:
        color = graph.add_pass("SkyboxPass", "skybox", color, scene.depth).color
    color = graph.add_pass("TransparentPass", "transparent", color, scene.depth, scene.shadow, lights).color
    graph.mark_output("shadow", scene.shadow)
    return color, scene.depth


def ray_traced_scene(graph, path, feature, lights):
    blas = graph.add_pass("BlasBuildPass", "blas")
    tlas = graph.add_pass("TlasBuildPass", "tlas", blas)
    if path == "OfflinePathTracing":
        return graph.add_pass("OfflinePathTracingPass", "offline", tlas).rayImage

    geometry = graph.add_pass("RayTracingScenePass", "rayTracingScene")
    traced = graph.add_pass("RayTracingPass", "rayTracing", tlas, geometry, lights)
    if feature["rayTracing"]["svgf"]:
        return graph.add_pass("SVGF", "svgf", traced).color
    return traced.rayImage


def build_graph(graph, config):
    path = config["renderPath"]
    feature = config["feature"]
    target = graph.input("presentTarget")
    lights = graph.input("lightBuffer")
    raster = path in ("Forward", "Deferred")
    if raster:
        color, depth = raster_scene(graph, path, feature, target, lights)
    else:
        color = ray_traced_scene(graph, path, feature, lights)
        depth = None

    hdr = feature["hdr"]
    falcor = feature["postProcess"] == "Falcor"
    if hdr and not falcor:
        exposure = graph.add_pass("ExposurePass", "exposure", color).state

    if depth is None:
        depth = graph.add_pass("EditorDepthPass", "editorDepth").depth
    markers = graph.add_pass("LightMarkerPass", "lightMarkers", color, depth)
    color, depth = markers.color, markers.depth

    if hdr and falcor:
        color = graph.add_pass("FalcorPostProcess", "falcor", color, target).color
    elif hdr:
        if feature["bloom"]["enable"]:
            color = graph.add_pass("Bloom", "bloom", color).color
        color = graph.add_pass("ToneMappingPass", "toneMapping", color, target, exposure).color
    elif not raster:
        color = graph.add_pass("RayTracingBlitPass", "rayTracingBlit", color, target).color

    picking = graph.add_pass("EditorPickingPass", "picking", depth).pickingImage
    readback = graph.add_pass("EditorPickingCopyPass", "pickingCopy", picking).readback
    finalColor = graph.add_pass("EditorUiPass", "editorUi", color).color
    graph.mark_output("color", finalColor)
    graph.mark_output("pickingReadback", readback)
