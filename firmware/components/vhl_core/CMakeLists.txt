# firmware/components/vhl_core/CMakeLists.txt
if(DEFINED ENV{IDF_PATH})
    idf_component_register(
        INCLUDE_DIRS "include"
    )
else()
    add_library(vhl_core INTERFACE)
    target_include_directories(vhl_core INTERFACE
        include
        ${CMAKE_CURRENT_SOURCE_DIR}/../vhl_hal/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../vhl_runtime/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../vhl_audio/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../vhl_input/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../vhl_feature/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../vhl_storage/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../vhl_ui/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../vhl_diagnostic/include
    )
endif()
