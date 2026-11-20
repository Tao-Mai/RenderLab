from modules.editor import add_editor_output
from modules.post_process import add_post_process
from modules.scene import add_shadows, complete_raster_scene, configure_ibl, connect_shadows
from renderlab_graph import RenderGraph


def build_graph(graph: RenderGraph) -> None:
    target = graph.input("presentTarget")
    lights = graph.input("lightBuffer")
    ibl = configure_ibl(graph)
    shadows = add_shadows(graph, lights)
    graph.create_pass("depth", "DepthPrepass")
    depth = "depth.depth"
    graph.create_pass("forward", "ScenePass")
    graph.add_edge(target, "forward.target")
    graph.add_edge(depth, "forward.depth")
    graph.add_edge(lights, "forward.lights")
    connect_shadows(graph, shadows, "forward")
    color = complete_raster_scene(graph, "forward.color", depth, shadows, lights, ibl)
    add_editor_output(graph, add_post_process(graph, color, target), depth)
