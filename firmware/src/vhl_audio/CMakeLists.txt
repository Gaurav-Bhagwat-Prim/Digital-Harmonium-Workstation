# firmware/components/vhl_audio/CMakeLists.txt
if(DEFINED ENV{IDF_PATH})
    idf_component_register(
        SRCS "sample_engine.c" "adsr_envelope.c" "poly_synth.c" "parameter_converter.c" "lfo_engine.c" "biquad_filter.c"
        INCLUDE_DIRS "include"
        REQUIRES vhl_core vhl_storage
    )
else()
    add_library(vhl_audio STATIC sample_engine.c adsr_envelope.c poly_synth.c parameter_converter.c lfo_engine.c biquad_filter.c)
    target_include_directories(vhl_audio PUBLIC include)
    target_link_libraries(vhl_audio PUBLIC vhl_core vhl_storage)
endif()
