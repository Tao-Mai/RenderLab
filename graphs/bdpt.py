from falcor import RenderGraph

from modules.post_process import add_post_process

def render_graph_BDPT(
    *, hdr: bool = True, postProcess: str = "RenderLab", bloom: bool = True
) -> RenderGraph:
    graph = RenderGraph("BDPT")
    graph.create_pass("bdpt", "BDPT", {"maxBounces": 6})
    add_post_process(graph, "bdpt.color", hdr, postProcess, bloom)
    return graph


if globals().get("__name__", "__main__") == "__main__":
    BDPT = render_graph_BDPT()
    if "m" in globals():
        m.addGraph(BDPT)
