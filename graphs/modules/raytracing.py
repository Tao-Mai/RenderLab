from renderlab_graph import RenderGraph


def add_svgf(graph: RenderGraph, trace: str, *, enabled_by_default: bool) -> str:
    if not graph.param("svgf", enabled_by_default, label="Enable SVGF"):
        return f"{trace}.rayImage"

    graph.create_pass("svgf", "SVGF", {"historyLength": 32, "emaAlpha": 0.1}, ui=True)
    for port in ("depth", "worldNormal", "direct", "indirect", "emission", "albedo"):
        graph.add_edge(f"{trace}.{port}", f"svgf.{port}")
    return "svgf.color"
