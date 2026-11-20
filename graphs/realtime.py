from modules.editor import add_editor_output
from modules.post_process import add_post_process
from modules.scene import configure_ibl
from renderlab_graph import RenderGraph


def build_graph(graph: RenderGraph) -> None:
    target = graph.input("presentTarget")
    lights = graph.input("lightBuffer")
    configure_ibl(graph)

    geometry = graph.add_pass("RayTracingScenePass", "rayTracingScene")
    traced = graph.add_pass(
        "RayTracingPass", "rayTracing", geometry, lights,
        properties={"algorithm": "ReSTIR", "maxBounces": 3, "restir": {"candidateCount": 32}},
        ui=True,
    )
    color = traced.rayImage
    if graph.param("svgf", True, label="Enable SVGF"):
        color = graph.add_pass(
            "SVGF", "svgf", traced, properties={"historyLength": 32, "emaAlpha": 0.1}, ui=True
        ).color

    add_editor_output(graph, add_post_process(graph, color, target))
