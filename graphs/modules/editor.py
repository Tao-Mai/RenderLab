from renderlab_graph import RenderGraph


def add_editor_output(graph: RenderGraph, color: str, depth: str | None = None) -> None:
    if depth is not None:
        graph.create_pass("picking", "EditorPickingPass")
        graph.create_pass("pickingCopy", "EditorPickingCopyPass")
        graph.add_edge(depth, "picking.depth")
        graph.add_edge("picking.pickingImage", "pickingCopy.pickingImage")
        graph.mark_output("pickingCopy.readback", name="pickingReadback")

    graph.create_pass("editorUi", "EditorUiPass")
    graph.add_edge(color, "editorUi.color")
    graph.mark_output("editorUi.color", name="color")
