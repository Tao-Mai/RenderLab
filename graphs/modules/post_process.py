from renderlab_graph import RenderGraph, ResourceReference


def add_post_process(
    graph: RenderGraph, sceneColor: ResourceReference, target: ResourceReference
) -> ResourceReference:
    if not graph.param("hdr", True, label="Enable HDR"):
        return graph.add_pass("PresentColorPass", "presentColor", sceneColor, target).color

    if graph.param("postProcess", "RenderLab") == "Falcor":
        return graph.add_pass(
            "FalcorPostProcess", "falcor", sceneColor, target,
            properties={"toneMapper": {"mode": "Aces"}}, ui=True,
        ).color

    exposure = graph.add_pass(
        "ExposurePass", "exposure", sceneColor, properties={"mode": "Manual", "ev100": 0.0}, ui=True
    ).state
    color = sceneColor
    if graph.param("bloom", True, label="Enable bloom"):
        color = graph.add_pass(
            "Bloom", "bloom", color, properties={"levels": 5, "intensity": 0.04}, ui=True
        ).color
    return graph.add_pass(
        "ToneMappingPass", "toneMapping", color, target, exposure, properties={"mode": "Neutral"}, ui=True
    ).color
