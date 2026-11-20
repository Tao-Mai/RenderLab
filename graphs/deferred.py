from modules.editor import add_editor_output
from modules.post_process import add_post_process
from modules.scene import add_shadows, complete_raster_scene, configure_ibl
from renderlab_graph import RenderGraph


def build_graph(graph: RenderGraph) -> None:
    target = graph.input("presentTarget")
    lights = graph.input("lightBuffer")
    ibl = configure_ibl(graph)
    shadow = add_shadows(graph, lights)
    depth = graph.add_pass("DepthPrepass", "depth").depth
    gbuffer = graph.add_pass("GBufferPass", "gbuffer", depth)
    color = graph.add_pass("DeferredLightingPass", "deferred", target, gbuffer, shadow, lights).color
    color = complete_raster_scene(graph, color, depth, shadow, lights, ibl)
    add_editor_output(graph, add_post_process(graph, color, target), depth)
