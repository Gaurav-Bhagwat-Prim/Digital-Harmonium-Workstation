# firmware/components/vhl_runtime/CMakeLists.txt
if(DEFINED ENV{IDF_PATH})
    idf_component_register(
        INCLUDE_DIRS "include"
        REQUIRES vhl_core vhl_hal
    )
else()
    add_library(vhl_runtime INTERFACE)
    target_include_directories(vhl_runtime INTERFACE include)
    target_link_libraries(vhl_runtime INTERFACE vhl_core vhl_hal)
endif()
