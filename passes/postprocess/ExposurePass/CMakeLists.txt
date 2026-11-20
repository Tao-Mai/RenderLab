renderlab_add_plugin(ExposurePass ExposurePass.cpp ExposurePassUi.cpp)

foreach(stage IN ITEMS 0 1)
    set(exposure_binaries exposure.histogram exposure.resolve)
    list(GET exposure_binaries ${stage} binary)
    renderlab_shader(exposure exposure.slang "${binary}" ENTRIES computeMain
        DEFINES EXPOSURE_STAGE=${stage})
endforeach()
