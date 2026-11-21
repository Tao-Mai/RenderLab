from falcor import RenderGraph


def add_picking(graph: RenderGraph, depth: str, color: str) -> None:
    graph.create_pass("picking", "EditorPickingPass")
    graph.add_edge(depth, "picking.depth")
    graph.add_edge(color.rsplit(".", 1)[0], "picking")
    graph.mark_output("picking.pickingImage")
