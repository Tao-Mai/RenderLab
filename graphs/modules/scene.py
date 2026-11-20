from collections.abc import Mapping

from renderlab_graph import RenderGraph


SHADOW_PORTS = (
    "pointLightShadowMap",
    "spotLightShadowMap",
    "rectAreaLightShadowMap",
    "cascadeShadowMap",
    "pointShadowBuffer",
    "spotShadowBuffer",
    "rectAreaShadowBuffer",
    "directionalShadowBuffer",
)


def configure_ibl(graph: RenderGraph) -> bool:
    graph.param("ambientCoefficient", 0.1, ui=False)
    return graph.param("ibl", True, label="IBL")


def add_shadows(graph: RenderGraph, lights: str) -> dict[str, str]:
    enabled = graph.param("shadows", True, label="Enable shadows")
    if enabled:
        graph.create_pass("shadow", "ShadowPass", {"cascade": {"count": 4}}, ui=True)
        graph.add_edge(lights, "shadow.lights")
    shadows = {
        port: f"shadow.{port}" if enabled else graph.input(f"emptyShadow_{port}")
        for port in SHADOW_PORTS
    }
    for port, source in shadows.items():
        graph.mark_output(source, name=port)
    return shadows


def connect_shadows(graph: RenderGraph, shadows: Mapping[str, str], consumer: str) -> None:
    for port, source in shadows.items():
        graph.add_edge(source, f"{consumer}.{port}")


def complete_raster_scene(
    graph: RenderGraph,
    color: str,
    depth: str,
    shadows: Mapping[str, str],
    lights: str,
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
    graph.add_edge(lights, "transparent.lights")
    connect_shadows(graph, shadows, "transparent")
    return "transparent.color"
