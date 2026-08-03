# firmware/main/CMakeLists.txt
if(DEFINED ENV{IDF_PATH})
    idf_component_register(
        SRCS "main.c"
        INCLUDE_DIRS "."
        REQUIRES vhl_core vhl_hal vhl_runtime vhl_audio vhl_input vhl_feature vhl_storage vhl_ui vhl_diagnostic
    )
else()
    add_executable(digital_harmonium_firmware main.c)
    target_include_directories(digital_harmonium_firmware PRIVATE 
        "${CMAKE_CURRENT_SOURCE_DIR}/../components/vhl_storage/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../components/vhl_feature/include"
    )
    target_link_libraries(digital_harmonium_firmware PRIVATE vhl_core vhl_hal vhl_runtime vhl_input vhl_audio vhl_storage vhl_feature)
endif()
