# firmware/components/vhl_audio/CMakeLists.txt
if(DEFINED ENV{IDF_PATH})
    idf_component_register(
        SRCS "sample_engine.c" "adsr_envelope.c" "poly_synth.c"
        INCLUDE_DIRS "include"
        REQUIRES vhl_core
    )
else()
    add_library(vhl_audio STATIC sample_engine.c adsr_envelope.c poly_synth.c)
    target_include_directories(vhl_audio PUBLIC include)
    target_link_libraries(vhl_audio PUBLIC vhl_core)
endif()
