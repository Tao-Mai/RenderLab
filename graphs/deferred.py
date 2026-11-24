from falcor import RenderGraph

from modules.editor import add_picking
from modules.post_process import add_post_process
from modules.scene import add_shadows, complete_raster_scene, connect_shadow_resources

def render_graph_Deferred(
    *,
    hdr: bool = True,
    ibl: bool = True,
    ambientCoefficient: float = 0.1,
    shadows: bool = True,
    postProcess: str = "RenderLab",
    bloom: bool = True,
) -> RenderGraph:
    graph = RenderGraph("Deferred")
    shadow = add_shadows(graph, shadows)
    graph.create_pass("depth", "DepthPrepass")
    depth = "depth.depth"
    graph.create_pass("gbuffer", "GBufferPass")
    graph.add_edge(depth, "gbuffer.depth")
    graph.create_pass("deferred", "DeferredLightingPass", {"hdr": hdr})
    graph.add_edge(depth, "deferred.depth")
    for port in (
        "diffuseOcclusion", "normalRoughness", "emissiveSpecularF90", "specularF0DiffuseFresnel", "sheenColorRoughness"
    ):
        graph.add_edge(f"gbuffer.{port}", f"deferred.{port}")
    connect_shadow_resources(graph, shadow, "deferred")
    sceneColor = "deferred.color"
    color = complete_raster_scene(graph, sceneColor, depth, shadow, ibl)
    color = add_post_process(graph, color, hdr, postProcess, bloom)
    add_picking(graph, depth, color)
    return graph


if globals().get("__name__", "__main__") == "__main__":
    Deferred = render_graph_Deferred()
    if "m" in globals():
        m.addGraph(Deferred)
