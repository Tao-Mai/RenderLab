renderlab_add_plugin(DeferredLightingPass DeferredLightingPass.cpp)

foreach(enable_ibl IN ITEMS 1 0)
    set(binary deferred_lighting)
    if(NOT enable_ibl)
        string(APPEND binary ".no_ibl")
    endif()
    renderlab_shader(deferred_lighting deferred_lighting.slang "${binary}"
        DEFINES ENABLE_IBL=${enable_ibl} ENABLE_CLEARCOAT=0 ENABLE_SHEEN=1)
endforeach()
