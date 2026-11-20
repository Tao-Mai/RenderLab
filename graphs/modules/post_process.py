from renderlab_graph import RenderGraph


def add_post_process(graph: RenderGraph, sceneColor: str, target: str) -> str:
    if not graph.param("hdr", True, label="Enable HDR"):
        graph.create_pass("presentColor", "PresentColorPass")
        graph.add_edge(sceneColor, "presentColor.source")
        graph.add_edge(target, "presentColor.target")
        return "presentColor.color"

    if graph.param("postProcess", "RenderLab") == "Falcor":
        graph.create_pass("falcor", "FalcorPostProcess", {"toneMapper": {"mode": "Aces"}}, ui=True)
        graph.add_edge(sceneColor, "falcor.source")
        graph.add_edge(target, "falcor.target")
        return "falcor.color"

    graph.create_pass("exposure", "ExposurePass", {"mode": "Manual", "ev100": 0.0}, ui=True)
    graph.add_edge(sceneColor, "exposure.hdrColor")
    color = sceneColor
    if graph.param("bloom", True, label="Enable bloom"):
        graph.create_pass("bloom", "Bloom", {"levels": 5, "intensity": 0.04}, ui=True)
        graph.add_edge(color, "bloom.hdrColor")
        color = "bloom.color"
    graph.create_pass("toneMapping", "ToneMappingPass", {"mode": "Neutral"}, ui=True)
    graph.add_edge(color, "toneMapping.hdrColor")
    graph.add_edge(target, "toneMapping.target")
    graph.add_edge("exposure.state", "toneMapping.exposure")
    return "toneMapping.color"
