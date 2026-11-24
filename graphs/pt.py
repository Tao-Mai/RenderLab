from renderlab import RenderGraph

from modules.post_process import add_post_process

def render_graph_PT(
    *, hdr: bool = True, postProcess: str = "RenderLab", bloom: bool = True
) -> RenderGraph:
    graph = RenderGraph("PT")
    graph.create_pass("pt", "PathTracer", {"maxBounces": 2})
    add_post_process(graph, "pt.color", hdr, postProcess, bloom)
    return graph


if globals().get("__name__", "__main__") == "__main__":
    PT = render_graph_PT()
    if "m" in globals():
        m.addGraph(PT)
