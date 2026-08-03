# firmware/components/vhl_storage/CMakeLists.txt
if(DEFINED ENV{IDF_PATH})
    idf_component_register(
        SRCS "vhl_loader.c" "recorder_engine.c"
        INCLUDE_DIRS "include"
        REQUIRES vhl_core
    )
else()
    add_library(vhl_storage STATIC vhl_loader.c recorder_engine.c)
    target_include_directories(vhl_storage PUBLIC include)
    target_link_libraries(vhl_storage PUBLIC vhl_core)
endif()
