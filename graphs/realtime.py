from modules.editor import add_editor_output
from modules.post_process import add_post_process
from modules.scene import configure_ibl
from renderlab_graph import RenderGraph


def build_graph(graph: RenderGraph) -> None:
    target = graph.input("presentTarget")
    lights = graph.input("lightBuffer")
    configure_ibl(graph)

    graph.create_pass("rayTracingScene", "RayTracingScenePass")
    graph.create_pass(
        "rayTracing", "RayTracingPass",
        properties={"algorithm": "ReSTIR", "maxBounces": 3, "restir": {"candidateCount": 32}},
        ui=True,
    )
    graph.add_edge("rayTracingScene.objects", "rayTracing.objects")
    graph.add_edge("rayTracingScene.triangles", "rayTracing.triangles")
    graph.add_edge(lights, "rayTracing.lights")
    color = "rayTracing.rayImage"
    if graph.param("svgf", True, label="Enable SVGF"):
        graph.create_pass("svgf", "SVGF", {"historyLength": 32, "emaAlpha": 0.1}, ui=True)
        for port in ("depth", "worldNormal", "direct", "indirect", "emission", "albedo"):
            graph.add_edge(f"rayTracing.{port}", f"svgf.{port}")
        color = "svgf.color"

    add_editor_output(graph, add_post_process(graph, color, target))
