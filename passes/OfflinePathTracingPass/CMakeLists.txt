renderlab_add_plugin(OfflinePathTracingPass OfflinePathTracingPass.cpp OfflinePathTracingPassUi.cpp)

renderlab_shader(offline_pathtracing main.slang offline_pathtracing
    DEFINES BDPT=0 ENTRIES raygenMain missMain visibilityMissMain closestHitMain)
foreach(depth IN ITEMS 4 8 16 32 64)
    renderlab_shader(offline_bdpt main.slang "offline_bdpt.depth${depth}"
        DEFINES BDPT=1 BDPT_MAX_DEPTH=${depth} ENTRIES raygenMain missMain visibilityMissMain closestHitMain)
endforeach()
foreach(bdpt IN ITEMS 0 1)
    renderlab_shader(offline_resolve resolve.slang "offline_resolve.${bdpt}" DEFINES BDPT=${bdpt} ENTRIES computeMain)
endforeach()
