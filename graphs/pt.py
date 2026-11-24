from renderlab import RenderGraph

from modules.post_process import add_post_process

def render_graph_PT(
    *, hdr: bool = True, postProcess: str = "RenderLab", bloom: bool = True
) -> RenderGraph:
    graph = RenderGraph("PT")
    graph.create_pass("pt", "PathTracer", {"maxBounces": 2})
    graph.create_pass("accumulate", "AccumulatePass")
    graph.add_edge("pt.samples", "accumulate.input")
    add_post_process(graph, "accumulate.output", hdr, postProcess, bloom)
    return graph
