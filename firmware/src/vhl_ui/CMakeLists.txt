# firmware/components/vhl_ui/CMakeLists.txt
if(DEFINED ENV{IDF_PATH})
    idf_component_register(
        INCLUDE_DIRS "include"
        REQUIRES vhl_core vhl_runtime
    )
else()
    add_library(vhl_ui INTERFACE)
    target_include_directories(vhl_ui INTERFACE include)
    target_link_libraries(vhl_ui INTERFACE vhl_core vhl_runtime)
endif()
