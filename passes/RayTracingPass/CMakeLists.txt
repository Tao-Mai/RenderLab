renderlab_add_plugin(RayTracingPass RayTracingPass.cpp RayTracingPassUi.cpp)

foreach(svgf IN ITEMS 0 1)
    set(suffix "")
    if(svgf)
        set(suffix ".svgf")
    endif()
    renderlab_shader(raytracing restir_path_tracer.slang "raytracing${suffix}"
        ENTRIES raygenMain temporalMain spatialMain missMain visibilityMissMain closestHitMain lightClosestHitMain
        DEFINES ENABLE_CLEARCOAT=0 ENABLE_SHEEN=0 SVGF=${svgf})
    renderlab_shader(minimal_path_tracer minimal_path_tracer.slang "minimal_path_tracer${suffix}"
        ENTRIES raygenMain missMain visibilityMissMain closestHitMain lightClosestHitMain
        DEFINES ENABLE_CLEARCOAT=0 ENABLE_SHEEN=0 SVGF=${svgf})
endforeach()
