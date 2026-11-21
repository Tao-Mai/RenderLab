renderlab_add_plugin(RayTracingPass RayTracingPass.cpp RayTracingPassUi.cpp RestirResources.cpp)

foreach(svgf IN ITEMS 0 1)
    set(suffix "")
    if(svgf)
        set(suffix ".svgf")
    endif()
    renderlab_shader(raytracing restir_path_tracer.slang "raytracing${suffix}"
        ENTRIES raygenMain temporalMain spatialMain missMain visibilityMissMain closestHitMain lightClosestHitMain
        DEFINES ENABLE_CLEARCOAT=0 ENABLE_SHEEN=0 SVGF=${svgf})
endforeach()
