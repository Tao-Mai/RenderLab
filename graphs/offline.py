from modules.editor import add_editor_output
from modules.post_process import add_post_process
from renderlab_graph import RenderGraph


def build_graph(graph: RenderGraph) -> None:
    target = graph.input("presentTarget")
    graph.create_pass(
        "offline", "OfflinePathTracingPass",
        properties={"algorithm": "PT", "pt": {"maxBounces": 2}, "bdpt": {"maxBounces": 6}},
        ui=True,
    )
    add_editor_output(graph, add_post_process(graph, "offline.rayImage", target))
