# firmware/components/vhl_feature/CMakeLists.txt
if(DEFINED ENV{IDF_PATH})
    idf_component_register(
        SRCS "tabla_engine.c" "tanpura_engine.c" "metronome_engine.c"
        INCLUDE_DIRS "include"
        REQUIRES vhl_core
    )
else()
    add_library(vhl_feature STATIC tabla_engine.c tanpura_engine.c metronome_engine.c)
    target_include_directories(vhl_feature PUBLIC include)
    target_link_libraries(vhl_feature PUBLIC vhl_core)
endif()
