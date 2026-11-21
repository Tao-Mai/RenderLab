from falcor import RenderGraph


def add_svgf(graph: RenderGraph, trace: str) -> str:
    graph.create_pass("svgf", "SVGFPass", {"historyLength": 32, "Alpha": 0.1})
    for source, target in (
        ("color", "Color"),
        ("depth", "LinearZ"),
        ("worldNormal", "WorldNormal"),
        ("emission", "Emission"),
        ("albedo", "Albedo"),
    ):
        graph.add_edge(f"{trace}.{source}", f"svgf.{target}")
    return "svgf.Filtered image"
