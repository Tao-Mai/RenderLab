renderlab_add_plugin(BDPT BDPT.cpp)

foreach(depth IN ITEMS 4 8 16 32 64)
    renderlab_shader(bdpt main.slang "bdpt.depth${depth}"
        DEFINES BDPT_MAX_DEPTH=${depth} ENTRIES raygenMain missMain visibilityMissMain closestHitMain)
endforeach()
renderlab_shader(bdpt_resolve resolve.slang bdpt_resolve ENTRIES computeMain)
