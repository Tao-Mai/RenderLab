target_sources(RenderLabPasses PRIVATE ShadowPass.cpp)
target_sources(RenderLabPassEditors PRIVATE ShadowPassUi.cpp)

foreach(type IN ITEMS 0 1 2 3)
    set(names shadow shadow.directional shadow.rect_area shadow.spot)
    list(GET names ${type} binary)
    renderlab_shader(shadow shadow.slang "${binary}" DEFINES SHADOW_TYPE=${type})
endforeach()
