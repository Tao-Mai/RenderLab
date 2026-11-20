from renderlab_graph import RenderGraph, ResourceReference


def configure_ibl(graph: RenderGraph) -> bool:
    graph.param("ambientCoefficient", 0.1, ui=False)
    return graph.param("ibl", True, label="IBL")


def add_shadows(graph: RenderGraph, lights: ResourceReference) -> ResourceReference:
    if graph.param("shadows", True, label="Enable shadows"):
        return graph.add_pass(
            "ShadowPass", "shadow", lights, properties={"cascade": {"count": 4}}, ui=True
        )
    return graph.input("emptyShadow")


def complete_raster_scene(
    graph: RenderGraph,
    color: ResourceReference,
    depth: ResourceReference,
    shadow: ResourceReference,
    lights: ResourceReference,
    ibl: bool,
) -> ResourceReference:
    if ibl:
        color = graph.add_pass("SkyboxPass", "skybox", color, depth).color
    color = graph.add_pass("TransparentPass", "transparent", color, depth, shadow, lights).color
    graph.mark_output("shadow", shadow)
    return color
