from renderlab import RenderGraph

from modules.editor import add_picking
from modules.post_process import add_post_process
from modules.scene import add_shadows, complete_raster_scene, connect_shadow_resources

def render_graph_Forward(
    *,
    hdr: bool = True,
    ambientCoefficient: float = 0.1,
    shadows: bool = True,
    postProcess: str = "RenderLab",
    bloom: bool = True,
) -> RenderGraph:
    graph = RenderGraph("Forward")
    shadow = add_shadows(graph, shadows)
    graph.create_pass("depth", "DepthPrepass")
    depth = "depth.depth"
    graph.create_pass("forward", "ScenePass", {"hdr": hdr, "ambientCoefficient": ambientCoefficient})
    graph.add_edge(depth, "forward.depth")
    connect_shadow_resources(graph, shadow, "forward")
    sceneColor = "forward.color"
    color = complete_raster_scene(graph, sceneColor, depth, shadow, ambientCoefficient, clearcoat=True)
    color = add_post_process(graph, color, hdr, postProcess, bloom)
    add_picking(graph, depth, color)
    return graph
