renderlab_add_plugin(PathTracer PathTracer.cpp)

renderlab_runtime_shader(pt main.slang ENTRIES raygenMain missMain visibilityMissMain closestHitMain lightClosestHitMain)
renderlab_shader(pt_resolve resolve.slang pt_resolve ENTRIES computeMain)
