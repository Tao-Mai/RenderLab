from falcor import RenderGraph


def add_post_process(
    graph: RenderGraph,
    sceneColor: str,
    hdr: bool,
    postProcess: str,
    bloom: bool,
) -> str:
    if not hdr:
        graph.mark_output(sceneColor)
        return sceneColor

    if postProcess == "Falcor":
        graph.create_pass("falcor", "FalcorPostProcess", {"toneMapper": {"mode": "Aces"}})
        graph.add_edge(sceneColor, "falcor.source")
        color = "falcor.color"
        graph.mark_output(color)
        return color

    graph.create_pass("exposure", "ExposurePass", {"mode": "Manual", "ev100": 0.0})
    graph.add_edge(sceneColor, "exposure.hdrColor")
    exposure = "exposure.state"
    color = sceneColor
    if bloom:
        graph.create_pass("bloom", "Bloom", {"levels": 5, "intensity": 0.04})
        graph.add_edge(color, "bloom.hdrColor")
        color = "bloom.color"
    graph.create_pass("toneMapping", "ToneMappingPass", {"mode": "Neutral"})
    graph.add_edge(color, "toneMapping.hdrColor")
    graph.add_edge(exposure, "toneMapping.exposure")
    color = "toneMapping.color"
    graph.mark_output(color)
    graph.mark_output(exposure)
    return color
