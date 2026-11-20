target_sources(RenderLabPasses PRIVATE
    depth/EditorDepthPass.cpp
    light_marker/LightMarkerPass.cpp
    picking/EditorPickingPass.cpp
    picking_copy/EditorPickingCopyPass.cpp
    ui/EditorUiPass.cpp
)

foreach(name IN ITEMS light_marker depth picking)
    renderlab_shader(editor_${name} ${name}/${name}.slang editor_${name})
endforeach()
