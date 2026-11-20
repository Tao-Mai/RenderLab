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
    graph.create_pass("gbuffer", "GBufferPass")
    graph.add_edge(depth, "gbuffer.depth")
    graph.create_pass("deferred", "DeferredLightingPass")
    graph.add_edge(target, "deferred.target")
    graph.add_edge(depth, "deferred.depth")
    graph.add_edge(lights, "deferred.lights")
    for port in (
        "diffuseOcclusion", "normalRoughness", "emissiveSpecularF90", "specularF0DiffuseFresnel", "sheenColorRoughness"
    ):
        graph.add_edge(f"gbuffer.{port}", f"deferred.{port}")
    connect_shadows(graph, shadows, "deferred")
    color = complete_raster_scene(graph, "deferred.color", depth, shadows, lights, ibl)
    add_editor_output(graph, add_post_process(graph, color, target), depth)
