# firmware/components/vhl_input/CMakeLists.txt
if(DEFINED ENV{IDF_PATH})
    idf_component_register(
        SRCS "button_manager.c" "slider_filter.c"
        INCLUDE_DIRS "include"
        REQUIRES vhl_core vhl_hal vhl_runtime
    )
else()
    add_library(vhl_input STATIC button_manager.c slider_filter.c)
    target_include_directories(vhl_input PUBLIC include)
    target_link_libraries(vhl_input PUBLIC vhl_core vhl_runtime)
endif()
