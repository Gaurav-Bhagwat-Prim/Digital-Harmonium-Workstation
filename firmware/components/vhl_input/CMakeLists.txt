# firmware/components/vhl_input/CMakeLists.txt
if(DEFINED ENV{IDF_PATH})
    idf_component_register(
        INCLUDE_DIRS "include"
        REQUIRES vhl_core vhl_hal vhl_runtime
    )
else()
    add_library(vhl_input INTERFACE)
    target_include_directories(vhl_input INTERFACE include)
    target_link_libraries(vhl_input INTERFACE vhl_core vhl_hal vhl_runtime)
endif()
