target_sources(RenderLabPasses PRIVATE
    common/SceneDraw.cpp
    deferred/DeferredLightingPass.cpp
    depth/DepthPrepass.cpp
    forward/ScenePass.cpp
    gbuffer/GBufferPass.cpp
    shadow/ShadowPass.cpp
    skybox/SkyboxPass.cpp
    transparent/TransparentPass.cpp
)

foreach(enable_clearcoat IN ITEMS 0 1)
    foreach(enable_sheen IN ITEMS 0 1)
        foreach(enable_ibl IN ITEMS 1 0)
            set(binary forward)
            if(enable_clearcoat)
                string(APPEND binary ".clearcoat")
            endif()
            if(enable_sheen)
                string(APPEND binary ".sheen")
            endif()
            if(NOT enable_ibl)
                string(APPEND binary ".no_ibl")
            endif()
            renderlab_shader(forward forward/forward.slang "${binary}"
                DEFINES ENABLE_IBL=${enable_ibl} ENABLE_CLEARCOAT=${enable_clearcoat} ENABLE_SHEEN=${enable_sheen})
        endforeach()
    endforeach()
endforeach()
renderlab_shader(gbuffer gbuffer/gbuffer.slang gbuffer DEFINES ENABLE_CLEARCOAT=0 ENABLE_SHEEN=1)
foreach(enable_ibl IN ITEMS 1 0)
    set(binary deferred_lighting)
    if(NOT enable_ibl)
        string(APPEND binary ".no_ibl")
    endif()
    renderlab_shader(deferred_lighting deferred/deferred_lighting.slang "${binary}"
        DEFINES ENABLE_IBL=${enable_ibl} ENABLE_CLEARCOAT=0 ENABLE_SHEEN=1)
endforeach()
renderlab_shader(depth depth/depth.slang depth DEFINES ENABLE_CLEARCOAT=0 ENABLE_SHEEN=0)
renderlab_shader(skybox skybox/skybox.slang skybox)
foreach(type IN ITEMS 0 1 2 3)
    set(names shadow shadow.directional shadow.rect_area shadow.spot)
    list(GET names ${type} binary)
    renderlab_shader(shadow shadow/shadow.slang "${binary}" DEFINES SHADOW_TYPE=${type})
endforeach()
