from modules.editor import add_editor_output
from modules.post_process import add_post_process
from modules.raytracing import add_svgf
from modules.scene import configure_ibl
from renderlab_graph import RenderGraph


def build_graph(graph: RenderGraph) -> None:
    target = graph.input("presentTarget")
    lights = graph.input("lightBuffer")
    configure_ibl(graph)

    graph.create_pass(
        "rayTracing", "RayTracingPass",
        properties={"maxBounces": 3, "restir": {"candidateCount": 32}},
        ui=True,
    )
    graph.add_edge(lights, "rayTracing.lights")
    color = add_svgf(graph, "rayTracing", enabled_by_default=True)
    add_editor_output(graph, add_post_process(graph, color, target))
