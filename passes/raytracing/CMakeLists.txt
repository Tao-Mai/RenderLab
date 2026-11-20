target_sources(RenderLabPasses PRIVATE
    blas/BlasBuildPass.cpp
    blit/RayTracingBlitPass.cpp
    offline/OfflinePathTracingPass.cpp
    offline/OfflineScene.cpp
    realtime/scene/RayTracingScenePass.cpp
    realtime/svgf/SVGFFilter.cpp
    realtime/svgf/SVGFReprojection.cpp
    realtime/trace/RayTracingPass.cpp
    tlas/TlasBuildPass.cpp
)

foreach(svgf IN ITEMS 0 1)
    set(binary raytracing)
    if(svgf)
        string(APPEND binary ".svgf")
    endif()
    renderlab_shader(raytracing realtime/trace/restir/path_tracing.slang "${binary}"
        ENTRIES raygenMain temporalMain spatialMain missMain visibilityMissMain closestHitMain lightClosestHitMain
        DEFINES ENABLE_CLEARCOAT=0 ENABLE_SHEEN=0 SVGF=${svgf})
    set(binary raytracing_simple)
    if(svgf)
        string(APPEND binary ".svgf")
    endif()
    renderlab_shader(raytracing_simple realtime/trace/simple/path_tracing.slang "${binary}"
        ENTRIES raygenMain missMain visibilityMissMain closestHitMain lightClosestHitMain
        DEFINES ENABLE_CLEARCOAT=0 ENABLE_SHEEN=0 SVGF=${svgf})
endforeach()
renderlab_shader(prepare_emissive_triangles realtime/scene/prepare_lights.slang prepare_emissive_triangles
    ENTRIES computeMain)
renderlab_shader(offline_pathtracing offline/main.slang offline_pathtracing
    DEFINES BDPT=0 ENTRIES raygenMain missMain visibilityMissMain closestHitMain)
foreach(depth IN ITEMS 4 8 16 32 64)
    renderlab_shader(offline_bdpt offline/main.slang "offline_bdpt.depth${depth}"
        DEFINES BDPT=1 BDPT_MAX_DEPTH=${depth} ENTRIES raygenMain missMain visibilityMissMain closestHitMain)
endforeach()
foreach(bdpt IN ITEMS 0 1)
    renderlab_shader(offline_resolve offline/resolve.slang "offline_resolve.${bdpt}"
        DEFINES BDPT=${bdpt} ENTRIES computeMain)
endforeach()
renderlab_shader(svgf_reprojection realtime/svgf/svgf_reprojection.slang svgf_reprojection
    ENTRIES computeMain)
foreach(stage IN ITEMS 0 1 2)
    set(binary svgf_filter)
    if(stage EQUAL 0)
        string(APPEND binary ".first")
    elseif(stage EQUAL 2)
        string(APPEND binary ".final")
    endif()
    renderlab_shader(svgf_filter realtime/svgf/svgf_filter.slang "${binary}" ENTRIES computeMain
        DEFINES SVGF_STAGE=${stage})
endforeach()
