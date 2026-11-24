from renderlab import RenderGraph

from modules.post_process import add_post_process

def render_graph_MinimalPathTracer(
    *,
    hdr: bool = True,
    postProcess: str = "RenderLab",
    bloom: bool = True,
) -> RenderGraph:
    graph = RenderGraph("MinimalPathTracer")

    graph.create_pass("pathTracer", "MinimalPathTracer", {"maxBounces": 3})
    add_post_process(graph, "pathTracer.color", hdr, postProcess, bloom)
    return graph


if globals().get("__name__", "__main__") == "__main__":
    MinimalPathTracer = render_graph_MinimalPathTracer()
    if "m" in globals():
        m.addGraph(MinimalPathTracer)
