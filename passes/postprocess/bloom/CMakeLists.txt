renderlab_add_plugin(Bloom Bloom.cpp BloomUi.cpp)

foreach(stage IN ITEMS 0 1 2 3)
    set(binary bloom)
    if(NOT stage EQUAL 0)
        string(APPEND binary ".${stage}")
    endif()
    renderlab_shader(bloom bloom.slang "${binary}" DEFINES BLOOM_STAGE=${stage})
endforeach()
