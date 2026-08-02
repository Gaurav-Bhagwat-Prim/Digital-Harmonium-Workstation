# firmware/components/vhl_audio/CMakeLists.txt
if(DEFINED ENV{IDF_PATH})
    idf_component_register(
        INCLUDE_DIRS "include"
        REQUIRES vhl_core vhl_hal vhl_runtime
    )
else()
    add_library(vhl_audio INTERFACE)
    target_include_directories(vhl_audio INTERFACE include)
    target_link_libraries(vhl_audio INTERFACE vhl_core vhl_hal vhl_runtime)
endif()
