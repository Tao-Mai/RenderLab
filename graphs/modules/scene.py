from falcor import RenderGraph


def add_shadows(graph: RenderGraph, enabled: bool) -> str:
    shadow = "shadow"
    graph.create_pass(shadow, "ShadowPass", {"enabled": enabled, "cascade": {"count": 4}})
    return shadow


def complete_raster_scene(
    graph: RenderGraph,
    color: str,
    depth: str,
    ibl: bool,
) -> str:
    if ibl:
        graph.create_pass("skybox", "SkyboxPass")
        graph.add_edge(color, "skybox.color")
        graph.add_edge(depth, "skybox.depth")
        color = "skybox.color"
    graph.create_pass("transparent", "TransparentPass")
    graph.add_edge(color, "transparent.color")
    graph.add_edge(depth, "transparent.depth")
    return "transparent.color"
