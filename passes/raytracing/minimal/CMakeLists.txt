renderlab_add_plugin(MinimalPathTracer MinimalPathTracer.cpp)

renderlab_shader(minimal_path_tracer minimal_path_tracer.slang minimal_path_tracer
    ENTRIES raygenMain missMain visibilityMissMain closestHitMain lightClosestHitMain
    DEFINES ENABLE_CLEARCOAT=0 ENABLE_SHEEN=0)
