from renderlab import RenderGraph

from modules.post_process import add_post_process
from modules.raytracing import add_svgf

def render_graph_ReSTIR(
    *,
    hdr: bool = True,
    svgf: bool = True,
    postProcess: str = "RenderLab",
    bloom: bool = True,
) -> RenderGraph:
    graph = RenderGraph("ReSTIR")

    graph.create_pass(
        "rayTracing", "RayTracingPass",
        {"maxBounces": 3, "restir": {"candidateCount": 32}, "splitLighting": svgf},
    )
    color = add_svgf(graph, "rayTracing") if svgf else "rayTracing.color"
    add_post_process(graph, color, hdr, postProcess, bloom)
    return graph


if globals().get("__name__", "__main__") == "__main__":
    ReSTIR = render_graph_ReSTIR()
    if "m" in globals():
        m.addGraph(ReSTIR)
