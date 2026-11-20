target_sources(RenderLabPasses PRIVATE
    bloom/Bloom.cpp
    exposure/ExposurePass.cpp
    falcor/FalcorPostProcess.cpp
    tone_mapping/ToneMappingPass.cpp
)

renderlab_shader(tonemapping tone_mapping/tonemapping.slang tonemapping)
foreach(stage IN ITEMS 0 1 2)
    set(binary falcor_simple_post_fx)
    if(NOT stage EQUAL 0)
        string(APPEND binary ".${stage}")
    endif()
    renderlab_shader(falcor_simple_post_fx falcor/simple_post_fx.slang "${binary}"
        ENTRIES computeMain DEFINES FALCOR_POST_FX_STAGE=${stage})
endforeach()
foreach(stage IN ITEMS 0 1)
    set(binary falcor_luminance)
    if(NOT stage EQUAL 0)
        string(APPEND binary ".${stage}")
    endif()
    renderlab_shader(falcor_luminance falcor/luminance.slang "${binary}"
        DEFINES FALCOR_LUMINANCE_STAGE=${stage})
endforeach()
renderlab_shader(falcor_tonemapping falcor/tonemapping.slang falcor_tonemapping)
renderlab_shader(falcor_present falcor/present.slang falcor_present)
foreach(stage IN ITEMS 0 1)
    set(exposure_binaries exposure.histogram exposure.resolve)
    list(GET exposure_binaries ${stage} binary)
    renderlab_shader(exposure exposure/exposure.slang "${binary}" ENTRIES computeMain
        DEFINES EXPOSURE_STAGE=${stage})
endforeach()
foreach(stage IN ITEMS 0 1 2 3)
    set(binary bloom)
    if(NOT stage EQUAL 0)
        string(APPEND binary ".${stage}")
    endif()
    renderlab_shader(bloom bloom/bloom.slang "${binary}" DEFINES BLOOM_STAGE=${stage})
endforeach()
