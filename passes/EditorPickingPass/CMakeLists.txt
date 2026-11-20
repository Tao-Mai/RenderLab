target_sources(RenderLabPasses PRIVATE EditorPickingPass.cpp)

renderlab_shader(editor_picking picking.slang editor_picking)
