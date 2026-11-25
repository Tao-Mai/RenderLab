from renderlab import RenderGraph

from modules.post_process import add_post_process

def render_graph_MinimalPathTracer(
    *,
    hdr: bool = True,
    postProcess: str = "RenderLab",
    bloom: bool = True,
) -> RenderGraph:
    graph = RenderGraph("MinimalPathTracer")

    graph.create_pass("pathTracer", "MinimalPathTracer")
    add_post_process(graph, "pathTracer.color", hdr, postProcess, bloom)
    return graph
