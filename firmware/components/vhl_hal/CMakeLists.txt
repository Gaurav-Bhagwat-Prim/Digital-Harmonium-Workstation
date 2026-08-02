# firmware/components/vhl_hal/CMakeLists.txt
if(DEFINED ENV{IDF_PATH})
    idf_component_register(
        INCLUDE_DIRS "include"
        REQUIRES vhl_core
    )
else()
    add_library(vhl_hal INTERFACE)
    target_include_directories(vhl_hal INTERFACE include)
    target_link_libraries(vhl_hal INTERFACE vhl_core)
endif()
