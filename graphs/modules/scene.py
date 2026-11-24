from falcor import RenderGraph


def add_shadows(graph: RenderGraph, enabled: bool) -> str:
    shadow = "shadow"
    graph.create_pass(shadow, "ShadowPass", {"enabled": enabled, "cascade": {"count": 4}})
    return shadow


def connect_shadow_resources(graph: RenderGraph, shadow: str, consumer: str) -> None:
    for field in (
        "pointShadowMap", "pointShadowData",
        "spotShadowMap", "spotShadowData",
        "rectAreaShadowMap", "rectAreaShadowData",
        "cascadeShadowMap", "directionalShadowData",
    ):
        graph.add_edge(f"{shadow}.{field}", f"{consumer}.{field}")


def complete_raster_scene(
    graph: RenderGraph,
    color: str,
    depth: str,
    shadow: str,
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
    connect_shadow_resources(graph, shadow, "transparent")
    return "transparent.color"
