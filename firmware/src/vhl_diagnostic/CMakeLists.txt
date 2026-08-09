# firmware/components/vhl_diagnostic/CMakeLists.txt
if(DEFINED ENV{IDF_PATH})
    idf_component_register(
        INCLUDE_DIRS "include"
        REQUIRES vhl_core vhl_runtime
    )
else()
    add_library(vhl_diagnostic INTERFACE)
    target_include_directories(vhl_diagnostic INTERFACE include)
    target_link_libraries(vhl_diagnostic INTERFACE vhl_core vhl_runtime)
endif()
