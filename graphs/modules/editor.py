from renderlab_graph import RenderGraph, ResourceReference


def add_editor_output(graph: RenderGraph, color: ResourceReference, depth: ResourceReference | None = None) -> None:
    if depth is not None:
        picking = graph.add_pass("EditorPickingPass", "picking", depth).pickingImage
        readback = graph.add_pass("EditorPickingCopyPass", "pickingCopy", picking).readback
        graph.mark_output("pickingReadback", readback)

    finalColor = graph.add_pass("EditorUiPass", "editorUi", color).color
    graph.mark_output("color", finalColor)
