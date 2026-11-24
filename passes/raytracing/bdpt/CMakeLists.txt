renderlab_add_plugin(BDPT BDPT.cpp)

renderlab_runtime_shader(bdpt main.slang ENTRIES raygenMain missMain visibilityMissMain closestHitMain lightClosestHitMain)
renderlab_shader(bdpt_resolve resolve.slang bdpt_resolve ENTRIES computeMain)
