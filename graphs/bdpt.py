from renderlab import RenderGraph

from modules.post_process import add_post_process

def render_graph_BDPT(
    *, hdr: bool = True, postProcess: str = "RenderLab", bloom: bool = True
) -> RenderGraph:
    graph = RenderGraph("BDPT")
    graph.create_pass("bdpt", "BDPT", {"maxBounces": 6})
    add_post_process(graph, "bdpt.color", hdr, postProcess, bloom)
    return graph
