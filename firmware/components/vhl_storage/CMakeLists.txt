# firmware/components/vhl_storage/CMakeLists.txt
if(DEFINED ENV{IDF_PATH})
    idf_component_register(
        INCLUDE_DIRS "include"
        REQUIRES vhl_core vhl_runtime
    )
else()
    add_library(vhl_storage INTERFACE)
    target_include_directories(vhl_storage INTERFACE include)
    target_link_libraries(vhl_storage INTERFACE vhl_core vhl_runtime)
endif()
