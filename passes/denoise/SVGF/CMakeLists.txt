renderlab_add_plugin(SVGFPass SVGFPass.cpp SVGFFilter.cpp SVGFReprojection.cpp)

renderlab_shader(svgf_reprojection svgf_reprojection.slang svgf_reprojection ENTRIES computeMain)
foreach(stage IN ITEMS 0 1 2)
    set(binary svgf_filter)
    if(stage EQUAL 0)
        string(APPEND binary ".first")
    elseif(stage EQUAL 2)
        string(APPEND binary ".final")
    endif()
    renderlab_shader(svgf_filter svgf_filter.slang "${binary}"
        ENTRIES computeMain DEFINES SVGF_STAGE=${stage})
endforeach()
